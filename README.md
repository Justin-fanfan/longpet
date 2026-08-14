# LongPet V0.2

LongPet V0.2 是面向 1024×600 触控终端的 Qt 6 Widgets 应用。本版本在 V0.1 的正式 UI 骨架上完成了首个可持久化的本地业务闭环：提醒管理、今日关怀、用户设置与设备状态入口。

## 已实现

- `Application` 作为组合根，统一创建和销毁数据库、Repository、Service、Controller 与窗口；
- `AppController` 负责页面流程和 15 秒控制页无操作返回；
- SQLite 版本化建库与事务迁移；
- 提醒新增、编辑、删除、完成、每日/工作日/单次调度与防重复触发；
- 喝水、用药完成、活动分钟和互动次数的本地汇总；
- 音量、亮度、宠物风格持久化，设备应用通过 Service 信号预留；
- 状态栏真实时钟，以及网络、电量、天气的统一输入接口；
- QRC 内嵌 QSS/SVG，保留后续版本会使用的页面与资源；
- 正式页面全部使用语义信号，页面不直接访问 SQL 或硬件。

未接入的硬件与远端能力会明确显示“待接入”，不会用假数据冒充可用状态。

## 目录

```text
src/app/       应用组合根与业务流程控制
src/model/     跨层数据模型
src/data/      SQLite、Repository 与迁移
src/services/  提醒、关怀、设置、系统状态
src/pages/     正式页面及保留的后续页面
src/widgets/   可复用视觉组件
resources/     内嵌样式与图标
tests/         V0.2 自动化与页面渲染验证
docs/          版本工作报告
```

## 构建

需要 CMake 3.21+、C++17 与 Qt 6.5+，Qt 组件为 Core、Gui、Widgets、Svg、Sql；测试还需要 Qt Test。

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DLONGPET_BUILD_TESTS=ON `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.11.0/mingw_64
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

默认数据库位于 `QStandardPaths::AppLocalDataLocation/longpet.db`。部署时可用环境变量 `LONGPET_DATABASE_PATH` 指定绝对路径；该目录必须可写。

## 设备接入点

- `SystemService::setNetworkState / setBatteryPercent / setWeatherSummary`
- `SettingsService::settingApplyRequested`
- `CareService::recordActivityMinutes / recordInteraction`
- `ReminderService::reminderTriggered`

具体上机验证项和当前限制见 [V0.2 工作报告](docs/LongPet-V0.2-Work-Report.md)。
