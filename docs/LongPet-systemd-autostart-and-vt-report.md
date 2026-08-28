# LongPet systemd 自启动与 Linux VT/Framebuffer 显示接管配置报告

## 1. 工作目标

本次工作主要完成 LongPet 在嵌入式 Linux 板卡上的正式部署与开机自启动配置，并解决 Qt `linuxfb` 运行时与 Linux 虚拟终端（VT）之间的显示冲突问题。

主要目标如下：

- 使用独立 `longpet` 用户运行 LongPet，避免以 `root` 身份长期运行应用；
- 使用 systemd 管理 LongPet 生命周期；
- 实现 LongPet 开机自动启动；
- 为 LongPet 配置 framebuffer、触摸屏、音频等设备权限；
- 明确 Qt `linuxfb` 使用 `/dev/fb0`；
- 解决 Qt 无权访问 Linux VT 导致的：

  ```text
  Failed to open tty (Permission denied)
  ```

- 解决 LCD 上残留 Linux 控制台光标的问题；
- 禁止本地 `tty1` 登录终端与 LongPet 抢占显示；
- 保留串口控制台用于板端调试。

---

## 2. 系统显示结构

当前 LongPet 不使用 X11 或 Wayland，而是通过 Qt `linuxfb` 平台插件直接访问 Linux framebuffer。

显示链路为：

```text
LongPet
   │
   ▼
Qt 6
   │
   ▼
linuxfb
   │
   ▼
/dev/fb0
   │
   ▼
LCD
```

Linux 本身同时存在 framebuffer console：

```text
Linux Kernel
   │
   ▼
fbcon
   │
   ▼
VT / tty1
   │
   ▼
/dev/fb0
   │
   ▼
LCD
```

因此，如果不处理 VT，Linux 控制台和 Qt 都可能操作同一个 framebuffer。

这会导致：

- Linux 控制台光标出现在 LongPet UI 上；
- 启动日志或控制台内容可能覆盖 Qt 界面；
- Qt `linuxfb` 无法完整接管显示设备。

---

## 3. Linux TTY / VT 情况确认

执行：

```bash
cat /sys/class/tty/tty0/active
```

结果：

```text
tty1
```

说明当前 framebuffer console 使用的活动虚拟终端为：

```text
/dev/tty1
```

其中：

```text
/dev/tty0
```

表示当前活动 VT 的入口，而：

```text
/dev/tty1
```

表示 Virtual Terminal 1。

需要特别注意：

```text
/dev/tty1
```

不是串口。

当前板卡串口控制台为：

```text
/dev/ttyS0
```

U-Boot/Linux bootargs 中存在：

```text
console=ttyS0,115200
```

因此：

```text
tty1  → LCD 本地虚拟终端
ttyS0 → 串口调试终端
```

关闭 `tty1` 上的 getty 不会影响串口调试。

---

## 4. getty 状态检查

检查：

```bash
systemctl status getty@tty1.service
```

原状态为：

```text
Loaded: loaded
Active: inactive (dead)
```

说明当前 `tty1` 上已经没有运行登录程序，但为了防止未来 systemd 或其他机制再次启动本地 getty，将其进一步 mask：

```bash
systemctl mask getty@tty1.service
```

这样 `tty1` 被明确保留给 LongPet / Qt 使用。

串口 `ttyS0` 不受此操作影响。

---

## 5. `/dev/tty1` 权限问题

修改前设备权限：

```text
crw-rw---- 1 root video 29, 0 /dev/fb0
crw--w---- 1 root tty    4, 0 /dev/tty0
crw--w---- 1 root tty    4, 1 /dev/tty1
```

其中：

```text
/dev/fb0
```

属于：

```text
root:video
```

且权限为：

```text
0660
```

因此 `longpet` 通过 `video` 组可以正常读写 framebuffer。

但 `/dev/tty1` 原权限为：

```text
0620
```

即：

```text
root  → rw
tty   → w
other → 无权限
```

即使 LongPet 加入 `tty` 组，也只有写权限，没有完整读写权限。

Qt `linuxfb` 因此无法正常打开该 VT，并产生：

```text
Failed to open tty (Permission denied)
```

同时 Qt 无法正常将 VT 从文本模式切换至图形模式，导致 LCD 上仍然出现 Linux 控制台光标。

---

## 6. 使用 udev 固化 tty1 权限

由于 `/dev/tty1` 属于动态设备节点，仅直接执行：

```bash
chmod 660 /dev/tty1
```

无法保证重启后仍然有效。

因此增加永久 udev 规则：

```text
/etc/udev/rules.d/70-longpet-vt.rules
```

内容：

```udev
SUBSYSTEM=="tty", KERNEL=="tty1", GROUP="tty", MODE="0660"
```

重新加载：

```bash
udevadm control --reload-rules
udevadm trigger --subsystem-match=tty
```

最终 `/dev/tty1` 应具有：

