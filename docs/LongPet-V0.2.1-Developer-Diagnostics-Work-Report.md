# LongPet V0.2.1 整改与 Developer / Diagnostics 工作报告

- 完成日期：2026-08-21
- 目标分支：`codex/v02-reminder-kws`
- 工程目录：`D:\code_qt\longpet`
- 软件版本：`0.2.1`
- 板端目录：`/root/mytest/qt`
- 板端：Loongson 2K0300，单核 1 GHz，369 MiB RAM，无 swap

## 1. 结论

本轮没有重写 LongPet 总体架构，也没有引入 EventBus、Plugin/DI Framework、Remote ASR/TTS、LLM、Motion 或 AutoFollow。实现继续遵守：

```text
UI
→ Application / AppController
→ Services
→ Platform / Adapter / Data
```

已完成的核心结果：

1. 修复 Emergency 期间 Reminder scheduler 静默消耗 `presentation_count` 的竞态；
2. KWS/Vision 改为异步二阶段停止、独立进程组、有限自动恢复；
3. KWS stdout 增加 256 KiB 上限；
4. KWS ALSA device/sample rate/channels/mic channel 由 UI 到 Python CLI 真实生效；
5. KWS 增加 5 Hz RMS/Peak、decode elapsed、RTF、keyword latency 和 dropped utterance 指标；
6. 新增 Audio/Camera 设备 Adapter、DiagnosticsService、DeveloperService 和五标签 DeveloperPage；
7. Simulation 经过真实 Service→AppController 业务链，不直接切 MainWindow，不污染 SQLite；
8. Windows Release、CTest、1024×600 五标签渲染、LoongArch Release 和板端 worker crash 恢复均已验证。

## 2. 实际修改文件

### 构建、部署与文档

- `CMakeLists.txt`
- `README.md`
- `scripts/build-loongarch.sh`
- `deploy/longpet-developer.conf`
- `docs/Developer-Diagnostics.md`
- `docs/LongPet-V0.2-Work-Report.md`
- `docs/LongPet-V0.2.1-Developer-Diagnostics-Work-Report.md`

### Model

- `src/model/KeywordSpottingModels.h`
- `src/model/VisionModels.h`
- `src/model/ReminderModels.h`
- `src/model/DiagnosticsModels.h`（新增）

### Platform / Adapter

- `src/platform/KeywordSpottingAdapter.h/.cpp`
- `src/platform/VisionAdapter.h/.cpp`
- `src/platform/AudioDeviceAdapter.h/.cpp`（新增）
- `src/platform/CameraDeviceAdapter.h/.cpp`（新增）

### Service

- `src/services/ReminderService.h/.cpp`
- `src/services/KeywordSpottingService.h/.cpp`
- `src/services/VisionService.h/.cpp`
- `src/services/DiagnosticsService.h/.cpp`（新增）
- `src/services/DeveloperService.h/.cpp`（新增）

### App / UI

- `src/app/Application.h/.cpp`
- `src/app/AppController.h/.cpp`
- `src/mainwindow.h/.cpp`
- `src/pages/SettingsPage.h/.cpp`
- `src/pages/DeveloperPage.h/.cpp`（新增）

### Python worker 与测试

- `third_party/loongson-kws/src/loongson_kws.py`
- `third_party/longpet-vision/src/vision_worker.py`
- `tests/V02Test.cpp`

本轮不修改 SQLite schema；数据库继续是 Schema V2，既有 V1→V2 migration 与 Reminder 数据兼容测试保持通过。

## 3. Reminder / Emergency 竞态修复

`ReminderService` 新增：

```cpp
suspendScheduling();
resumeScheduling();
isSchedulingSuspended();
```

进入 Emergency 时，AppController 同时：

- 停止 Reminder scheduler；
- 停止当前 ReminderAlert 的 30 秒展示 timeout；
- 保留当前 presentation，不修改 occurrence。

退出 Emergency：

- 若此前有 ReminderAlert，先恢复同一条 Reminder 和 timeout；
- 当前 Reminder 被确认或超时后才恢复 scheduler；
- 若此前没有 Reminder，立即恢复 scheduler。

自动测试覆盖：第一次展示计数为 1；Emergency 持续跨过三个重复间隔；`checkNow()` 不增加计数；退出后仍恢复原 Reminder；确认后 scheduler 恢复并能投递新的 due reminder。

