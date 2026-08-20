# LongPet V0.2 提醒功能架构重构工作报告

- 完成日期：2026-08-20
- 当前实现基线：`Justin-fanfan/longpet` `main`（检查时提交 `6ca16d5`）
- 架构参考：`Justin-fanfan/longpetui_2` `agent/reminder-alert-architecture` / PR #1（检查时提交 `4a24519`）
- 结论：提醒管理、到期投递、确认/完成语义、重复投递、SQLite V2 migration、页面抢占与恢复均已落地；Windows Release/CTest 与开发板实际运行验证通过。

## 1. 架构阅读与实现边界

本次实现前重新读取了当前 `longpet/main` 的实际工程，包括近期加入的 Network、ALSA 音量、Backlight、Power Adapter；同时完整读取了架构分支中的以下文档：

- `01-device/model/ReminderModels.md`
- `01-device/services/ReminderService.md`
- `01-device/ui/pages/ReminderPage.md`
- `01-device/ui/pages/ReminderAlertPage.md`
- `01-device/app/AppController.md`
- `03-flows/ReminderCareFlow.md`

实现遵循现有分层，没有让 Page、Widget 或 MainWindow 访问 SQLite、NetworkManager、音频硬件等底层能力。

当前主要调用关系如下：

```text
Application
  -> AppController
      -> ReminderService
          -> ReminderRepository
              -> SQLite

ReminderService -- reminderPresentationRequested --> AppController
AppController    -- 当前提醒/排队/抢占/恢复 ----------> MainWindow
MainWindow       -- 触摸语义事件 --------------------> AppController
AppController    -- Acknowledge / Complete ----------> ReminderService
```

`ReminderPage` 继续是提醒管理页；新增的 `ReminderAlertPage` 是提醒 occurrence 到期后的独立投递页，两者没有混用。

## 2. 数据模型改动

### 2.1 Reminder

在保留现有 `title` 作为显示文本语义的前提下，新增：

| 字段 | 作用 |
| --- | --- |
| `uuid` | 跨端稳定标识；本地新建时生成标准 UUID，编辑时不可变 |
| `iconKey` | 受控图标键，不存本地绝对路径 |
| `voiceType` | `None` / `Tts` / `AudioAsset` |
| `voiceText` | TTS 或录音失败后的文本能力输入 |
| `voiceAssetId` | 家属录音的安全、不透明资源 ID |
| `repeatIntervalMinutes` | 未确认后的重复提醒间隔 |
| `maxPresentationCount` | 单个 occurrence 的最大展示次数 |

默认策略集中定义在 `ReminderDefaults`：

- 页面展示窗口：30 秒；
- 未确认重复间隔：5 分钟；
- 最大展示次数：3 次；
- 调度器最大复查间隔：60 秒。

这些参数没有散落在 UI 代码中。Repository 边界会把可选的空录音 ID 写为非 NULL 空字符串，避免不同 Qt/SQLite 平台对 null `QString` 的差异。

### 2.2 Reminder occurrence / event

新增正式的 `ReminderOccurrence`，包含：

- `Pending`
- `Presented`
- `Acknowledged`
- `Completed`
- `Missed`
- `presentationCount`
- `lastPresentedAt`
- `acknowledgedAt`
- `completedAt`
- `ackSource`（`None` / `Touch` / `Voice` / `Family`）

`Disabled` 仍保留为 Reminder 管理页的展示态，但不会作为正常 occurrence 生命周期写入。

## 3. 状态机与调度行为

```text
到达计划时间
  -> 创建 Pending occurrence
  -> Presented，presentationCount += 1
  -> AppController 主动显示 ReminderAlertPage

Presented
  -> 用户“知道了”      -> Acknowledged
  -> 用户“已经完成”    -> Completed
  -> 30 秒未操作        -> 页面退出，但 occurrence 保持 Presented
  -> 到重复间隔且未确认 -> 再次 Presented，计数加一
  -> 达到最大次数后再到重复检查点 -> Missed
```

重要语义：

- `Acknowledged` 仅代表老人看到/听到提醒，不计为完成；
- `Completed` 才进入 CareService 的用药完成统计；
- Acknowledged 后不再重复展示；
- 管理页仍可把已知晓但后来实际完成的事项标为 Completed；
- Service 重启后会读取持久化 occurrence，不会因“当天已有 event”错误地永远停止未确认提醒。

## 4. AppController 抢占、排队和恢复

AppController 现在负责：

1. 接收正式 `ReminderPresentation`；
2. 保存提醒前页面；
3. 抢占 Companion、Home、Care、Reminder、ReminderEdit、Settings 等当前页面；
4. 同时到期多条提醒时按 occurrence 排队，每次只展示一条；
5. 每条确认或完成后展示下一条；
6. 队列清空后恢复提醒前页面；
7. 30 秒未操作时退出当前提醒页，等待 Service 后续重投；
8. 提醒显示期间暂停普通页面的 15 秒控制超时。

