# LongPet V0.2 开发板软硬件联调工作报告

- 日期：2026-08-17
- 本地工程：`D:\code_qt\longpet`
- 开发板：Loongson LS2K300，SSH 地址 `10.234.167.51`
- 板端工作目录：`/root/mytest/qt`
- 构建环境：WSL `Ubuntu-24.04`
- 构建脚本：`scripts/build-loongarch.sh`

> 本报告不记录 SSH 口令。正式部署前应更换当前 root 口令，并优先改用密钥认证。

## 1. 完成结论

本轮已完成 LongPet V0.2 在 LS2K300 开发板上的真实软硬件接入、交叉构建、部署、持久化验证、故障恢复验证和整板重启验证。

已接通并验证的接口：

1. `QNetworkInformation` + `networkmanager` 后端的真实网络状态；
2. ES8388 声卡的 ALSA mixer 音量控制；
3. Linux sysfs 背光控制，并按真实硬件能力识别为 GPIO 开/关；
4. Linux power-supply 电池状态读取及无电池降级；
5. `/dev/fb0` + Qt `linuxfb` 的 1024×600 显示；
6. Goodix 触摸屏 + Qt `evdevtouch`；
7. systemd 启动、自动重启和整板重启后的自动恢复；
8. systemd-timesyncd 启用，以及系统时间问题的定位。

程序当前已在开发板上运行，`longpet.service` 为 `enabled/active`。最终二进制 SHA-256：

```text
010b507680b18aa022e50339cdc398422ab91b32c5edacf7ca3d977927ca61d2
```

## 2. 板端实际环境

| 项目 | 实测结果 | 处理结论 |
|---|---|---|
| SoC / 机器 | Loongson LS2K300 | 与交叉工具链目标一致 |
| 系统 | Buildroot 2024.08 | 使用 systemd、NetworkManager、ALSA、sysfs |
| Kernel | Linux 6.12.0.lsgd | 目标 ELF 声明 Linux 6.6+，可运行 |
| 内存 | 369 MiB，无 swap | LongPet 稳态 RSS 约 55–60 MiB，应继续关注后续功能增长 |
| Qt 运行库 | 实测 Qt 6.8.1 | 与原先“Qt 6.5”信息有差异；工程要求 6.5+，SDK 与板端均为 6.8.1，当前兼容 |
| 显示 | `/dev/fb0`，1024×600×32，BGRA/BGRX，stride 4096 | 使用 `linuxfb:fb=/dev/fb0` |
| 触摸 | Goodix Capacitive TouchScreen，`/dev/input/event0` | 显式加载 `evdevtouch`，进程已持有该节点 |
| 网络 | `wlan0`，Realtek RTL8188FU，NetworkManager | `QNetworkInformation` 实际加载 `networkmanager` |
| 声卡 | ALSA card 0，ES8388 | 直接使用 libasound mixer API |
| 背光 | `/sys/class/backlight/backlight`，`max_brightness=1` | 设备树为 `gpio-backlight`，仅支持开/关 |
| 电池 | `/sys/class/power_supply` 无设备 | 电量保持未知，不显示假百分比 |
| RTC | `loongson-rtc`，`/dev/rtc0` | 内核可见，但 `hwclock --show` 会超时，见风险项 |
| 摄像头 | 无 `/dev/video*`、无 `/dev/media*` | 当前没有可接入的摄像头设备 |
| 串口 | console 为 ttyS0，另有 ttyS2 | 未获得外设协议，不向串口发送数据 |
| I2C / SPI / GPIO | i2c-0/1/2/4、spidev1.0/2.0/2.1、gpiochip0 | 仅盘点；没有协议/引脚定义时未做风险写入 |
| ADC | IIO `1611c000.adc`，8 路 raw 值 | 无通道用途和电压/电量换算资料，不冒充电池数据 |

## 3. 架构与实现

### 3.1 分层关系