## 4. Worker 生命周期与重试策略

KWS/Vision 统一采用：

```text
unexpected exit / temporary input failure
→ 5 秒 retry
→ 30 秒 retry
→ 120 秒 retry
→ Degraded/Error，等待手动 Restart
```

稳定运行 5 分钟后才清零连续失败次数，避免“每次刚进入 Running 就 crash”形成无限循环。手动 Restart 明确清零失败次数。

不可自动重试：

- runtime/model/worker 文件缺失；
- Python executable 不存在；
- Python/module 依赖缺失（worker 通过 `recoverable=false` 上报）；
- DeveloperService 参数或设备校验失败。

启动超时不再阻塞 UI：

```text
startup timeout
→ terminate()
→ 2.5 秒 kill fallback QTimer
→ 仍 Running 才 kill()
```

正常 Start/Stop/Restart/Reconfigure 不调用长时间 `waitForFinished()`。对象析构的最终安全网只在 kill 后最多等待 100 ms 回收 QProcess，防止 zombie/QProcess warning。

## 5. KWS 进程树与 stdout

Linux KWS/Vision worker 在 child modifier 中建立独立进程组并设置 `PDEATHSIG`。KWS Python capture 子进程和 `arecord` 也设置 parent-death signal，并继承 KWS PGID。

最终板端进程结构：

```text
LongPet PGID=37161
├── KWS main PGID=37849
│   ├── resource_tracker PGID=37849
│   ├── capture worker PGID=37849
│   └── arecord PGID=37849
└── Vision worker PGID=38271
```

KWS stdout buffer 上限为 256 KiB。单行超过上限且没有换行时清空并产生 diagnostic warning；测试用 fake worker 输出 300,000 字节无换行数据，验证缓存不会无限增长。

## 6. DeveloperPage 标签页

入口由 `LONGPET_DEVELOPER_MODE=1` 控制，普通模式隐藏。板端开发机已安装 systemd drop-in，入口在“设置 → 关于设备 → 诊断”。DeveloperPage 不受老人模式 15 秒自动返回影响。

### 总览

显示 KWS、Vision、Audio、Camera、SQLite、Network、Power，以及 Remote AI/Motion MCU 的 `Not implemented` 诚实占位。KWS/Vision 分别显示 Enabled、Available、Running、State、PID、Uptime、Error/Detail。

### 语音

- Enable/Disable、Start、Stop、Restart、应用并重启；
- ALSA input、sample rate、channels、mic channel；
- threshold、score；
- 5 Hz RMS/Peak；
- decode elapsed、RTF、keyword latency、dropped utterances；
- Last keyword、Last event 和最近 8 条 Audio/KWS event。

### 视觉

- Enable/Disable、Start、Stop、Restart、应用并重启；
- Camera、width/height、target/actual FPS、frame time；
- Wave recognition；
- Fall candidate（experimental，默认关闭）；
- Last raw event、confidence、timestamp、Error。

页面不显示虚假的 “Person detector Running”。

### 设备

显示 Audio input、`/dev/video*`、SQLite 路径/schema。

### 事件

事件驱动显示最近 200 条 DiagnosticEvent。

## 7. Enable / Disable / Restart / Reconfigure

DeveloperPage 只发语义信号；AppController 将请求交给 DeveloperService；DeveloperService 调用 KWS/Vision Service；Service 再控制 Adapter。页面不持有 Adapter/QProcess。

所有 reconfigure 都停止旧 worker，再以新 Options 启动。控件不会每秒被 snapshot 回写覆盖，避免开发者输入一半时参数跳回旧值。

## 8. Audio 输入设备枚举和切换

`AudioDeviceAdapter` 使用 ALSA `snd_device_name_hint(..., "pcm", ...)` 枚举输入 PCM，过滤 `IOID=Output` 并去重，返回：

```cpp
struct AudioInputDevice {
    QString id;
    QString displayName;
    bool available;
};
```

Windows/普通 CI 未编译 ALSA 时返回空列表并正常降级。DeveloperService 在应用参数前验证所选 ID；无效 ID 不会启动 worker。

板端最终 KWS CLI 已确认：

```text
--alsa-device hw:0,0 --sample-rate 44100 --channels 2 --mic-channel 0
```

## 9. Camera 枚举和切换

`CameraDeviceAdapter` 扫描 `/dev/video*`，解析 index，并返回 `CameraDevice`。DeveloperService 拒绝不存在的 index；合法重配置按 stop→apply Options→start 执行。

