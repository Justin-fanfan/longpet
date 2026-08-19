# LongPet 开发板 RTL8188FU 启动自愈与 Qt/linuxfb 抢屏处理报告

- 实施日期：2026-08-17（Asia/Shanghai）
- 目标设备：LongPet 开发板，`10.234.167.51`
- 系统：Buildroot 2024.08，LoongArch64
- 内核：Linux `6.12.0.lsgd`
- 涉及组件：DWC2、RTL8188FU、`rtl8xxxu`、NetworkManager、systemd、Qt `linuxfb`、fbcon、getty

## 1. 结论摘要

本次完成了两项处理：

1. 为 RTL8188FU 开机首次 `rtl8xxxu` probe 偶发失败、导致 `wlan0` 缺失的问题安装了安全、有限重试的启动自愈机制。
2. 确认并保持 LCD 虚拟终端 `getty@tty0.service` 为 masked/inactive，保留 `serial-getty@ttyS0.service` 为 enabled/active，从用户态消除 agetty 与 Qt `linuxfb` 对当前虚拟终端和 framebuffer 的竞争。

自愈机制的关键约束均已落实：

- systemd 和脚本进行双重 `wlan0` 存在性检查；`wlan0` 已存在时绝不重载。
- 最多重试 3 次，不存在无限循环。
- 每次卸载驱动后等待 2 秒，再加载并最多等待 6 秒观察 `wlan0`。
- 启动前最多等待 30 秒让初始 udev coldplug/probe 收敛，避免与正常首次 probe 竞争。
- 整个 oneshot 单元设 70 秒上限；3 次失败后记录日志并正常退出。
- 自愈单元不属于 LongPet 或 NetworkManager 的 `Wants/Requires/After` 链，LongPet 不等待它；NetworkManager 可在接口稍后出现时动态接管。
- 没有修改 `/dev/fb0`、framebuffer/DRM 驱动或 fbcon 绑定。

受控故障注入验证成功：人为卸载 `rtl8xxxu` 并确认 `wlan0` 消失后，自愈单元在第 1/3 次尝试恢复 `wlan0`；NetworkManager 自动重新连接原 Wi-Fi、取得原地址并恢复默认路由及公网访问；LongPet 全程保持同一 PID，重启次数为 0；串口 getty 保持正常。

## 2. 问题现象

### 2.1 RTL8188FU 偶发无 `wlan0`

异常启动时，USB 层仍能枚举无线网卡：

```text
Bus 001 Device 002: ID 0bda:f179 Realtek 802.11n
```

但接口没有绑定驱动：

```text
Bus 001 ... Driver=dwc2
  └─ Dev 002 ... Driver=[none]
```

内核能识别 RTL8188FU、读取 MAC、找到固件并读取固件版本，随后发生多次 USB block write 失败，probe 最终返回 `-11`：

```text
usb 1-1: RTL8188FU rev B (SMIC) ...
usb 1-1: RTL8188FU MAC: 84:fc:14:b8:ee:f9
usb 1-1: rtl8xxxu: Loading firmware rtlwifi/rtl8188fufw.bin
usb 1-1: Firmware revision 4.0 (signature 0x88f1)
usb 1-1: rtl8xxxu_writeN: Failed to write block ...
rtl8xxxu 1-1:1.0: probe with driver rtl8xxxu failed with error -11
```

结果是：

- `/sys/class/net/wlan0` 不存在；
- `iw dev` 无无线接口；
- NetworkManager 没有可管理的 Wi-Fi 设备；
- LongPet 中基于 NetworkManager 的网络状态只能呈现离线。

### 2.2 默认终端页面与 Qt `linuxfb` 抢屏

原冲突入口为：

```text
/etc/systemd/system/getty.target.wants/getty@tty0.service
```

它实例化模板：

```text
/usr/lib/systemd/system/getty@.service
```

板上模板的关键配置为：

```ini
ExecStart=-/sbin/agetty -a root --noclear - $TERM
StandardInput=tty
StandardOutput=tty
TTYPath=/dev/%I
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes
```

