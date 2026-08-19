# LongPet V0.2 工作报告

- 完成日期：2026-08-14
- 最近更新：2026-08-15（真实网络状态 Adapter）
- 工程目录：`D:\code_qt\longpet`
- 架构依据：`Justin-fanfan/longpetui_2` 的 `longpet-architecture-package` 分支
- 架构基线提交：`4618ea34f7bbc07f5affa7be0c35650b38232255`
- 软件版本：`0.2.0`

## 1. 结论

LongPet V0.2 已完成。

本版本不再只是 V0.1 的 UI 骨架，而是完成了第一批真实、可持久化、可测试的本地业务闭环：

1. 提醒可新增、编辑、删除、完成并保存到 SQLite；
2. 每日、工作日和单次提醒可由单一调度器安排，且重启后不会重复触发当天已投递提醒；
3. 今日关怀可汇总饮水、用药完成、活动分钟和互动次数；
4. 音量、亮度和宠物风格可以持久化；
5. 状态栏与设置页使用统一的真实系统状态模型，不再自行制造状态；
6. 页面、业务流程、Service 和数据访问已经分层，页面不直接操作 SQL 或硬件；
7. 正式导航已经包含陪伴、首页、今日关怀、提醒、提醒编辑和设置；
8. 原型中后续会使用的页面、组件、图标和样式继续保留并参与编译，没有为了缩小 V0.2 范围而做无意义删减。
9. 网络状态已通过 Qt QNetworkInformation 的 NetworkManager 后端事件驱动接入。

当前工程已通过 Release 构建、自动化测试和 1024×600 页面截图验收。

## 2. 架构复核范围

本次重新按目录复核了架构包中的 104 份文档，覆盖：

- 应用组合根、AppController、AppState、PetStateMachine；
- Reminder、Care、Settings、System 等 V0.2 Service；
- DatabaseManager、Repository、SQLite Schema 和持久化约束；
- MainWindow、正式页面、公共 Widget、QSS/QRC 和原型迁移规则；
- Reminder/Care、启动、离线、语音、感知、紧急与家属同步流程；
- 音频、视觉、运动 MCU、串口、推理与外部节点边界；
- 单核运行、线程模型、降级恢复、安全隐私与日志约束；
- CMake、配置、部署、测试策略和版本路线图。

V0.2 的实现边界遵循架构包定义：优先建立本地数据和业务闭环，语音、感知、运动、家属通信和远端 AI 保持明确接口，不在本版本中伪造实现。

## 3. 架构调整

### 3.1 应用组合根

新增 `Application` 作为唯一组合根，统一负责：

- 解析数据库路径；
- 打开和迁移 SQLite；
- 创建 Repository；
- 创建 Reminder、Care、Settings、System Service；
- 创建 MainWindow 与 AppController；
- 按依赖逆序停止和销毁对象。

原先集中在 `main.cpp` / `MainWindow` 的生命周期责任已经拆开。`main.cpp` 现在只负责 QApplication 元数据、样式加载、应用初始化和进入事件循环。

### 3.2 业务流程控制

新增 `AppController`，由它决定：

- 陪伴页到首页的切换；
- 首页到关怀、提醒、设置的切换；
- 提醒的新建、编辑、保存、删除和完成；
- 关怀记录刷新；
- 设置保存反馈；
- 提醒触发后的页面与 Toast 行为；
- 控制页 15 秒无操作返回陪伴页。

`MainWindow` 现在只做页面容器、模型转发和语义信号汇总，不再承担数据库或业务决策。

### 3.3 分层依赖

当前正式链路为：

```text
Page / Widget
    ↓ semantic signal
MainWindow
    ↓
AppController
    ↓
Service
    ↓
Repository
    ↓
SQLite
```

静态扫描结果：

- 页面和 Widget 层直接引用 SQL / Repository / DatabaseManager：0 处；
- 源码中的阻塞 `sleep` / `waitFor`：0 处；
- 源码中的开发机绝对路径：0 处；
- 正式代码中的 fake / mock / 随机演示数据：0 处；
- 未加入 CMake 的 `.cpp`：0 个。

## 4. 数据层