板端最终 Vision CLI 已确认：

```text
--camera 0 --width 320 --height 240 --fps 5 --wave-enabled
```

## 10. RMS/Peak 与性能指标

采集子进程每 200 ms 计算当前选定 microphone channel 的 RMS/Peak，只把标量发给 C++。DeveloperService 使用独立 `audioLevelChanged` 信号更新两个 progress bar，不触发设备重扫或整页重建。

每次 utterance 解码后上报：

- decode elapsed ms；
- RTF；
- keyword latency（从 VAD speech start 到命中结果）；
- capture-to-decode 排队时间；
- dropped utterance count。

## 11. DiagnosticEvent 传播与容量

`DiagnosticsService` 固定容量为 200 条，时间顺序保存；第 201 条到来时删除最旧一条。不会记录 PCM chunk、逐帧视觉或每次 heartbeat。

已接入的有价值事件包括：

- Adapter warning、worker retry；
- KWS keyword 与 Service semantic；
- Vision accepted/suppressed；
- Reminder presentation requested；
- Controller accepted/ignored 及原因；
- SQLite 启动状态、Network 状态变化。

示例：

```text
VISION-SERVICE  Wave confidence=0.78
CONTROLLER      Wave ignored: ReminderAlert active
```

## 12. Simulation 真实链路

- Greeting/Acknowledge/Emergency：DeveloperService → KeywordSpottingService semantic → AppController；
- Wave：DeveloperService → VisionService validation/cooldown → AppController；
- Reminder Due：DeveloperService → ReminderService memory presentation → AppController。

模拟 Reminder 使用负 event ID 和 `diagnostic=true`，确认时只完成 UI 生命周期，不访问 Repository。因此不会制造正式 occurrence 或污染关怀统计。

## 13. 自动化测试结果

Windows 环境：MSVC 19.44、Qt 6.11.2、Release、`LONGPET_BUILD_TESTS=ON`。

- Release 构建：通过，无本轮 C++ warning/error；
- CTest：`1/1 LongPet.V02 Passed`；
- Qt Test 明细：25 passed、0 failed、1 skipped；skip 仅为未设置截图目录；
- 单独设置截图目录后：全部正式页面及 Developer 五标签 1024×600 渲染通过；
- Python `py_compile`：KWS/Vision worker 均通过。

新增/强化覆盖：

- Emergency 不消耗 Reminder repeat presentation；
- startup timeout、kill fallback、unexpected exit、三次 retry limit；
- KWS 256 KiB stdout bound；
- Diagnostics ring buffer 上限和顺序；
- DeveloperService 设备校验、reconfigure、disable/restart failure；
- Wave/KWS/Reminder/Emergency Simulation 到达 AppController；
- Developer 入口普通模式隐藏、开发模式显示；
- Developer 五标签 1024×600 渲染。

fake worker 和 fake device 测试不要求 CI 拥有摄像头、ALSA 或 NetworkManager。

## 14. LoongArch Release 构建结果

使用 `Ubuntu-24.04` WSL 和整改后的 `scripts/build-loongarch.sh`：

- `PROJECT_DIR` 从脚本自身目录推导；
- `BUILD_DIR` 可由环境变量覆盖；
- 并行使用 `LONGPET_BUILD_JOBS=$(nproc)`；
- Release 构建通过，无本轮 GCC warning/error。

产物：

```text
ELF 64-bit LSB executable, LoongArch
interpreter /lib64/ld-linux-loongarch-lp64d.so.1
size 936,024 bytes
SHA-256 8fe71e1e5f6a6c7233d58e8b00786f9bdc20e29a1eefdf3f27a3a26091134eed
```

## 15. 开发板实测结果

最终部署哈希：

```text
LongPet          8fe71e1e5f6a6c7233d58e8b00786f9bdc20e29a1eefdf3f27a3a26091134eed
loongson_kws.py  7f8da21092f6b01aba053fd392a1231b94af34d6a77a465097033a62ddbdfd9e
vision_worker.py 5bc680edb6e0db9bd77396b3f79792af5448cbe2aa75ef8c9864371632d97695
```

最终新启动稳定状态（启动约 70 秒，`ps` 为进程生命周期平均 CPU，不是瞬时采样）：