到期流程已经删除旧的 `showPage(Reminder) + Toast` 行为。管理页不会再承担提醒投递。

## 5. ReminderAlertPage

新增 1024×600 适老提醒页：

- 单条提醒模式；
- 112 px 大图标；
- 48 px 大字号提醒文本；
- 96 px 高主确认按钮“知道了”；
- 独立次操作“已经完成”；
- 显示计划时间和当前第几次展示；
- 明示未确认后会再次提醒；
- 无列表、无数据库访问、无音频硬件访问。

图标通过 `iconKey` 解析为 QRC 内置资源。当前支持 `medicine/pill`、`water`、`activity`、`care`、`reminder`；未知键统一回退到 `:/icons/reminder.svg`。

ReminderPage 同步改为使用 `iconKey`，并能显示“提醒中”“已知晓”“已完成”“已错过”“已停用”等状态。ReminderEditPage 会保留未来家属端写入的 UUID、图标、语音和重复策略；当本地图形类型变更且原图标只是旧类型默认值时，才自动更新默认图标，不覆盖自定义图标。

## 6. SQLite Schema V1 -> V2 migration

`DatabaseManager` 当前 schema version 为 2。升级通过事务执行，不删库、不重建已有表、不删除旧提醒或旧事件。

### 6.1 reminders 新列

```text
uuid
icon_key
voice_type
voice_text
voice_asset_id
repeat_interval_minutes
max_repeat_count
```

迁移旧数据时：

- 为每条旧 Reminder 生成稳定 UUID；
- 根据旧 `type` 补 `medicine` / `water` / `reminder` 图标键；
- `voice_type` 设为 `none`；
- `voice_text` 使用原 `title`；
- 重复策略补为 5 分钟、最多 3 次；
- 保留原 id、title、revision、时间、重复规则、enabled 和时间戳；
- 创建 UUID 唯一索引。

### 6.2 reminder_events 新列

```text
presentation_count
last_presented_at
acknowledged_at
ack_source
```

原有 `completed_at` 和原状态保留；旧 Completed event 迁移后仍是 Completed，展示计数默认为 0。

新数据库也严格走 `V0 -> V1 -> V2` 的同一迁移链，避免维护两份不同建表定义。

## 7. Family / KWS / 提醒语音接口现状

### 7.1 已真实实现

- Family 未来需要的 Reminder 字段已进入终端模型、Repository、Service 和 SQLite；
- `ReminderService::save(ReminderDraft)` 已能保存上述高级配置；
- UUID 编辑时保持稳定，外部传入非空 UUID 必须是合法 UUID；
- `voiceAssetId` 只接受安全资源标识，不接受本地绝对路径或 `file:` URL；
- AppController 提供上下文相关的 `handleReminderConfirmation(...)`；
- 当前没有 ReminderAlert 时，语音语义调用会返回 false，不会误确认普通聊天状态；
- `Acknowledge` 与 `Complete` 服务接口和持久化语义已经分开；
- AppController 在每次真正进入提醒投递流程时发出 `reminderVoicePlaybackRequested(Reminder)` 能力请求信号。

### 7.2 仅预留、未伪造完成

- 没有实现 Family App 网络同步或伪造 FamilyLinkService；
- 没有新增假的 KWS/ASR；
- 没有声称录音、TTS、提示音已播放成功；
- 录音 asset 查找、校验、缓存与播放尚未实现；
- `录音 -> voiceText TTS -> 提示音` 的降级链需要未来真实 Audio/TTS Service 接收能力请求后实现。

未来推荐接入方向保持为：

```text
Family App -> FamilyLinkService -> ReminderService -> Repository -> SQLite
KWS semantic event -> AppController::handleReminderConfirmation(...)
Reminder voice request -> Audio/TTS Service -> 真实播放结果与降级
```

## 8. 修改文件清单

### 构建与样式

- `CMakeLists.txt`
- `resources/styles/app.qss`

### Model

- `src/model/ReminderModels.h`
- `src/model/ReminderModels.cpp`（新增）

### Data

- `src/data/DatabaseManager.h`
- `src/data/DatabaseManager.cpp`
- `src/data/ReminderRepository.h`
- `src/data/ReminderRepository.cpp`

### Service / Controller

- `src/services/ReminderService.h`
- `src/services/ReminderService.cpp`
- `src/app/AppController.h`
- `src/app/AppController.cpp`

### UI