实例为 `tty0` 时，agetty 自动登录 root 并操作 `/dev/tty0`。检查时当前活动 VT 为：

```text
/sys/class/tty/tty0/active = tty1
```

LongPet 同时明确使用 framebuffer 和当前虚拟终端：

```text
QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0
/proc/<LongPet PID>/fd/5 -> /dev/fb0
/proc/<LongPet PID>/fd/6 -> /dev/tty0
```

因此，agetty/fbcon 的终端输出、VT 清理/切换与 Qt 对 `/dev/fb0`、`/dev/tty0` 的使用会互相覆盖，形成“默认终端页面”和 Qt UI 抢屏。

## 3. 证据链与根因判断

### 3.1 本次启动的 Wi-Fi 证据

本次启动日志再次复现原问题：

```text
20:29:33 dwc2 16040000.otg: DWC OTG Controller
20:29:33 usb 1-1: new high-speed USB device number 2 using dwc2
20:29:51 usb 1-1: RTL8188FU rev B ...
20:29:52 usb 1-1: rtl8xxxu: Loading firmware rtlwifi/rtl8188fufw.bin
20:29:52 usb 1-1: Firmware revision 4.0 ...
20:29:52 rtl8xxxu_writeN: Failed to write block ...（多次）
20:29:52 rtl8xxxu 1-1:1.0: probe ... failed with error -11
```

在 20:38 手工执行卸载、重载后，仍可见少量 `rtl8xxxu_writeN` 失败，但没有再出现 probe failed，随后产生 `wlan0`、完成关联并取得 DHCP 地址。这证明：

- USB 设备、VID/PID、固件文件、MAC 读取和基本型号支持是有效的；
- 问题不是 Wi-Fi 密码或 NetworkManager profile 缺失；
- 同一硬件不重启即可通过重新 probe 恢复，不像永久硬件损坏；
- 单条 `rtl8xxxu_writeN: Failed to write block` 不必然等于整个 probe 失败，关键在于初始化期间错误的数量、位置或底层返回结果；
- probe 日志中的 `-11` 不能单独还原 DWC2/USB control transfer 的原始失败类型，仍需更底层动态调试才能区分 timeout、short transfer、pipe error 等。

### 3.2 根因判断

当前最合理的根因范围为：

```text
DWC2 host 初始化/USB 控制传输稳定性
        ↕
RTL8188FU 上电与固件下载时序
        ↕
rtl8xxxu 初始化容错与重试策略
```

这是“USB host—芯片—驱动初始化窗口”的时序/稳定性问题。自愈机制是工程绕过措施，并没有从内核或硬件层彻底消除根因。

可排除或显著降级的方向包括：

- LongPet Qt 业务逻辑；
- `QNetworkInformation`；
- NetworkManager 启动失败；
- 保存的 Wi-Fi 连接配置或密码错误；
- firmware 文件不存在；
- USB 设备完全未枚举；
- 永久性无线芯片损坏。

## 4. 改动前启动链检查

### 4.1 `longpet.service`

板上原单元：

