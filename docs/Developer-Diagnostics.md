# LongPet Developer / Diagnostics 使用指南

## 1. 用途与边界

DeveloperPage 用于区分“传感器/worker 没有产生事件”和“业务层收到事件但因页面优先级忽略”两类问题。它不是老人日常设置页，也不是通用系统管理器。

页面只能通过 `DeveloperService` 查询或操作 KWS、Vision、设备枚举和安全 Simulation。它不能直接访问 `QProcess`、ALSA handle、OpenCV、SQLite、Shell、GPIO、UART 或任意文件。

## 2. 如何进入

默认不显示入口。启动 LongPet 前设置：

```sh
export LONGPET_DEVELOPER_MODE=1
```

systemd 调试板可安装 `deploy/longpet-developer.conf` 为：

```text
/etc/systemd/system/longpet.service.d/10-developer.conf
```

然后执行 `systemctl daemon-reload && systemctl restart longpet.service`。进入路径是“设置 → 关于设备 → 诊断”。普通模式不安装 drop-in，或将变量设为 `0`。

## 3. 状态字段

每个 worker 模块都分别显示：

- `Enabled`：配置上是否允许启动；
- `Available`：运行时依赖和输入当前是否可用；
- `Running`：worker 进程是否存在；
- `State`：Disabled、Starting、Listening/Running、Degraded 或 Error；
- `PID`：当前 worker 主进程 PID；
- `Uptime`：本次 worker 启动时长；
- `Error/Detail`：当前降级或错误原因。

因此 `Enabled=Yes, Running=No` 并不代表状态矛盾：它可能正等待自动重试，或因不可恢复配置错误等待人工 Restart。

总览还显示 Audio input、Camera、SQLite schema、Network、Power。Remote AI 与 Motion MCU 只显示 `Not implemented`，不伪造 Running。

## 4. KWS 排障

推荐从下往上检查：

1. `Audio inputs` 是否能列出目标 ALSA PCM；
2. 当前 Input Device 是否正确；
3. `RMS`/`Peak` 在说话时是否变化。两者持续为 0，先查麦克风、ALSA device、channel 和接线，不要先调模型；
4. State 是否到 `Listening`，PID/Uptime 是否稳定；
5. 说关键词后检查 Last keyword、Decode、RTF、Keyword latency 和 Dropped；
6. Event 页应依次看到 Adapter keyword、Service semantic、Controller accepted/ignored；
7. 若 Controller 显示 `ReminderAlert active` 或 `Emergency active`，说明感知链正常，事件被业务优先级有意抑制。

RMS/Peak 由采集子进程每 200 ms 只发送两个统计值，不向 UI 传 PCM。设备、CPU/RAM 与完整快照为 1 秒刷新；事件日志为事件驱动。

## 5. Vision 排障

1. Devices 页确认 `/dev/video*` 存在；
2. Vision 页检查 `CameraAvailable`、PID、State；
3. `Actual FPS` 应接近目标值，`Frame time` 是算法处理均值而非摄像头曝光时间；
4. 当前正式能力是 `Wave recognition`；
5. `Fall candidate (experimental)` 默认关闭，只是候选预筛，不能当安全级跌倒确认；
6. 挥手后检查 Last raw event、confidence，再看 Event 页的 Service accepted 与 Controller accepted/ignored。

页面不显示“人体识别 Running”，因为当前 worker 没有正式 Person Detector。

## 6. 切换设备与参数

KWS 可设置 ALSA input、sample rate、channels、microphone channel、threshold 和 score。Vision 可设置 camera index、width、height、target FPS、Wave 和实验性 Fall candidate。

点击“应用并重启”后的真实链路是：

```text
DeveloperPage
→ AppController
→ DeveloperService
→ KWS/Vision Service
→ Adapter reconfigure
→ 异步停止旧 worker
→ 以新 CLI 参数启动 worker
```

无效设备在 `DeveloperService` 层拒绝，不会只更新下拉框，也不会启动一个注定失败的 worker。

## 7. Start / Stop / Restart 与自动恢复