| 项目 | 结果 |
|---|---:|
| longpet.service | active |
| NRestarts | 0 |
| service MemoryCurrent | 170,639,360 B（约 162.7 MiB） |
| service MemoryPeak | 171,704,320 B（约 163.8 MiB） |
| MemAvailable | 132,672 KiB（约 129.6 MiB） |
| Swap | 0 |
| LongPet RSS / CPU | 61,424 KiB / 6.6% |
| KWS main RSS / CPU | 83,696 KiB / 41.5% |
| KWS capture RSS / CPU | 34,032 KiB / 9.1% |
| Vision RSS / CPU | 55,968 KiB / 22.9% |
| arecord RSS / CPU | 2,704 KiB / 0.2% |

进程树与 5 FPS Vision、KWS 同时常驻成功。冷启动观测窗口内各进程 CPU 合计约 80%，因此保持 Vision 5 FPS、fall candidate 关闭是正确默认值。

最终哈希版本 KWS `kill -9 PID 37192`：2 秒时旧 PID/PGID、capture、resource tracker、arecord 均不存在；8 秒时新 KWS PID `37849` 出现；LongPet MainPID `37161` 不变、NRestarts=0。

最终哈希版本 Vision `kill -9 PID 37356`：2 秒时旧 PID/PGID 不存在；8 秒时新 Vision PID `38271` 出现；LongPet MainPID `37161` 不变、NRestarts=0。

两次故障恢复后的复查：service `MemoryCurrent=180,486,144 B`（约 172.1 MiB）、`MemoryPeak=181,567,488 B`（约 173.2 MiB），`MemAvailable=134,944 KiB`（约 131.8 MiB），仍无旧 worker 残留。后续长稳测试仍应关注多次 Restart 后内存是否缓慢增长。

最终版正常 `systemctl stop longpet.service` 用时约 1 秒，未再出现旧实现中 UI 线程长 wait 的停止延迟。

### 性能口径说明

此前“约 2.5 ms/处理帧”是 Windows 上对纯 OpenCV 运动候选原型的离线测试，不是 2K0300 板端实测。之前板端 SSH 中断是临时 synthetic benchmark 错误预分配约 922 MB 帧列表造成 OOM。本轮所有板端检查均采用现有流式 worker 和常量内存命令，没有再次预分配帧列表，也没有把 Windows 数字写成板端性能。

## 16. 仍需现场人工完成

以下能力必须有人在屏幕、麦克风和摄像头前操作；本报告没有用 Simulation 冒充真实验收：

1. 设置 → 关于设备 → 诊断入口的触摸可达性；
2. KWS 页 RMS/Peak 随真实说话变化；
3. 从设备下拉框切换一个真实可用 ALSA input，确认新 CLI 与 Restart；
4. 连续真实说关键词 50～100 次，逐次记录命中/漏检、decode elapsed、RTF、keyword latency、dropped；
5. KWS+Vision 同时运行时的触摸响应主观验收；
6. 真实挥手至少 30 次，记录 confidence、漏检、误触发和 Controller 决策；
7. DeveloperPage 显示的 Actual FPS 与 Frame time；
8. 依次制造第二、第三、第四次连续 worker failure，现场等待验证 30 秒、120 秒和最终停止重试；
9. Camera 切换后的实际取帧；
10. Fall candidate 若开启，只做实验观察，禁止将其当作已完成的安全级跌倒报警。

## 17. 后续必须注意

### V0.3 AudioService

Remote ASR/TTS 接入前必须先做统一 AudioService：一次 capture 同时供 KWS 与 Remote ASR。禁止两个独立 `arecord` 抢占同一 ES8388。

### Vision / AutoFollow 边界

现有 MOG2/Wave worker 是 fixed-camera 交互模块。AutoFollow 必须另建 PersonDetector/Tracker、PersonObservation、AutoFollowController、MotionService 和 MCU 链路，本轮代码不能直接演变成电机控制器。

### 内存与组合负载

目标板只有 369 MiB 且无 swap。不得在板上预生成/缓存数千帧；测试必须流式、固定 buffer。新增模型前先测组合 RSS 和单核 CPU，不能只报告单 worker benchmark。

### 开发模式

开发板当前安装了 `LONGPET_DEVELOPER_MODE=1` drop-in。交付老人使用前应删除 `/etc/systemd/system/longpet.service.d/10-developer.conf`，daemon-reload 并重启，以隐藏诊断入口。