```ini
[Unit]
Description=LongPet touch terminal
After=NetworkManager.service systemd-timesyncd.service
Wants=NetworkManager.service systemd-timesyncd.service

[Service]
Type=simple
WorkingDirectory=/root/mytest/qt
ExecStart=/root/mytest/qt/run-board.sh
SuccessExitStatus=1
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

观察结果：

- `longpet.service` enabled、active/running；
- LongPet 在本次启动的 12.117 秒开始运行；
- 它只等待 NetworkManager 守护进程启动，不依赖 `NetworkManager-wait-online.service`，也不依赖 `wlan0`；
- 因此没有 Wi-Fi 时仍可离线启动，这是应保留的性质。

### 4.2 NetworkManager

NetworkManager 为 enabled、active/running，启动时间约为 8.700 秒。初始 probe 失败时，它没有能力创建内核网络接口；驱动后来创建 `wlan0` 时，它能够动态发现设备并自动连接。

### 4.3 模块加载来源

在以下位置没有发现 `rtl8xxxu` 静态加载配置：

```text
/etc/modules-load.d
/usr/lib/modules-load.d
/etc/modules
/etc/modprobe.d
/usr/lib/modprobe.d
```

`rtl8xxxu` 是 USB 设备 modalias 匹配后由内核/udev 自动加载，不适合通过简单重复添加 modules-load 条目解决。自愈需要发生在初始 udev probe 已结束且 `wlan0` 仍缺失之后。

## 5. 启动自愈设计

### 5.1 放置位置

- 管理员本地脚本：`/usr/local/sbin/rtl8xxxu-wlan0-recover`
- 管理员本地 systemd 单元：`/etc/systemd/system/rtl8xxxu-wlan0-recover.service`
- 启用链接：`/etc/systemd/system/multi-user.target.wants/rtl8xxxu-wlan0-recover.service`

这样不修改 Buildroot 提供的 `/usr/lib/systemd/system`，系统包/镜像更新时边界清晰，回滚也只涉及两个明确文件和一个 enable 链接。

### 5.2 执行逻辑

```text
systemd 激活
  ├─ wlan0 已存在：ConditionPathExists 不满足，直接跳过
  └─ wlan0 不存在：运行脚本
       ├─ 等待初始 udev 工作收敛（最多 30 秒）
       ├─ wlan0 已出现：退出，不重载
       └─ wlan0 仍缺失：最多 3 次
            ├─ 操作前再次检查 wlan0
            ├─ modprobe -r rtl8xxxu
            ├─ 等待 2 秒
            ├─ modprobe rtl8xxxu
            └─ 最多等待 6 秒观察 wlan0
```

上限估算为：

```text
30 秒 udev settle + 3 ×（2 秒复位 + 6 秒观察）= 54 秒
```

另设 `TimeoutStartSec=70s` 作为外层硬上限。脚本重试耗尽后记录警告并返回成功，避免可选 Wi-Fi 失败把系统标记为因本单元失败；即使脚本异常被 70 秒上限终止，LongPet 与此单元之间也没有依赖或顺序边，应用仍可独立运行。

## 6. 实际安装内容

### 6.1 `/usr/local/sbin/rtl8xxxu-wlan0-recover`

```sh
#!/bin/sh

# Recover an RTL8188FU whose first rtl8xxxu probe did not create wlan0.
# This helper is intentionally finite and harmless to offline LongPet startup.

set -u

PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH

WLAN_PATH=/sys/class/net/wlan0
MAX_ATTEMPTS=3
RESET_DELAY_SECONDS=2
APPEAR_TIMEOUT_SECONDS=6
UDEV_SETTLE_TIMEOUT_SECONDS=30

log()
{
    printf '%s\n' "rtl8xxxu-wlan0-recover: $*"
}

if [ -e "$WLAN_PATH" ]; then
    log "wlan0 already exists; no action needed"
    exit 0
fi

if command -v udevadm >/dev/null 2>&1; then
    log "waiting up to ${UDEV_SETTLE_TIMEOUT_SECONDS}s for initial udev work"
    if ! udevadm settle --timeout="$UDEV_SETTLE_TIMEOUT_SECONDS"; then
        log "udev settle timed out; continuing with bounded recovery"
    fi
fi

if [ -e "$WLAN_PATH" ]; then
    log "wlan0 appeared during udev settle; no module reload needed"
    exit 0
fi

attempt=1
while [ "$attempt" -le "$MAX_ATTEMPTS" ]; do
    if [ -e "$WLAN_PATH" ]; then
        log "wlan0 appeared before attempt ${attempt}; stopping"
        exit 0
    fi

    log "wlan0 is absent; reload attempt ${attempt}/${MAX_ATTEMPTS}"

    if modprobe -r rtl8xxxu; then
        sleep "$RESET_DELAY_SECONDS"
    else
        log "attempt ${attempt}: could not unload rtl8xxxu; trying to load it"
    fi

    if ! modprobe rtl8xxxu; then
        log "attempt ${attempt}: modprobe rtl8xxxu failed"
    fi

    waited=0
    while [ "$waited" -lt "$APPEAR_TIMEOUT_SECONDS" ]; do
        if [ -e "$WLAN_PATH" ]; then
            log "recovered wlan0 on attempt ${attempt}"
            exit 0
        fi
        sleep 1
        waited=$((waited + 1))
    done

    log "attempt ${attempt}: wlan0 still absent after ${APPEAR_TIMEOUT_SECONDS}s"
    attempt=$((attempt + 1))