### 4.1 DatabaseManager

已实现：

- 每个实例使用独立 Qt SQL connection name；
- 自动创建数据目录；
- `QSQLITE` 打开与错误反馈；
- `PRAGMA foreign_keys = ON`；
- `PRAGMA busy_timeout = 3000`；
- `schema_meta` 版本表；
- 事务化建表和迁移；
- 拒绝打开高于当前程序支持版本的数据库；
- 测试可使用 `:memory:` 或临时数据库；
- 正确关闭并移除 Qt SQL connection。

当前 Schema 版本为 1。

### 4.2 表结构

| 表 | 用途 |
|---|---|
| `schema_meta` | 数据库版本 |
| `reminders` | 提醒定义、重复规则、启用状态和 revision |
| `reminder_events` | 提醒投递、完成和错过记录 |
| `care_events` | 饮水、活动、互动等关怀事件 |
| `settings` | 用户设置键值 |

同时建立了提醒时间、提醒事件时间和关怀事件时间索引。

### 4.3 Repository

新增：

- `ReminderRepository`；
- `CareEventRepository`；
- `SettingsRepository`。

提醒更新采用 revision 乐观并发检查。旧编辑页提交不会静默覆盖较新的内容，而会返回“请刷新后重试”。

## 5. 提醒闭环

### 5.1 数据模型

提醒支持：

- 类型：用药、喝水、其他；
- 重复：每天、工作日、仅一次；
- 状态：待完成、已完成、已错过、已停用；
- 单次提醒日期；
- revision；
- 创建和更新时间。

### 5.2 ReminderService

已实现：

- 标题规范化与默认标题；
- 时间和标题校验；
- 单次提醒必须晚于当前时间；
- 查询、保存、删除和完成；
- 每日、工作日、单次的下一次发生时间计算；
- 单一 next-due QTimer，而不是每条提醒一个定时器；
- 最大 60 秒重检，能适应普通系统时间推进；
- 当日事件检测，应用重启不会对已投递的同一提醒重复投递；
- 过期单次提醒显示为已错过；
- `reminderTriggered(Reminder)` 作为后续声音、TTS 或实体表现入口。

### 5.3 提醒页面

- 列表来自 Service 模型，不再写死两条数据；
- 支持空状态和滚动；
- 点击提醒主体进入编辑；
- “完成”按钮发出独立语义信号；
- 状态颜色和图标覆盖完成、待办、错过、停用；
- 编辑页支持类型、重复规则、内容、时间和单次日期；
- 非单次规则会隐藏日期控件，避免误导；
- 新建时不显示删除，编辑时显示删除。

## 6. 今日关怀闭环

`CareService` 当前汇总：

- 当日饮水杯数；
- 今日适用的启用用药提醒总数；
- 当日已完成的用药提醒数；
- 活动分钟；
- 有效互动次数。

正式入口：

```cpp
CareService::recordWater()
CareService::recordActivityMinutes(int minutes, const QString& source)
CareService::recordInteraction(int count, const QString& source)
```

活动和互动接口带来源字段，后续设备适配器不需要直接写数据库。

页面已移除固定假统计：没有设备输入时明确显示“待设备接入 / 待感知接入”。饮水和用药数据来自本地记录。

## 7. 设置与系统状态

### 7.1 SettingsService

已实现并持久化：

- 音量 0～100；
- 亮度 0～100；
- 宠物风格“温和陪伴 / 活泼陪伴”。

滑块有 180 ms 防抖，避免拖动时高频写数据库。

保存音量或亮度后会发出：

```cpp
SettingsService::settingApplyRequested(key, value)
```

这是设备音量和背光 Adapter 的正式接入点。当前值代表“已保存的期望设置”；硬件应用状态仍需设备实现反馈。

### 7.2 SystemService

`SystemService` 统一维护：

- 当前时间；
- 网络是否已知、是否可用和摘要；
- 电池百分比；
- 天气摘要；
- 软件版本与家属状态摘要。

输入接口为：

```cpp
SystemService::setNetworkState(bool known, bool available, const QString& summary)
SystemService::setBatteryPercent(int percent)
SystemService::setWeatherSummary(const QString& summary)
```