```text
crw-rw---- root tty
```

即：

```text
0660
```

这样属于 `tty` 组的 LongPet 进程即可正常读写 `tty1`。

---

## 7. LongPet systemd 服务配置

systemd 服务文件位置：

```text
/etc/systemd/system/longpet.service
```

最终配置如下：

```ini
[Unit]
Description=LongPet touch terminal
After=dev-fb0.device NetworkManager.service
Wants=dev-fb0.device NetworkManager.service

[Service]
Type=simple

User=longpet
Group=longpet
SupplementaryGroups=video input audio tty

WorkingDirectory=/home/longpet
ExecStart=/home/longpet/LongPet

RuntimeDirectory=longpet
RuntimeDirectoryMode=0700

Environment="HOME=/home/longpet"
Environment="XDG_RUNTIME_DIR=/run/longpet"
Environment="LONGPET_DATABASE_PATH=/home/longpet/data/longpet.db"

Environment="QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:tty=/dev/tty1"
Environment="QT_QPA_GENERIC_PLUGINS=evdevtouch:/dev/input/event0"
Environment="QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=/dev/input/event0"
Environment="QT_QPA_FB_HIDECURSOR=1"
Environment="QT_IM_MODULE=qtvirtualkeyboard"

Environment="LONGPET_ALSA_MIXER_DEVICE=hw:CARD=Device"

Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

---

## 8. systemd 配置说明

### 8.1 独立用户运行

```ini
User=longpet
Group=longpet
```

LongPet 不再使用 root 身份运行。

额外设备权限通过：

```ini
SupplementaryGroups=video input audio tty
```

赋予。

各组用途：

```text
video → /dev/fb0 framebuffer
input → /dev/input/* 触摸设备
audio → ALSA 音频设备
tty   → /dev/tty1 VT 控制
```

这样相比直接使用 root 权限更加安全和明确。

### 8.2 Qt framebuffer 配置

配置：

```ini
Environment="QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:tty=/dev/tty1"
```

明确指定：

```text
fb=/dev/fb0
```

作为 Qt 图形输出设备。

同时明确指定：

```text
tty=/dev/tty1
```

作为 Qt 使用的 Linux VT。

因此 Qt 启动后能够正常接管 `tty1`，并将相应 VT 切换到适合图形应用运行的状态，避免 framebuffer console 的光标干扰 LongPet UI。

---

## 9. 触摸屏配置

当前触摸输入设备使用：

```text
/dev/input/event0
```

systemd 配置：

```ini
Environment="QT_QPA_GENERIC_PLUGINS=evdevtouch:/dev/input/event0"
Environment="QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=/dev/input/event0"
```

Qt 通过 evdev 直接读取触摸屏事件。

目前配置有效。

后续如发现设备枚举顺序可能变化，可进一步通过 udev 创建固定设备别名，避免 `event0` 在硬件变化后变成 `event1` 等。

---

## 10. XDG Runtime Directory

systemd 配置：

```ini
RuntimeDirectory=longpet
RuntimeDirectoryMode=0700
```

并设置：

```ini
Environment="XDG_RUNTIME_DIR=/run/longpet"
```

systemd 会为 LongPet 创建：

```text
/run/longpet
```

作为运行时目录。

该目录用于 Qt 等程序的临时 runtime 数据，不用于永久保存数据。

---

## 11. 数据库路径

配置：

```ini
Environment="LONGPET_DATABASE_PATH=/home/longpet/data/longpet.db"
```

LongPet 在启动时通过环境变量读取数据库路径。

因此数据库被固定保存到：

```text
/home/longpet/data/longpet.db
```

而不是依赖当前登录用户或 Qt 默认数据目录。

---

## 12. systemd 开机自启动

执行：

```bash
systemctl daemon-reload
systemctl enable --now longpet.service
```

systemd 创建：

```text
/etc/systemd/system/multi-user.target.wants/longpet.service
    ->
/etc/systemd/system/longpet.service
```

说明：

```ini
WantedBy=multi-user.target
```

已经生效。

LongPet 会在系统正常进入 multi-user target 时自动启动。

---

## 13. 当前运行状态

最终执行：

```bash
systemctl status longpet.service --no-pager
```

结果：

```text
● longpet.service - LongPet touch terminal
     Loaded: loaded (/etc/systemd/system/longpet.service; enabled; preset: enabled)
     Active: active (running)
   Main PID: 3896 (LongPet)
      Tasks: 4
     Memory: 16.7M
     CGroup: /system.slice/longpet.service
             └─3896 /home/longpet/LongPet
```

说明：

```text
systemd 服务加载       正常
开机自启动             已启用
LongPet 进程           正常运行
独立用户运行           正常
```

---

## 14. TTY 问题修复结果

修改前日志：

```text
LongPet: Failed to open tty (Permission denied)
```

完成 `/dev/tty1` 权限和 Qt `tty=/dev/tty1` 配置后，最新启动日志为：

```text
Started LongPet touch terminal.
QNetworkInformation backend loaded: "networkmanager"
Audio volume adapter unavailable: "ALSA mixer 中没有 PCM 音量控件"
```

原来的：

```text
Failed to open tty (Permission denied)
```

已经不再出现。

说明：

- Qt 已经能够正常访问指定 VT；
- `/dev/tty1` 权限配置生效；
- Linux VT/Qt linuxfb 接管问题已解决；
- LCD 控制台光标问题对应的根本原因已经处理。

---

## 15. NetworkManager 状态

LongPet 启动日志：

```text
QNetworkInformation backend loaded: "networkmanager"
```

说明 Qt 网络信息模块成功加载 NetworkManager backend。

本次 systemd 改造没有影响网络模块初始化。

---

## 16. 当前剩余的音频警告

当前日志仍存在：

```text
Audio volume adapter unavailable: "ALSA mixer 中没有 PCM 音量控件"
```

该问题与 systemd、VT、framebuffer 无关。

目前 LongPet 使用：

```ini
Environment="LONGPET_ALSA_MIXER_DEVICE=hw:CARD=Device"
```

指向 USB 声卡。

当前 LongPet `AudioVolumeAdapter` 主要寻找 ALSA mixer 中的：

```text
PCM
```

音量控件。

但当前 USB 声卡使用的 mixer control 很可能为：

```text
Speaker
```

或其他名称，因此无法找到 `PCM`。

后续应修改 AudioVolumeAdapter，使播放音量控件支持例如：

```text
PCM
  ↓ 不存在

Speaker
  ↓ 不存在

Master
```

等候选项。

该问题目前不会阻止 LongPet 主程序启动。

---

## 17. systemctl restart 时的退出码

日志中出现：

```text
Stopping LongPet touch terminal...
longpet.service: Main process exited, code=exited, status=1/FAILURE
longpet.service: Failed with result 'exit-code'.
Stopped LongPet touch terminal.
Started LongPet touch terminal.
```

这是执行服务重启时旧 LongPet 进程退出产生的。

随后新的 LongPet 实例已经成功启动：

```text
Started LongPet touch terminal.
```

且当前状态：

```text
Active: active (running)
```

因此不影响本次部署结果。

不过从程序规范性角度看，LongPet 在收到正常退出请求时最好返回：

```text
exit code 0
```

而不是：

```text
exit code 1
```

后续可以检查 Qt 应用关闭流程或信号处理逻辑，使正常 systemd stop/restart 被记录为正常退出。

此问题优先级低于 USB 音频适配。

---

## 18. 最终系统结构

当前 LongPet 显示与启动架构为：

```text
                    Linux / systemd
                           │
             ┌─────────────┴─────────────┐
             │                           │
          ttyS0                        tty1
             │                           │
        串口控制台                    本地 VT
        保留调试功能                     │
                                         │
                                  getty 已禁用/mask
                                         │
                                   Qt linuxfb 接管
                                         │
                ┌────────────────────────┴──────────────┐
                │                                       │
             /dev/tty1                              /dev/fb0
              VT 控制                                图形输出
                │                                       │
                └──────────────────┬────────────────────┘
                                   │
                                LongPet
                                   │
                             /home/longpet/LongPet
                                   │
                               systemd 管理
```

LongPet 的权限结构为：

```text
longpet 用户
   │
   ├── video → framebuffer
   ├── input → touchscreen
   ├── audio → ALSA
   └── tty   → VT1
```

---

## 19. 当前结论

本次工作已完成以下内容：

- [x] LongPet 独立用户运行
- [x] systemd 服务配置
- [x] 开机自动启动
- [x] framebuffer 权限配置
- [x] 输入设备权限配置
- [x] 音频设备组权限配置
- [x] `/dev/tty1` VT 权限配置
- [x] udev 永久权限规则
- [x] Qt `linuxfb` 显式绑定 `/dev/fb0`
- [x] Qt 显式绑定 `/dev/tty1`
- [x] tty1 getty 禁用
- [x] 保留 ttyS0 串口调试终端
- [x] `Failed to open tty (Permission denied)` 已解决
- [x] NetworkManager Qt backend 正常加载
- [x] LongPet 当前处于 `active (running)`

当前剩余非阻塞问题：

1. USB 声卡 ALSA mixer 没有 `PCM` 控件，需要修改 LongPet 音量适配器；
2. systemd stop/restart 时 LongPet 旧进程返回 exit code 1，后续可改进为正常的 exit code 0；
3. 当前触摸屏使用固定 `/dev/input/event0`，后续可根据需要使用 udev 创建稳定设备别名。

总体而言，LongPet 的 systemd 自启动、普通用户权限管理以及 Qt `linuxfb`/Linux VT 显示接管已经完成，可以作为当前板端正式启动方案使用。