done

log "giving up after ${MAX_ATTEMPTS} attempts; leaving LongPet offline startup unaffected"
exit 0
```

文件权限为 `0755 root:root`。

### 6.2 `/etc/systemd/system/rtl8xxxu-wlan0-recover.service`

```ini
[Unit]
Description=Recover RTL8188FU wlan0 after a failed rtl8xxxu probe
After=systemd-udev-trigger.service
ConditionPathExists=!/sys/class/net/wlan0

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/rtl8xxxu-wlan0-recover
TimeoutStartSec=70s
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

文件权限为 `0644 root:root`，当前 `UnitFileState=enabled`。

### 6.3 文件校验值

```text
32aef72addaf1a965570577376a44b225bedd9a39b44f7ff233e4dcf915ec644  /usr/local/sbin/rtl8xxxu-wlan0-recover
d84748f42d21437f671f90c8fdae5757d5ae19d7b224091b689725ba47a91c9c  /etc/systemd/system/rtl8xxxu-wlan0-recover.service
```

脚本通过 `sh -n`；单元安装后通过 `systemd-analyze verify`。校验过程中还观察到一个与本次单元无关的既有警告：`/usr/lib/systemd/system/multi-user.target.wants/boot_run_service` 不是合法 unit name；它没有导致新单元验证失败。

## 7. 默认终端与 Qt `linuxfb` 抢屏处理

### 7.1 已实施状态

LCD getty：

```text
/etc/systemd/system/getty@tty0.service -> /dev/null
getty@tty0.service: masked, inactive
```

原 `/etc/systemd/system/getty.target.wants/getty@tty0.service` 启用链接当前已不存在。实施时再次以幂等方式执行 disable/stop 与 mask，确认最终状态。

串口 getty 未被屏蔽或删除：

```text
/etc/systemd/system/getty.target.wants/serial-getty@ttyS0.service
  -> /usr/lib/systemd/system/serial-getty@.service
serial-getty@ttyS0.service: enabled, active
```

### 7.2 getty 与 fbcon 是两个层次

屏蔽 `getty@tty0.service` 只停止用户态 agetty，不等于解绑内核 fbcon，也不等于移除 kernel console。

本次检查的内核命令行为：

```text
earlycon fbcon=logo-pos:center fbcon=logo-count:1 console=ttyS0,115200 noinitrd init=/sbin/init rootfstype=ext4 rw rootwait root=/dev/mmcblk0p1 ... fbcon=rotate:0 panel=default ...
```

其中：

- `console=ttyS0,115200` 指定内核主 console 为串口；
- `fbcon=logo-pos:center`、`fbcon=logo-count:1`、`fbcon=rotate:0` 仍配置 framebuffer console；
- `/proc/fb` 显示 `0 loongsondrmfb`；
- `/sys/class/vtconsole/vtcon1/name` 为 `(M) frame buffer device`，`bind=1`，说明 fbcon 仍绑定；
- `/sys/class/tty/tty0/active` 仍为 `tty1`。

因此本次动作只解决“agetty 自动登录页面与 Qt UI 的用户态抢屏”。启动 logo、内核日志或 fbcon 自身对 framebuffer 的写入属于另一层，需要另行设计内核命令行或 VT/fbcon 策略，不能通过 disable getty 假定已经消失。

### 7.3 明确未做的动作

- 未删除或破坏 `/dev/fb0`；
- 未卸载 `loongsondrmfb`/DRM/framebuffer driver；
- 未写 `/sys/class/vtconsole/*/bind` 来解绑 fbcon；
- 未删除 `getty@.service` 模板；
- 未禁用 `serial-getty@ttyS0.service`；
- 未移除内核命令行中的 `console=ttyS0,115200`。

## 8. 实施动作记录

