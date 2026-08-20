# LongPet V0.2.1

LongPet 是面向 1024×600 触控终端的 Qt 6 Widgets 应用。V0.2.1 在提醒、关怀、设置和真实板端 Adapter 的基础上，补齐了离线关键词、轻量视觉、Emergency 抢占以及受环境变量保护的 Developer / Diagnostics 控制台。

## 已实现

- `Application` 作为组合根，统一创建和销毁数据库、Repository、Service、Platform Adapter、Controller 与窗口；
- `AppController` 负责页面流程、业务抢占和 15 秒普通控制页无操作返回；
- SQLite V1→V2 事务迁移，兼容旧提醒数据；
- `ReminderPage` 管理提醒；独立 `ReminderAlertPage` 负责到期投递、逐条排队、确认/完成语义和未确认重复；
- Emergency 抢占期间暂停 Reminder scheduler 和展示 timeout，不会静默消耗 `presentation_count`；
- 喝水、用药完成、活动分钟和互动次数的本地汇总；
- 音量、亮度、宠物风格持久化；音量通过 ALSA mixer 接入，背光通过 sysfs 接入；
- 状态栏真实时钟，以及基于 `QNetworkInformation` 的事件驱动网络状态；
- power-supply 电池状态读取，以及无电池设备的正常降级；
- Loongson KWS Python worker：真实 ALSA 设备/采样率/通道配置、RMS/Peak、解码耗时、RTF、关键词延迟和有限恢复；
- Vision Python worker：固定摄像头下的 MOG2 + 挥手轨迹；跌倒只提供可选的实验性 candidate，不宣称安全级确认；
- KWS/Vision worker 使用异步 terminate→kill fallback、独立进程组和 5s/30s/120s 有限重试；
- DeveloperPage 五个标签页：总览、语音、视觉、设备、事件；支持安全重启、真实重配置和不落库 Simulation；
- `DiagnosticsService` 使用固定 200 条 ring buffer，不记录 PCM chunk 或逐帧数据；
- QRC 内嵌 QSS/SVG，页面不直接访问 SQL、QProcess、ALSA、OpenCV 或硬件。

未接入的硬件与远端能力会明确显示“不可用”或“未实现”，不会用假数据冒充可用状态。

## 目录

```text
src/app/       应用组合根与业务流程控制
src/model/     跨层数据模型
src/data/      SQLite、Repository 与迁移
src/services/  业务服务、Developer facade 与诊断 ring buffer
src/platform/  操作系统、worker 与设备枚举 Adapter
src/pages/     正式页面和 DeveloperPage
src/widgets/   可复用视觉组件
resources/     内嵌样式与图标
tests/         自动化、fake worker 与页面渲染验证
docs/          开发、验收与版本报告
```

## Windows 构建与测试

需要 CMake 3.21+、C++17 与 Qt 6.5+。Qt 组件为 Core、Gui、Widgets、Svg、Sql、Network；测试还需要 Qt Test。

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DLONGPET_BUILD_TESTS=ON `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

默认数据库位于 `QStandardPaths::AppLocalDataLocation/longpet.db`。部署时可用 `LONGPET_DATABASE_PATH` 指定绝对路径；目录必须可写。

## 主要接入链

- `NetworkStatusAdapter → SystemService::setNetworkState`
- `AudioVolumeAdapter ← SettingsService::settingApplyRequested(volume)`
- `BacklightAdapter ← SettingsService::settingApplyRequested(brightness)`
- `PowerStatusAdapter → SystemService::setBatteryPercent`
- `KeywordSpottingAdapter → KeywordSpottingService → AppController`
- `VisionAdapter → VisionService → AppController`
- `ReminderService::reminderPresentationRequested → AppController`
- `AudioDeviceAdapter / CameraDeviceAdapter → DeveloperService → DeveloperPage`
- `DiagnosticsService → DeveloperService → DeveloperPage`

DeveloperPage 默认隐藏。开发板调试时设置 `LONGPET_DEVELOPER_MODE=1`，再从“设置 → 关于设备 → 诊断”进入。详见 [Developer / Diagnostics 指南](docs/Developer-Diagnostics.md) 与 [V0.2.1 工作报告](docs/LongPet-V0.2.1-Developer-Diagnostics-Work-Report.md)。

## LS2K300 板端运行

交叉构建完成后，将 `LongPet` 与 `scripts/run-board.sh` 放入 `/root/mytest/qt`，并赋予启动脚本执行权限。启动脚本默认使用：

- framebuffer：`/dev/fb0`，Qt `linuxfb`；
- 触摸：`/dev/input/event0`，Qt `evdevtouch`；
- 数据库：`/root/mytest/qt/data/longpet.db`；
- KWS runtime：可执行文件旁 `kws/`；
- Vision runtime：可执行文件旁 `vision/`。

可选的 systemd 单元位于 `deploy/longpet.service`；开发模式 drop-in 示例为 `deploy/longpet-developer.conf`。实际设备路径可通过环境变量覆盖。

## 当前边界

- KWS V0.2.1 仍独占 ALSA。接入 Remote ASR/TTS 前必须引入统一 `AudioService`，由一次采集同时向 KWS 与 ASR 分发，不能各自启动 `arecord`。
- Vision 是固定摄像头的本地交互能力，不是人体识别或 AutoFollow。未来跟随应独立走 `PersonDetector/Tracker → PersonObservation → AutoFollowController → MotionService → MCU`。
- Remote AI、Motion MCU、Family 网络同步、TTS/家属录音播放仍未实现；诊断页只显示诚实占位。