- `src/pages/ReminderAlertPage.h`（新增）
- `src/pages/ReminderAlertPage.cpp`（新增）
- `src/pages/ReminderPage.cpp`
- `src/pages/ReminderEditPage.h`
- `src/pages/ReminderEditPage.cpp`
- `src/mainwindow.h`
- `src/mainwindow.cpp`
- `src/widgets/VisualComponents.h`
- `src/widgets/VisualComponents.cpp`

### 测试与报告

- `tests/V02Test.cpp`
- `docs/LongPet-V0.2-Reminder-Refactor-Report.md`（本报告）

## 9. 构建与测试结果

### 9.1 Windows Release

- 工具链：MSVC 19.44、Qt 6.11.2、Ninja；
- 构建目录：`build-win-reminder-ninja`；
- `LongPet.exe`：编译、链接成功；
- `LongPetV02Tests.exe`：编译、链接成功；
- `/W4`：没有本次修改产生的 warning/error。

### 9.2 CTest

```text
1/1 LongPet.V02 Passed
100% tests passed, 0 tests failed
```

常规 CTest 内部结果：18 passed、0 failed、1 optional capture skipped。

启用 `LONGPET_TEST_CAPTURE_DIR` 后的完整渲染测试：

```text
19 passed, 0 failed, 0 skipped
ReminderAlertPage capture: 1024 x 600
```

覆盖项包括：

- V1 -> V2 migration；
- 旧 Reminder 与 Completed event 保留；
- 到点进入 ReminderAlertPage；
- 到期不进入 ReminderPage；
- “知道了”产生 Acknowledged 而不是 Completed；
- Acknowledged 后停止重复；
- 未确认可再次 presentation；
- 最大展示次数；
- 同时到期多条排队；
- 队列结束恢复原页面；
- 非提醒上下文拒绝语音确认；
- 无效 iconKey fallback；
- 1024×600 ReminderAlertPage 实际渲染。

## 10. 开发板验证结果

测试连接：`192.168.137.45`，工作目录 `/root/mytest/qt`。

为避免影响正式程序，本次只部署了独立测试文件：

```text
/root/mytest/qt/LongPet.reminder-v02
```

没有替换现有 `/root/mytest/qt/LongPet`，没有修改 `longpet.service`，没有操作正式 `data/longpet.db`。

实际结果：

- LoongArch Release ELF 启动稳定；
- Qt 6.5 offscreen 多次启动均持续运行至测试 timeout，无崩溃；
- NetworkInformation 成功加载 `networkmanager` backend；
- ES8388 ALSA 音量 Adapter 成功加载；
- 隔离 V1 数据库成功升级到 version 2；
- 旧提醒 title、revision=4、旧 Completed event 均保留；
- 自动补齐 UUID、`icon_key=medicine`、语音字段、5 分钟/3 次策略；
- 两条同分钟到期提醒分别产生独立 Presented occurrence；
- 两条 reminder event 均按 1 -> 2 -> 3 递增 presentationCount；
- 超过最大展示次数后均转为 Missed，计数保持 3。

板端隔离验证数据：

```text
/root/mytest/qt/data/reminder-v1-migration-20260820.db
/root/mytest/qt/data/reminder-state-machine-1787179157.db
```

## 11. 仍需产品/后续工程确认

1. **默认策略是否最终定版**：当前为展示 30 秒、5 分钟后重投、最多展示 3 次；均已集中定义，可统一调整。
2. **“已经完成”按钮适用范围**：当前所有提醒类型都显示。若产品希望只有吃药/任务类显示，可在模型增加 completion policy，而不应按 UI 文案硬编码。
3. **Family 同步协议**：需确定 UUID 归属、revision 冲突策略、删除 tombstone、时区与录音 asset 生命周期。
4. **voiceAssetId 格式**：当前采用安全不透明 ID（字母、数字、点、下划线、横线），未来协议如需 namespaced ID，应先统一格式再放宽校验。
5. **KWS 词表与冲突消解**：建议将“知道了/好的”映射 Acknowledged，“完成了/吃过了”映射 Completed，并仅在 AppController 当前有 ReminderAlert context 时转发。
6. **真实音频能力**：需实现 Audio/TTS Service、录音 asset 校验与缓存、播放结果回传和完整降级链；当前仅有模型与请求入口。
7. **正式部署前备份**：migration 已有事务保护，仍建议升级正式设备前备份 `data/longpet.db`，并在一台含真实历史数据的设备上再做一次升级验收。
8. **板端人工交互验收**：自动化已覆盖页面尺寸和按钮语义；正式替换 systemd 使用的二进制前，建议在 linuxfb + 真实触摸屏上人工确认大字可读性、30 秒停留体验、两条排队切换以及返回原页面的视觉连续性。