新增 Platform 层 `NetworkStatusAdapter`。它负责加载 QNetworkInformation 默认后端、监听 reachability / transport medium / captive portal 变化，并通过现有 `setNetworkState` 写入 SystemService。Linux + Qt 6.5 的默认后端为 `networkmanager`；加载失败只发布“网络状态未知”，不阻止 LongPet 启动。

当前网络摘要可区分：未知、未连接、Wi-Fi / 以太网等介质下无互联网、已联网以及需要门户认证。页面、Widget 和 MainWindow 均不依赖 QNetworkInformation。

首页和 4 个正式业务页头部的 5 个 StatusBarWidget 会收到同一份状态模型，时间和设备摘要不会再各页不一致。

### 7.3 状态栏修复

- StatusBarWidget 高度保持 64 px；
- 设置按钮保持独立 64×64；
- 设置图标为 32×32 并完整落在组件矩形内；
- 系统摘要和设置按钮使用独立右侧容器；
- 自动化测试检查按钮 geometry 未超出 StatusBarWidget；
- 视觉截图确认图标不再截断。

## 8. UI 与资源

正式导航页：

1. CompanionPage；
2. HomePage；
3. CarePage；
4. ReminderPage；
5. ReminderEditPage；
6. SettingsPage。

ConversationPage、EmergencyPage、SleepPage 仍作为后续版本资产保留并参加编译，没有删除。

资源策略：

- 当前 27 个 QRC 文件全部存在；
- 26 个 SVG 均能被 QSvgRenderer 正确解析；
- QSS 继续统一管理触控按钮、输入框、滑块、提醒状态和 Toast；
- 没有运行时绝对资源路径；
- 没有因为 V0.2 暂时未进入某页面而删除以后会使用的资源。

本轮视觉验收额外修复：

- 设置页双栏说明文字与滑块/按钮互相覆盖；
- 网络摘要过长换行不稳定；
- 今日关怀“活动 / 互动”占位信息横向挤压；
- 今日关怀主卡片与底部按钮总高度过紧；
- 非单次提醒仍显示日期控件。

## 9. 构建与验证

### 9.1 构建结果

- CMake 配置：成功；
- Release 主程序 `LongPet.exe`：成功；
- Release 测试程序 `LongPetV02Tests.exe`：成功；
- 编译选项：C++17、`-Wall -Wextra -Wpedantic`；
- Qt 模块：Core、Gui、Widgets、Svg、Sql、Network、Test。

主程序与测试原先各自重复编译全部业务源码，高并发时会形成不必要的内存峰值。本次增加仅用于构建复用的 `LongPetCore` OBJECT target，两套可执行程序共享同一批业务对象，不增加运行时库。高并发 Release 构建已重新验证成功，工程代码没有产生编译错误或警告。

### 9.2 自动化测试

CTest 结果：

```text
Test: LongPet.V02
Result: Passed
100% tests passed, 0 tests failed
```

覆盖内容：

- 所有内嵌资源存在且 SVG 有效；
- SQLite 建库、表和版本；
- Reminder CRUD、revision 冲突和完成状态；
- Application 组合根初始化与数据库路径；
- 当日提醒启动补偿和重启防重复投递；
- 网络状态枚举映射、连接介质、门户状态和 backend 缺失降级；
- Care 与 Settings 持久化；
- 活动和互动正式接入接口；
- 页面模型输入和语义信号；
- AppController 导航；
- 15 秒无操作返回；
- StatusBarWidget 64 px、设置按钮范围和跨页状态广播；
- 6 个正式页面 1024×600 渲染。

### 9.3 视觉验收

以下页面均按 1024×600 实际抓图检查：

- companion；
- home；
- care；
- reminder；
- reminder-edit；
- settings。

最终截图目录：`build-win-v02-test/captures/`。该目录属于构建产物，不进入版本库。

## 10. 上机需要接入和验证的项目

### P0：V0.2 上机前必须确认

#### 10.1 SQLite 运行环境