1. 只读检查系统、LongPet、NetworkManager、模块、USB 拓扑、`wlan0`、getty、VT、内核命令行和 framebuffer 状态。
2. 确认 `rtl8xxxu` 由 USB/udev 自动加载，不存在静态 modules-load 配置。
3. 确认 LongPet 使用 `/dev/fb0` 与 `/dev/tty0`，`QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0`。
4. 在临时目录传入脚本和 unit，核对 SHA-256，并做 shell/systemd 语法校验。
5. 安装脚本和 service，执行 `systemctl daemon-reload` 与 enable。
6. 幂等确认 `getty@tty0.service` 为 masked/inactive；验证串口 getty 未受影响。
7. 先验证“健康接口不操作”路径，再进行受控故障注入与恢复验证。
8. 检查 NetworkManager 日志、IP/路由/网关/公网、LongPet PID 与重启计数、getty 状态。

## 9. 验证结果

### 9.1 健康路径：`wlan0` 存在时不重载

直接执行脚本输出：

```text
rtl8xxxu-wlan0-recover: wlan0 already exists; no action needed
```

启动 service 时 systemd 条件结果：

```text
ConditionResult=no
Result=success
```

执行前后：

```text
wlan0 ifindex: 5 -> 5
LongPet PID:   262 -> 262
NetworkManager: wlan0 connected realme_Neo7_g9qr
```

这验证了“仅当 `/sys/class/net/wlan0` 不存在时才重载”。

### 9.2 故障路径：模拟 `wlan0` 缺失

测试由独立 transient systemd 单元执行，避免 SSH 短暂中断中止后续恢复命令。过程日志：

```text
20:57:15 usbcore: deregistering interface driver rtl8xxxu
20:57:15 wlan0: deauthenticating ... Reason: DEAUTH_LEAVING
TEST_MISSING_CONFIRMED
rtl8xxxu-wlan0-recover: wlan0 is absent; reload attempt 1/3
rtl8xxxu-wlan0-recover: recovered wlan0 on attempt 1
TEST_WLAN0_RECOVERED ifindex=6
```

恢复 probe 期间仍有 3 条 `rtl8xxxu_writeN` 失败，但没有 `probe failed`，最终：

```text
Bus 001 ... Dev 002 ... Driver=rtl8xxxu
phy#2
Interface wlan0
ifindex 6
```

### 9.3 NetworkManager 自动接管

NetworkManager 日志完整显示：

```text
20:57:15 wlan0: activated -> unmanaged (reason removed)
20:57:20 new 802.11 Wi-Fi device
20:57:22 Activation: starting connection 'realme_Neo7_g9qr'
20:57:24 authentication/association completed
20:57:24 DHCP new lease, address=10.234.167.51
20:57:24 set connection as default for IPv4 routing and DNS
20:57:24 Activation: successful, device activated
20:57:24 NetworkManager state: CONNECTED_GLOBAL
```

最终状态：

```text
wlan0 UP, LOWER_UP
connection: realme_Neo7_g9qr
IPv4: 10.234.167.51/24
default route: via 10.234.167.78 dev wlan0
```

连通性验证：

- 默认网关 `10.234.167.78`：3/3 响应，0% 丢包；
- 公网 IP `223.5.5.5`：3/3 响应，0% 丢包；
- `http://www.baidu.com/`：HTTP 200，解析并连接 `36.152.44.93`。

补充：`connectivitycheck.gstatic.com` 单独连接超时，但中国大陆可达地址的 DNS、ICMP 与 HTTP 均成功，因此不影响“接口、NetworkManager、IP、路由和公网已经恢复”的判断。

### 9.4 LongPet 与串口未受影响

故障注入前后 LongPet：

```text
ActiveState=active
SubState=running
ExecMainPID=262
NRestarts=0
ActiveEnterTimestamp=2026-08-17 20:29:44 CST
```

串口：

```text
serial-getty@ttyS0.service: enabled, active
```

LCD getty：

```text
getty@tty0.service: masked, inactive
```

## 10. 运维检查方法

检查接口和 NetworkManager：