- Enable/Disable 修改模块允许状态；
- Start 只在 Enabled 时启动；
- Stop 停止当前 worker，但保留配置；
- Restart 清除本轮失败计数并按当前配置重启；
- Reconfigure 在参数校验后异步停止并重启。

意外 crash、摄像头临时失效和非正常退出采用 5 秒、30 秒、120 秒三次有限重试。连续失败用尽后保持 Degraded/Error，等待手动 Restart。稳定运行 5 分钟后失败计数才重置。

模型缺失、Python/module 缺失、可执行文件无效和参数无效属于不可重复重试错误。启动超时采用 `terminate → 2.5s kill fallback`，UI 线程不执行长 `waitForFinished()`。

Linux worker 使用独立进程组。KWS main、multiprocessing capture、resource tracker 和 `arecord` 同组；LongPet Stop/Restart 或检测到主进程退出时会清理整组。

## 8. Simulation

总览提供五个安全按钮：

- 模拟“你好”：KWS Service semantic → AppController → Home；
- 模拟“知道了”：只在当前 ReminderAlert 存在时确认；
- 模拟挥手：Vision Service validation/cooldown → AppController；
- 模拟提醒：ReminderService 发出内存 presentation → AppController → ReminderAlert；
- 模拟紧急：KWS Service emergency semantic → AppController → Emergency。

模拟提醒使用负 event ID 和 `diagnostic=true`，确认时不写 Repository/SQLite。Simulation 不直接调用 `MainWindow::showHome()`，也不执行真实呼叫、网络同步或危险硬件动作。

## 9. Event Log

`DiagnosticsService` 按时间顺序保留最近 200 条。达到上限时删除最旧事件，不会无限增长。来源包括 AUDIO、KWS-ADAPTER、KWS-SERVICE、VISION-ADAPTER、VISION-SERVICE、REMINDER、CONTROLLER、DATABASE、SYSTEM 和 UI。

典型链路：

```text
00:42:24.100 [VISION-SERVICE/INFO] Wave confidence=0.78
00:42:24.101 [CONTROLLER/WARN] Wave ignored ReminderAlert active
```

它说明 Camera/worker/Adapter/Service 已经成功，未切页是业务优先级结果。日志不会记录每个 PCM chunk、每帧图像或每次 heartbeat。

## 10. 板端常见故障

### RMS/Peak 始终为 0

检查输入 device、channels 与 microphone channel；确认没有另一个进程独占同一 ALSA PCM；再检查 ES8388 mixer 和接线。

### Vision 显示 Camera unavailable

检查 Devices 页是否存在 `/dev/video0`，确认不是 metadata node 或被其他进程占用。选择新的 camera 后“应用并重启”。

### worker 反复 Degraded

在 Event 页确认是 recoverable crash 还是 missing module/model。三次重试用尽后先修复依赖，再点 Restart，避免无限循环消耗单核 CPU。

### 单核板组合负载过高

先保持 Vision 5 FPS、fall candidate 关闭，并提高 Vision nice 值；不要在 V0.2.1 引入复杂 InferenceScheduler。观察 KWS RTF、Dropped、Vision Actual FPS、各进程 CPU/RSS 和触摸响应。

## 11. 板端验收清单

Audio/KWS：确认设备列表、RMS/Peak、切设备后新 CLI 参数、50～100 次真实关键词、Last result、RTF/latency/dropped 和完整 Event 链。

Vision：确认 CameraAvailable、Actual FPS、Frame time、挥手 confidence、Controller 决策；实验性 Fall candidate 只做观察，不作为安全报警验收。

Worker：分别 `kill -9` KWS/Vision 主 PID，确认旧 PID/PGID、capture 和 `arecord` 无残留，5 秒后出现新 PID，LongPet 自身 `NRestarts` 不增加；重复制造失败验证 30 秒、120 秒和最终停止自动恢复。

## 12. 后续音频架构约束

V0.2.1 KWS 仍独占 ALSA。Remote ASR/TTS 接入前必须新增统一 `AudioService`：

```text
one audio capture
├── KWS
└── Remote ASR
```

禁止 KWS 与 Remote ASR 分别打开两个 `arecord`。
