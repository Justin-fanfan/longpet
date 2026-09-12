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
- Family Remote Control V1：FamilyLink 临时会话、独立 WebSocket、`MotionService` 与 `/dev/ttyS2` Motion MCU 串口链路；
- QRC 内嵌 QSS/SVG，保留后续版本会使用的页面与资源；
- 正式页面全部使用语义信号，页面不直接访问 SQL 或硬件。

开发板启动脚本会使用 `linuxfb` 显示后端和 `evdevtouch` 触摸后端。未接入的硬件与远端能力会明确显示“未检测到”或“待接入”，不会用假数据冒充可用状态。

## 目录

```text
src/           LongPet 正式 C++ runtime
components/    LongPet 自维护、可独立运行的第一方组件
resources/     内嵌样式、图标和声音
scripts/       构建、训练、验证、benchmark 与数据准备工具
third_party/   固定上游信息、补丁、许可证/provenance 和必要模型
deploy/        板端 service、环境变量和配置样例
tests/         C++ 自动化与页面渲染验证
tools/         随 C++ 工程构建的辅助程序
docs/          设计、实验、开发、部署与维护文档
```

`components/longpet-kws` 是 LongPet 直接维护的 Python KWS 组件；
`third_party/tinyissimo-yolo` 则只保存真正第三方 TinyissimoYOLO 的固定版本、补丁、训练依赖和
正式小型模型。完整边界、目录树和外部训练工作区约定见
[仓库结构与维护说明](docs/Repository-Structure-and-Maintenance.md)。

## 当前 Vision 方向

- Vision V1：FastestDet 352×352 历史基线，板端数秒一帧，不再作为正式实时 detector；
- Vision V1.1：TinyissimoYOLO-v1-small 128×128 person-only FP32 ONNX，已完成接入和性能探索；
- Vision V1.2：完整 COCO person checkpoint 加 LongPet 实拍 domain fine-tune，当前候选模型为
  `tinyissimo-person-128-longpet-v1.onnx`，尚未替换仓库内 V1.1 runtime baseline，等待用户板端验收。

当前产品主线是本地人物检测、在场/方向感知、低频 detector 配合轻量 tracker、后续人物跟随与
主动交互，再逐步探索轻量手势识别。固定摄像头视角明显偏上，跌倒检测仅作为历史探索记录，
不再是当前 roadmap 的下一版本必做项。V1.2 PC 结果与尚未覆盖的无人误报指标见
[Vision V1.2 报告](docs/LongPet-Vision-V1.2-Domain-Finetune-Report.md)。

倒装摄像头可通过 `LONGPET_CAMERA_ROTATION=180` 统一校正。支持 `0/90/180/270`（顺时针）；
Detector 与 Tracker 在解码后旋转，AI 视野和视频通话在 Windows Canvas 旋转原始 JPEG，避免板端
逐帧重新编码。

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
- `FamilyMotionControlAdapter → MotionService → MotionPort → EspSerialAdapter`

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
- `POST /api/v1/motion-control/sessions`

设置与提醒写入均使用持久化 revision 做乐观锁；旧版本写入返回 HTTP 409，客户端刷新后再提交。音量或亮度 Adapter 不可用时，对应远程字段返回 HTTP 503，不会写入数据库。

可通过以下环境变量配置：

- `LONGPET_FAMILY_LINK_PORT`：监听端口，默认 `8787`；
- `LONGPET_FAMILY_LINK_ADDRESS`：监听地址，默认 `127.0.0.1`；
- `LONGPET_FAMILY_LINK_TOKEN`：Bearer Token；非回环监听时必须配置；
- `LONGPET_VISION_MONITOR_PORT`：家属端“AI 视野”WebSocket 端口，默认 `8789`；
- `LONGPET_VISION_MONITOR_FPS`：AI 视野 JPEG 发送帧率，范围 `1~10`，默认 `7`；
- `LONGPET_MOTION_ENABLED`：是否启用 Family Remote Control；
- `LONGPET_MOTION_DEVICE`：Motion MCU 串口，当前板卡为 `/dev/ttyS2`；
- `LONGPET_MOTION_CONTROL_PORT`：独立运动控制 WebSocket 端口，默认 `8790`；
- `LONGPET_MOTION_REFRESH_MS`、`LONGPET_MOTION_REMOTE_LEASE_MS`：MOVE 刷新与 LongPet 停车租约；
- `LONGPET_MOTION_STATUS_POLL_MS`：Motion MCU 状态轮询周期，V2.3 默认 `250` ms；
- `LONGPET_AUTO_HEAD_ENABLED`：启动时是否开启视觉自动跟头，默认关闭；家属端可在 AI 视野页动态切换；
- `LONGPET_AUTO_HEAD_MAX_TARGET_AGE_MS`：单个 observation 可用于控制的最大年龄，默认 `500` ms；
- `LONGPET_AUTO_HEAD_TARGET_EXPIRY_MS`：收到有效目标后等待下一新帧的最长时间，默认 `500` ms；超时只发送一次 target lost；
- `LONGPET_FOLLOW_*`：人物跟随的目标稳定、头身对齐滞回/驻留、归一化 bbox 距离阈值和低速命令。人物跟随没有开机自动启用项，必须由家属端显式开启；
- `LONGPET_DEVICE_ID`、`LONGPET_DEVICE_NAME`：家属端显示的设备标识和名称。

局域网监听必须使用 Token，且不得将端口映射到公网。只读连接基线见 [FamilyLink 只读连接报告](docs/LongPet-FamilyLink-ReadOnly-Report.md)，写入实现、测试方法与回滚记录见 [FamilyLink 写入报告](docs/LongPet-FamilyLink-Write-Report.md)。
systemd drop-in 示例见 `deploy/longpet-familylink.conf.example`，示例中的 Token 占位值必须替换。
运动控制架构、协议、安全策略与实机验收边界见 [Family Remote Control V1 报告](docs/LongPet-Family-Remote-Control-V1-Report.md)。

## LS2K300 板端运行

交叉构建完成后，将 `LongPet` 与 `scripts/run-board.sh` 放入
`/root/mytest/qt`，并赋予启动脚本执行权限。启动脚本默认使用：

- framebuffer：`/dev/fb0`，Qt `linuxfb`；
- 触摸：`/dev/input/event0`，Qt `evdevtouch`；
- 数据库：`/root/mytest/qt/data/longpet.db`。

可选的 systemd 单元位于 `deploy/longpet.service`。实际设备路径均可通过
启动脚本中的环境变量覆盖，应用代码不依赖页面或窗口直接访问硬件。