```text
SettingsPage
    ↓ 语义信号
SettingsService（先持久化）
    ↓ settingApplyRequested
Application（组合根与分发）
    ├─ AudioVolumeAdapter ── libasound ── ES8388
    └─ BacklightAdapter  ── sysfs ────── gpio-backlight

NetworkStatusAdapter ── QNetworkInformation ── SystemService ── UI
PowerStatusAdapter   ── power_supply sysfs ─── SystemService ── UI
```

Page、Widget、MainWindow 均未直接依赖 ALSA、sysfs、NetworkManager 或 `QNetworkInformation`。Adapter 由 `Application` 创建、启动、停止和销毁，符合现有组合根职责。

### 3.2 网络

- 保留现有 `NetworkStatusAdapter`；
- 板端真实加载 `networkmanager`，日志为 `QNetworkInformation backend loaded: "networkmanager"`；
- 未使用 `QProcess`、`nmcli` 轮询或额外线程；
- Wi-Fi 当前为 `connected:full`；
- 网关 10 包、互联网地址 5 包测试均为 0% 丢包；
- Adapter 后端不可用时仍降级为未知，不阻止应用启动。

### 3.3 音量 / ES8388

新增 `AudioVolumeAdapter`：

- Linux/交叉构建检测到 ALSA 时直接链接 `libasound.so.2`；
- Windows 或普通 CI 没有 ALSA 时编译为可正常降级的 stub；
- 使用 `PCM` 控件作为 0–100 用户音量；
- ES8388 的 `Output 1`、`Output 2` 设为 75% 的固定安全输出增益；
- 音量非零时打开 Left/Right Mixer 播放路由，音量为零时关闭；
- 写入失败会把设备状态降级并反馈到 `SystemService`，而不是继续显示“已接入”。

板端最终状态：PCM 60%，Output 1/2 75%，左右播放路由为 on。

### 3.4 背光

新增 `BacklightAdapter`：

- 自动发现 `/sys/class/backlight`；
- 使用 `max_brightness` 完成 0–100 到 raw level 的换算；
- 当前板 `max_brightness=1`，因此设置页自动切换为“关/开”，不再显示虚假的连续百分比；
- 若未来设备树提供多级/PWM backlight，无需修改 Page，即可恢复为 0–100 滑杆；
- `LONGPET_BACKLIGHT_DEVICE` 可覆盖默认 sysfs 路径。

### 3.5 电池 / 电源

新增 `PowerStatusAdapter`：

- 只在 platform 层扫描 power-supply；
- 电池存在时读取 `capacity`，使用 30 秒 very-coarse timer 更新；
- 当前板无 power-supply 电池节点，因此向 `SystemService` 发布 `-1/未检测到电池`；
- 没有使用 IIO ADC raw 值编造电池百分比。

### 3.6 显示、触摸与进程管理

新增 `scripts/run-board.sh`：

- `QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0`；
- `QT_QPA_GENERIC_PLUGINS=evdevtouch:/dev/input/event0`；
- `QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=/dev/input/event0`；
- 隐藏 framebuffer 鼠标指针；
- 数据库固定为 `/root/mytest/qt/data/longpet.db`。

新增 `deploy/longpet.service`：

- 开机自动启动；
- 进程异常退出 2 秒后自动拉起；
- 手动 stop/restart 时不再把 Qt linuxfb 的退出码 1 误判为部署故障；
- 依赖 NetworkManager 和 systemd-timesyncd 的启动，但不因无互联网而阻止 LongPet 启动。

## 4. 修改文件清单

### 新增

- `src/platform/AudioVolumeAdapter.h`
- `src/platform/AudioVolumeAdapter.cpp`
- `src/platform/BacklightAdapter.h`
- `src/platform/BacklightAdapter.cpp`
- `src/platform/PowerStatusAdapter.h`
- `src/platform/PowerStatusAdapter.cpp`
- `scripts/run-board.sh`
- `deploy/longpet.service`
- `reports/LongPet-V0.2-Board-Integration-Report.md`

### 修改