1. 确认 Qt 的 QSQLITE driver 能被程序发现；
2. 确认数据库目录存在且 LongPet 进程可写；
3. 建议明确设置 `LONGPET_DATABASE_PATH`，不要依赖不同系统镜像的默认数据目录；
4. 验证首次建库、二次启动、提醒 CRUD 和设置重启保留；
5. 验证异常断电后的数据库可重新打开；
6. 备份真实数据库后再验证未来 Schema 升级。

#### 10.2 系统状态 Adapter

网络状态 Adapter 已完成。开发板仍需人工验证：

- 启动日志中的 backend 为 `networkmanager`；
- Wi-Fi 联网显示“Wi-Fi · 已联网”；
- 拔掉有线、关闭 Wi-Fi、无默认连接时显示“未连接”；
- 连接路由器但阻断互联网时显示“Wi-Fi / 以太网 · 无互联网”；
- Wi-Fi 与 Ethernet 切换后介质能自动变化；
- NetworkManager 重启或 backend 加载失败时应用继续运行并显示“网络状态未知”；
- rootfs / 安装目录中包含 Qt networkinformation 的 NetworkManager plugin 及其运行依赖。

以下状态仍需要由设备层采集后调用 SystemService：

- 电量：实际电池管理芯片、MCU telemetry 或 sysfs；
- 天气：明确数据来源、刷新周期、离线策略与定位来源。

采集必须异步或低频，不能在 UI 线程做阻塞 shell / socket 调用。

#### 10.3 音量与背光 Adapter

把 `SettingsService::settingApplyRequested` 连接到设备 Adapter：

- `volume`：映射到实际 ALSA mixer 控件；
- `brightness`：映射到背光设备接口；
- 设备失败时记录错误并向 UI 提供“保存成功但设备应用失败”的明确反馈；
- 启动时把已保存值重新应用到设备。

#### 10.4 提醒投递表现

当前提醒到时会持久化投递事件、发出 `reminderTriggered`、切换到提醒页并提示。上机还需确定并实现：

- 提醒音；
- 是否播报 TTS；
- 是否触发实体宠物动作；
- 静音、夜间和紧急状态下的优先级；
- 用户不处理时是否重复提醒、重复间隔和最大次数。

### P1：对应设备能力接通时完成

#### 10.5 活动与互动数据

设备 / 感知模块应调用 CareService，而不是直接写 SQLite：

```cpp
recordActivityMinutes(minutes, source)
recordInteraction(count, source)
```

需要在真实数据源上确认去重和统计口径，例如：

- 活动分钟是按时间片累计还是按一次事件提交；
- 摄像头连续检测到人是否只算一次互动；
- 同一来源重启后是否重复上报；
- 每日统计按本地时区何时归零。

#### 10.6 RTC、时区和系统时间跳变

提醒调度器每 60 秒重新检查一次，普通时间推进没有问题。仍应上机验证：

- 开机时 RTC 不正确、联网后校时；
- 时区改变；
- 时间向前跳过提醒；
- 时间向后跳时不重复投递；
- 休眠恢复后的补偿策略。

#### 10.7 长期运行

建议至少做 24～72 小时运行验证：

- UI 响应和内存；
- SQLite 连接稳定性；
- 每日跨天状态；
- 多条提醒调度；
- 日志增长；
- 进程异常退出后的重启和数据保留。

## 11. 需要你敲定的产品决策

以下事项无法仅靠架构推导，建议在进入下一版本前确认：

1. **饮水目标**：当前为固定 8 杯。是否允许用户或家属修改？“杯”的实际容量是否需要定义？
2. **提醒未处理策略**：只提醒一次，还是每隔若干分钟重复？最多重复几次？
3. **错过定义**：当前计划时间已过且当天从未成功投递时显示“已错过”；已经投递但尚未处理仍显示“待完成”。是否还需要宽限时间或补投策略？
4. **单次提醒生命周期**：触发后保留为历史、自动停用，还是次日归档？当前会保留并显示状态。
5. **完成规则**：用户能否在计划时间之前提前完成？当前允许手动完成启用中的提醒。
6. **亮度下限**：当前允许 0。设备是否需要安全最低亮度，避免把屏幕调到完全不可见？
7. **设置应用失败语义**：数据库保存成功但硬件失败时，是回滚值，还是保留“期望值”等设备恢复后重试？建议后者，但需产品确认。
8. **状态栏信息密度**：当前有设备输入时显示天气、网络、电量；无输入时显示“状态待设备接口接入”。正式出厂版本是否隐藏未知项？
9. **家属配对入口**：V0.2 保留正式入口但只提示尚未开放。是否希望在 FamilyLink 实现前隐藏按钮？
10. **宠物风格**：目前两种枚举；是否需要在后续扩展名称、表情参数和实体动作参数？
11. **控制页超时**：当前所有正式业务页 15 秒无操作返回陪伴页。编辑提醒时是否应该使用更长时间，例如 60 秒？