```sh
test -e /sys/class/net/wlan0 && echo wlan0-present
lsusb -t
iw dev
nmcli device status
ip -4 -br addr show wlan0
ip route show default
```

检查自愈单元：

```sh
systemctl is-enabled rtl8xxxu-wlan0-recover.service
systemctl status rtl8xxxu-wlan0-recover.service --no-pager -l
journalctl -b -u rtl8xxxu-wlan0-recover.service --no-pager
```

检查 LongPet 与 getty：

```sh
systemctl status longpet.service --no-pager -l
systemctl is-enabled getty@tty0.service
systemctl is-active getty@tty0.service
systemctl is-enabled serial-getty@ttyS0.service
systemctl is-active serial-getty@ttyS0.service
cat /sys/class/tty/tty0/active
```

## 11. 回滚方式

### 11.1 只回滚 Wi-Fi 自愈

```sh
systemctl disable --now rtl8xxxu-wlan0-recover.service
rm -f /etc/systemd/system/rtl8xxxu-wlan0-recover.service
rm -f /usr/local/sbin/rtl8xxxu-wlan0-recover
systemctl daemon-reload
systemctl reset-failed
```

这些命令只删除本次新增的两个明确文件及 enable 链接，不影响 NetworkManager、原 Wi-Fi profile、固件或内核模块。

### 11.2 如需恢复 LCD getty（会重新引入抢屏风险）

```sh
systemctl unmask getty@tty0.service
systemctl enable --now getty@tty0.service
```

不建议在 LongPet 使用 Qt `linuxfb` 的生产形态下执行此回滚。无论是否恢复 LCD getty，都不要禁用 `serial-getty@ttyS0.service`，以保留串口维护入口。

## 12. 已知限制

1. 自愈是启动阶段的工程绕过，不修复 DWC2/USB control transfer 或 RTL8188FU 初始化的底层根因。
2. 机制明确针对模块 `rtl8xxxu` 和接口名 `wlan0`；如果未来 udev 命名策略、驱动或硬件改变，需要同步调整。
3. 每次开机只运行一次。运行期间后续发生的拔插、掉线或驱动崩溃不在本单元的恢复范围内，避免演变为无限守护和反复重载。
4. 驱动恢复后能否自动接入网络仍依赖有效的 NetworkManager 自动连接 profile、凭据及可用 AP。
5. 重试耗尽后单元返回成功以保证离线启动和避免 systemd 因可选 Wi-Fi 降级；必须从 journal 中观察 `giving up after 3 attempts`。
6. `getty@tty0` 被屏蔽并不禁止内核 fbcon 写 framebuffer。若仍看到启动 logo或内核 framebuffer 输出，这是预期的另一层行为。
7. LongPet 日志中的 `This plugin does not support setting window masks` 为 Qt `linuxfb` 插件能力提示，与本次 getty 抢屏或 Wi-Fi 自愈不是同一问题。
8. 板上已有两个与本次无关的 failed unit：`proc-fs-nfsd.mount` 与 `systemd-networkd-wait-online...`；本次未改动它们。

## 13. 后续建议

1. 做至少 30～100 次冷启动统计，记录首次 probe 成功率、自愈触发率、每次恢复尝试次数与失败样本。
2. 在 `rtl8xxxu_writeN()`/USB control message 返回处增加临时动态调试，保留底层返回值和实际传输长度，避免只看到最终 `-11`。
3. 对照更新的内核、DWC2 修复和 RTL8188FU/rtl8xxxu 相关补丁做 A/B 测试。
4. 检查 USB 供电、上电复位、时钟与 DWC2 host 参数，尤其关注冷启动与热重载差异。
5. 若产品要求 LCD 从内核启动到 Qt 全程无任何非 Qt 内容，应另立变更评审内核命令行、启动 logo和 fbcon/VT 策略；保持 framebuffer/DRM driver 本身可用，不以删除 `/dev/fb0` 或破坏驱动作为方案。
6. 清理既有无效 unit 链接 `boot_run_service` 及评估两个既有 failed unit，但应作为独立维护任务，避免与本次已验证变更混合。
