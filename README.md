# LongPet V0.2

LongPet V0.2 是面向 1024×600 触控终端的 Qt 6 Widgets 应用。本版本在 V0.1 的正式 UI 骨架上完成了首个可持久化的本地业务闭环：提醒管理、今日关怀、用户设置与设备状态入口。

## 已实现

- `Application` 作为组合根，统一创建和销毁数据库、Repository、Service、Controller 与窗口；
- `AppController` 负责页面流程和 15 秒控制页无操作返回；
- SQLite 版本化建库与事务迁移；
- 提醒新增、编辑、删除、完成、每日/工作日/单次调度与防重复触发；
- 喝水、用药完成、活动分钟和互动次数的本地汇总；
- 音量、亮度、宠物风格持久化；音量通过 ALSA mixer 接入，背光通过 sysfs 接入；
- 状态栏真实时钟，以及基于 QNetworkInformation 的事件驱动网络状态；
- power-supply 电池状态读取，以及无电池设备的正常降级；
- FamilyLink 局域网 API，可读取设备状态并远程管理设置与提醒；
- QRC 内嵌 QSS/SVG，保留后续版本会使用的页面与资源；
- 正式页面全部使用语义信号，页面不直接访问 SQL 或硬件。

开发板启动脚本会使用 `linuxfb` 显示后端和 `evdevtouch` 触摸后端。未接入的硬件与远端能力会明确显示“未检测到”或“待接入”，不会用假数据冒充可用状态。

## 目录

```text
src/app/       应用组合根与业务流程控制
src/model/     跨层数据模型
src/data/      SQLite、Repository 与迁移
src/services/  提醒、关怀、设置、系统状态
src/platform/  操作系统与设备能力 Adapter
src/pages/     正式页面及保留的后续页面
src/widgets/   可复用视觉组件
resources/     内嵌样式与图标
tests/         V0.2 自动化与页面渲染验证
docs/          版本工作报告
```

## 构建

需要 CMake 3.21+、C++17 与 Qt 6.5+，Qt 组件为 Core、Gui、Widgets、Svg、Sql、Network；测试还需要 Qt Test。

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

- `NetworkStatusAdapter → SystemService::setNetworkState`
- `AudioVolumeAdapter ← SettingsService::settingApplyRequested(volume)`
- `BacklightAdapter ← SettingsService::settingApplyRequested(brightness)`
- `PowerStatusAdapter → SystemService::setBatteryPercent`
- `SystemService::setWeatherSummary`
- `CareService::recordActivityMinutes / recordInteraction`
- `ReminderService::reminderTriggered`
- `FamilyLinkHttpAdapter → FamilyLinkController → FamilyLinkService`

具体上机验证项和当前限制见 [V0.2 工作报告](docs/LongPet-V0.2-Work-Report.md)。

## FamilyLink API

应用默认仅监听 `127.0.0.1:8787`，当前开放：

- `GET /api/v1/status`
- `GET /api/v1/settings`
- `PATCH /api/v1/settings`
- `GET /api/v1/reminders`
- `POST /api/v1/reminders`
- `PUT /api/v1/reminders/{id}`
- `DELETE /api/v1/reminders/{id}?expectedRevision={revision}`

设置与提醒写入均使用持久化 revision 做乐观锁；旧版本写入返回 HTTP 409，客户端刷新后再提交。音量或亮度 Adapter 不可用时，对应远程字段返回 HTTP 503，不会写入数据库。

可通过以下环境变量配置：

- `LONGPET_FAMILY_LINK_PORT`：监听端口，默认 `8787`；
- `LONGPET_FAMILY_LINK_ADDRESS`：监听地址，默认 `127.0.0.1`；
- `LONGPET_FAMILY_LINK_TOKEN`：Bearer Token；非回环监听时必须配置；
- `LONGPET_DEVICE_ID`、`LONGPET_DEVICE_NAME`：家属端显示的设备标识和名称。

局域网监听必须使用 Token，且不得将端口映射到公网。只读连接基线见 [FamilyLink 只读连接报告](docs/LongPet-FamilyLink-ReadOnly-Report.md)，写入实现、测试方法与回滚记录见 [FamilyLink 写入报告](docs/LongPet-FamilyLink-Write-Report.md)。
systemd drop-in 示例见 `deploy/longpet-familylink.conf.example`，示例中的 Token 占位值必须替换。

## LS2K300 板端运行

交叉构建完成后，将 `LongPet` 与 `scripts/run-board.sh` 放入
`/root/mytest/qt`，并赋予启动脚本执行权限。启动脚本默认使用：

- framebuffer：`/dev/fb0`，Qt `linuxfb`；
- 触摸：`/dev/input/event0`，Qt `evdevtouch`；
- 数据库：`/root/mytest/qt/data/longpet.db`。

可选的 systemd 单元位于 `deploy/longpet.service`。实际设备路径均可通过
启动脚本中的环境变量覆盖，应用代码不依赖页面或窗口直接访问硬件。