- `CMakeLists.txt`：加入三个 Adapter；Linux 可选发现/链接 ALSA；
- `src/app/Application.h/.cpp`：统一持有、连接、启动和销毁 Adapter；
- `src/app/AppController.cpp`：设置保存提示不再显示“接口待接入”，也不虚报硬件一定写入成功；
- `src/model/SettingsModels.h`：补充音量、背光、电源能力摘要；
- `src/services/SystemService.h/.cpp`：集中保存并发布设备能力状态；
- `src/pages/SettingsPage.h/.cpp`：显示真实设备摘要，GPIO 背光切换为二态控件；
- `tests/V02Test.cpp`：增加音量映射、背光、无电池降级、二态 UI 测试；
- `README.md`：补充真实接口和板端运行说明。

## 5. 构建与自动化测试

### 5.1 Windows / 普通 CI 路径

- 配置：Qt 6.11.0、MinGW 13.1、Release、`LONGPET_BUILD_TESTS=ON`；
- 编译：成功；
- CTest：`LongPet.V02` 1/1 通过；
- 最终自动化耗时：1.32 秒；
- 页面渲染测试：3 passed，0 failed；
- `git diff --check`：无空白错误。

本机如果优先加载 `C:\mingw64` 的运行库，Qt Test 会在初始化时挂起。将以下目录放到 PATH 前部后测试正常：

```text
D:\Qt\Tools\mingw1310_64\bin
D:\Qt\6.11.0\mingw_64\bin
```

### 5.2 WSL LoongArch 交叉构建

执行：

```bash
cd /mnt/d/code_qt/longpet
./scripts/build-loongarch.sh
```

结果：成功生成 LoongArch 64-bit ELF，解释器为 `/lib64/ld-linux-loongarch-lp64d.so.1`。动态依赖包含：

- Qt6 Widgets / Svg / Gui / Sql / Network / Core；
- `libasound.so.2`；
- 标准 C/C++ 运行库。

## 6. 板端测试结果

| 测试 | 结果 |
|---|---|
| ELF 动态库检查 | 全部找到，无 `not found` |
| linuxfb 启动 | 通过，真实 framebuffer 抓取为完整 1024×600 画面 |
| evdevtouch 插件 | 通过，插件加载，进程持有 `/dev/input/event0` |
| QNetworkInformation | 通过，加载 `networkmanager` |
| Wi-Fi 连通 | 通过，`connected:full`，15 包测试 0% 丢包 |
| ALSA mixer | 通过，PCM/Output/Mixer 路由均按设置写入 |
| PCM 播放设备 | 通过，48 kHz/16-bit/双声道持续打开 2 秒，无驱动错误 |
| PCM 录音设备 | 通过，48 kHz/16-bit/双声道录制约 361 KB，无驱动错误 |
| 背光写入 | 通过，0/1 均实测生效 |
| 设置持久化 | 通过，数据库 35%/关 → 重启后硬件 35%/0；恢复 60%/开 → 硬件 60%/1 |
| 数据库 | 通过，`/root/mytest/qt/data/longpet.db` 可写且重启保留 |
| systemd restart | 通过，手动 restart 后 active |
| 故障恢复 | 通过，SIGKILL 后 2 秒自动换 PID 拉起，`NRestarts=1` |
| 整板重启 | 通过，SSH、Wi-Fi、LongPet、显示、触摸、音量、背光、数据库均自动恢复 |

重启后 LongPet 进程实际持有：

```text
/dev/fb0
/dev/input/event0
/dev/snd/controlC0
/root/mytest/qt/data/longpet.db
```

## 7. 板端最终部署状态

保留文件：

```text
/root/mytest/qt/LongPet
/root/mytest/qt/run-board.sh
/root/mytest/qt/longpet.service
/root/mytest/qt/data/longpet.db
/root/mytest/qt/LongPet.previous-20260817-1819
/etc/systemd/system/longpet.service
```

`longpet.service` 已 enable 且 active。原有的 `LongPet_0.1.0`、`LongPet_0.2.0`、UI demo、测试程序等均未删除。只清理了本轮生成的临时 framebuffer、录音、日志和基线文件。

## 8. 仍需人工验证或用户敲定