## 12. 当前明确未实现的能力

以下能力属于后续架构阶段，当前仅保留页面、模型或接入边界：

- 麦克风采集、KWS、完整 ASR、TTS；
- 外部 AI Server 连接和会话；
- CameraCapture、人体 / 手势感知；
- 自动跟随、MotionService、UART / MCU 控制；
- 家属真实配对、鉴权和同步；
- Emergency 业务闭环；
- Sleep 业务闭环；
- 远端天气来源；
- 设备日志轮转与运行指标页面。

这些功能没有在当前正式页面中被假装成“已接通”。

## 13. 已知限制与后续建议

### 13.1 V0.2 可接受限制

- SQLite 仍在 UI 主线程同步执行。当前操作低频、SQL 很小，符合架构 V0.2 建议；后续实测出现卡顿时再引入单一串行 DB worker。
- ReminderService 使用单一 QTimer，适合当前数量级；提醒数量显著增加后再评估查询和调度索引。
- 设置保存是“期望状态”，缺少硬件 apply result 模型。
- SystemService 保持状态聚合入口；网络 Adapter 已接入，电池和天气 Adapter 尚未实现。
- Care 的饮水上限和目标当前都是 8。

### 13.2 推荐下一步顺序

1. 先完成 SQLite、RTC、电量、音量、背光的上机适配，并验证现有网络 Adapter；
2. 完成提醒声音 / 播报和重复策略；
3. 做跨天、休眠恢复、断电和长期运行验证；
4. 根据架构路线进入 V0.3 语音闭环；
5. 运动能力必须在 MCU watchdog、通信失联停车和 Emergency 抢占验证后再接自动跟随；
6. 家属与外部 AI 接入前先确定身份、配对、协议版本和消息大小限制。

## 14. 主要文件变化

### 新增

```text
src/app/Application.*
src/app/AppController.*
src/model/ReminderModels.h
src/model/SettingsModels.h
src/model/SystemModels.h
src/platform/NetworkStatusAdapter.*
src/data/DatabaseManager.*
src/data/ReminderRepository.*
src/data/CareEventRepository.*
src/data/SettingsRepository.*
src/services/ReminderService.*
src/services/CareService.*
src/services/SettingsService.*
src/services/SystemService.*
tests/V02Test.cpp
docs/LongPet-V0.2-Work-Report.md
```

### 重构

```text
CMakeLists.txt
README.md
src/main.cpp
src/mainwindow.*
src/pages/HomePage.*
src/pages/CarePage.*
src/pages/ReminderPage.*
src/pages/ReminderEditPage.*
src/pages/SettingsPage.*
src/widgets/VisualComponents.*
resources/styles/app.qss
```

### 清理

- 删除已经失效且与当前接口不兼容的 `tests/V01UiTest.cpp`；
- 由 `tests/V02Test.cpp` 完整接管正式测试；
- 调整 `.gitignore`，允许本次 V0.2 Markdown 工作报告进入仓库，继续忽略其他 docs 构建产物。

## 15. 最终验收结论

V0.2 的目标已经从“页面能显示”推进到“本地业务真实闭环”：

- 架构层次已经落地；
- 数据可持久化；
- 页面和业务信号可测试；
- 状态栏、设置、关怀和提醒页面可正常显示；
- 未接入能力有明确边界和诚实状态；
- 后续设备 Adapter 可以通过 Service 接口接入，无需让页面依赖硬件细节。

工程可以进入上机接口接入和 V0.2 长稳验证阶段。