### 8.1 必须人工验证

1. **扬声器听感**：SSH 可以验证 mixer 和 PCM 写入，但无法判断实际响度、爆音、左右声道和扬声器接线。请现场试听 0%、20%、60%、100%。
2. **麦克风听感**：录音设备可打开且能产生数据，但仍需回放确认底噪、增益、声道和麦克风接线。
3. **触摸边缘与坐标**：插件和设备节点已接通，仍需现场点击四角、返回按钮、设置滑杆，确认无旋转、镜像或边缘偏移。
4. **屏幕物理效果**：framebuffer 内容正确，仍需确认面板颜色、亮度和休眠/唤醒观感。

### 8.2 需要敲定

1. **是否需要连续亮度**：当前硬件和设备树只能开/关。如果产品要求 0–100 调光，需要确认 PWM 引脚、频率、极性，并把设备树从 `gpio-backlight` 改为可调 backlight；应用层已经兼容多级 sysfs。
2. **音量增益策略**：当前 Output 1/2 固定 75%，用户滑杆控制 PCM。请现场确认最大音量是否安全、60% 是否足够；确认后可调整固定模拟增益或改为感知响度曲线。
3. **串口/I2C/SPI/GPIO 外设协议**：目前只确认节点存在。接入电机、IMU、灯、按键或控制板前，需要提供端口、波特率、帧格式、校验、超时、引脚复用和失控保护。没有这些资料时不应试发控制指令。
4. **ADC/电池映射**：如果 8 路 ADC 中有电池电压，需要给出通道、分压比例、电池化学体系和电压-电量曲线；否则继续保持“未检测到电池”是正确行为。
5. **摄像头方案**：当前没有 `/dev/video*`。如后续要做视觉互动，请先确定 USB/MIPI 摄像头型号、驱动和稳定的 V4L2 节点。

## 9. 后续注意事项与风险

### 高优先级

1. **RTC / NTP**：初次联调时系统时间停在 2024 年且 NTP 未启用；本轮已 enable systemd-timesyncd，曾成功同步到 2026-08-17。整板重启后 RTC/系统时间可恢复到接近当前时间，但 `hwclock --show` 仍会超时，且 `ntp.ntsc.ac.cn` 在重启后的前两分钟多次超时、尚未再次标记 synchronized。提醒业务依赖准确时间，建议在系统镜像层修复 RTC 驱动/备份电源，并配置至少两个可靠 NTP 源。
2. **SSH 安全**：当前允许 root 口令登录。请更换口令、启用密钥、按需要限制 SSH 来源。
3. **服务权限**：当前服务以 root 运行，因为 framebuffer/input/backlight 节点权限较严格。量产前建议创建 `longpet` 用户，加入 audio/video/input 组，并用 udev 规则只授权所需背光节点。

### 中优先级

1. RTL8188FU 启动日志出现多条 `rtl8xxxu_writeN: Failed to write block`，当前连接和丢包测试正常，但建议做 24 小时 Wi-Fi 稳定性、断网重连和热点切换测试。
2. 板端总内存 369 MiB、无 swap；LongPet 稳态 RSS 约 55–60 MiB。后续加入语音、摄像头、AI SDK 前应设内存预算并持续观察峰值。
3. 当前无摄像头、无 power-supply 电池节点；这些不是应用 bug，不能在 UI 中用假状态替代。
4. 不建议让应用通过 `QProcess` 调用 `amixer`、`nmcli` 或 shell 写硬件；继续维持 platform Adapter 的直接 API/sysfs 接入。

## 10. 建议的下一轮现场验收顺序

1. 现场完成扬声器、麦克风和触摸四角验证；
2. 修复 RTC/NTP 启动可靠性，再验证断电冷启动后的提醒日期；
3. 连续运行 24 小时，记录 RSS、CPU、服务重启次数和 Wi-Fi 断线次数；
4. 确认 PWM、ADC、串口及摄像头硬件方案后，再新增相应 Adapter；
5. 完成专用用户/udev 权限和 SSH 安全加固。
