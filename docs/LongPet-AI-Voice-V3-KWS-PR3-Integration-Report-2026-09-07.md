# LongPet V3：PR #3 审查、接入与验收报告

> 历史记录：本文描述 2026-09-07 在语音分支进行的工作和测试，不代表当前分支重复完成了这些硬件/模型检查。
> 2026-09-11 已从 `kws@208e1f7` 迁入 `vision-v1.1`，当前迁移范围、审计及验证结果见
> [KWS → Vision 迁移报告](LongPet-KWS-to-Vision-v1.1-Migration-Audit-2026-09-11.md)。
> 以下仓库内 KWS 路径已按当前 `components/` 布局更新；旧 IP、HEAD 和测试数字保留为历史事实。

日期：2026-09-07。本文取代 2026-08-31 报告中的旧关键词范围和旧音频交接说明。

## 1. 交付状态与仓库范围

- 只修改 `D:\code_qt\longpet_main\longpet`，没有修改旧 LongPet 目录或家属端仓库。
- 开始时 `main` 工作区干净，HEAD 为 `42ca6a9a5f73b8d32bf4c148177e2556d64f8af8`。
- 审查 [PR #3](https://github.com/Justin-fanfan/longpet/pull/3)，已抓取其 8 个提交到 `origin/pr-3`；审查的末端为 `acf3875a323349ed1dd908957dbcd4818c9ecc20`。
- 已将 PR 的词表扩展和本地业务动作接入当前工作区，并修复审查发现的问题。不是原样合并：保留组员的识别方案，将协调逻辑收口到现有分层。
- 未创建 Git 提交、未推送、未操作 GitHub 合并按钮。HEAD 保持不变，等待用户检查确认。
- 本轮完成 Windows Release、自动化业务/进程测试及真实 ONNX 加载检查；没有交叉编译、上传程序、重启或操作开发板。
- “代码闭环完成”不等于“板端验收完成”：真实关键词命中率、ALSA 切换耗时、长时间运行和云端工具调用准确性仍待下述板端实测。

## 2. PR 审查结论

PR 共涉及 `deploy/kws/longpet_kws_bridge.py`、`VoiceCommandDispatcher.{h,cpp}`、`Application.cpp` 四个文件。
词表覆盖方式可复用，不需要更换模型或加载两套识别器；不能原样判定无问题后直接合并。

| 来源 | 问题 | 本轮处理 |
| --- | --- | --- |
| PR | `callActivityChanged(true)` 在媒体准备后才暂停 KWS；没有等待暂停确认 | 所有媒体入口统一预留 ownership，双向音频/提示音等确认后再开始；信令/摄像头允许预热 |
| PR | `notifyExternalMediaActivity` 没有保存通话活跃状态，`mediaBusy()` 也未覆盖通话 | Dispatcher 保存外部通话状态，普通关键词在媒体忙时不启动新动作 |
| PR | 新增本地快捷指令在 Online 模式也直接执行 | 除唤醒/救命/停止外，仅 Offline 模式执行快捷词，Online 自然语言仍走 ASR/LLM |
| PR | 新增词阈值硬编码为 `0.05` | 增加 `kws/command_threshold` 和独立环境变量 |
| 原有 V3 | 暂停超时后仍继续启动录音；模型未 ready 时合成暂停确认 | 超时失败，不能据此放行；QProcess Starting 也算运行中 |
| 原有 V3 | AI 取消先释放逻辑 ownership，实际录音/播放进程稍后才退出 | 等 `cancellationFinished(sessionId)` 后释放，防止下一轮、通话和 KWS 抢设备 |
| 原有 V3 | 正常完成先发 activity=false，再改 Idle，容易漏掉 KWS 恢复 | 由协调器统一负责释放及冷却后恢复，不再依赖快照发布顺序 |
| 原有 V3 | 多次 sounddevice resume 创建的 multiprocessing 队列未关闭；采集子进程死亡可能一直等输入 | 关闭/回收队列与进程；检测子进程退出和 5 秒无 PCM；重启始终从暂停状态开始 |
| 原有 V3 | 工具 schema 的 required/type 限制未在执行层落实，无效日期可能被业务层修正成今天 | 执行前校验白名单、必填、类型、额外字段、枚举、日期时间和正整数 ID |
| 原有 V3 | 工具批次同步执行，取消难以介入；工具 ID 重复可能重复写入 | 每个工具在事件循环中执行；session 检查、批次上限、同 ID 同参数结果复用、冲突 ID 拒绝 |

未修改 KWS 组件内的声学推理代码、模型和 token 文件（当前路径为 `components/longpet-kws`）。

## 3. 调用链与音频信号流

现有 V1/V2 的 Provider、VAD、SentenceBuffer、顺序 TTS 队列、天气上下文和提醒数据库继续复用。

```text
Application：创建并连接对象
  KwsProcessAdapter ←→ Python bridge ← 上游 FSMN-CTC / capture
         │ KeywordDetected(keyword, score, timestamp)
         ▼
  VoiceCommandDispatcher ← VoiceCapabilityService（配置 / 网络 / Provider 失败冷却）
         ├─ Online 唤醒 → VoiceInteractionService → ASR → SSE LLM → TTS → 音频 Adapter
         │                                      └→ VoiceToolRegistry → ReminderService
         ├─ Offline 陪伴 → LocalCompanionService → 同一个 VoiceAudioPort
         └─ 本地语义信号 → AppController → 现有提醒 / 对话 / 通话 / 紧急页面

  AI / LocalCompanion / VideoCallService
         └→ MediaSessionCoordinator（唯一音频交接入口）
                reserve owner → pause(command_id) → paused(command_id) → mediaReady(owner)
                正常结束 / 音频退出确认 → release owner → cooldown → resume
```

重要约定：

1. `tryAcquire()` 只表示“预留成功”；必须再检查 `isReady(owner)` 或收到 `mediaReady(owner)` 才能开音频。
2. `KwsPort::isPaused()` 表示已确认释放采集设备，不表示“暂停请求已发出”。
3. KWS 启动使用 `--start-paused`，加载一次模型后常驻，暂停清理 capture、音频队列、VAD 和 FSMN cache。
4. `ready` 必须声明 `paused=true`；暂停/恢复回显 `command_id`。旧确认不能放行新请求，resume 在途也不算暂停。
5. 默认暂停期限为 5000 ms。超时终止本次动作并回收 KWS，绝不继续抢 ALSA。下一次仍可点击按钮重试。
6. KWS 启动 60 秒无 ready 会被回收；异常退出按 `restart_delay_ms` 重试，不终止主程序。Linux 上 bridge 被放入自己的进程组，强制停止时只回收该组及其采集子进程。
7. 通话仍使用原有 FamilyLink 和媒体协议。来电鉴权、busy、提示音、自动接通不变；本轮只补 KWS 与音频启动的交接。信令端口和摄像头预热不等于双向音频已连接。
8. 停止 AI 时先封锁旧 session 回调、取消网络和队列，再等待音频确认。快速 restart 后又 cancel，不会被已排队的 restart 回调重新启动。

KWS 的微小模型不会重复加载，但 sounddevice capture 子进程仍会在恢复时重新创建；这沿用上游隔离设计，重启耗时应在板端测量。

## 4. 正式词表与 Online/Offline 策略

| 关键词 | 行为 |
| --- | --- |
| 小龙小龙 | Online：直接进入 AI Listening；Offline：显示本地指令提示 |
| 救命 | 收到事件立即走本地紧急入口，不请求 AI |
| 停止 | 收到事件取消当前 AI / 离线播放；不通过 ASR |
| 陪我说话 | 仅 Offline：随机播放一条本地音频，可直接说，不强制先唤醒 |
| 打开提醒 | 仅 Offline：打开原有提醒页面 |
| 现在几点 | 仅 Offline：设备当前时间在现有对话页大字显示，不请求 TTS |
| 联系家人 | 仅 Offline：调用原有首页视频通话流程；没有网络时不能凭此保证家属可达 |
| 返回主页 | 仅 Offline：现有导航返回首页 |
| 音量大点 / 音量小点 | 仅 Offline：SettingsService 调整 ±10%，限制 0–100；无可用音量 Adapter 则提示失败 |
| 你好 | 兼容识别，业务层忽略，不作为第二个唤醒词 |

共 11 个识别词条，10 个正式业务词条。`返回主页` 使用 `反回主页` 声学别名；“返”不在当前 token 表中。
紧急页面保留期间屏蔽普通 KWS 快捷词，避免电视背景声把页面切走；页面“联系家人”按钮复用现有视频通话入口，
不再只有占位提示。它不是自动告警推送或自动呼叫急救服务，家属不可达时仍需使用其他求助渠道。
模型是通用中文 CTC，扩展匹配词表不等于为这些词重新训练过，也不等于准确率有保证。
真实 token 表检查和一次静音推理已通过；各词在 USB 麦克风、不同距离/噪声下的命中率尚未测量。

半双工边界必须明确：AI 录音、TTS、离线播放和通话期间 KWS 暂停，以避免抢麦克风和自触发。
所以这些阶段**不承诺声学“停止”或“救命”可被听见**。它们是 KWS 监听时的高优先级本地事件；
播放中的可靠取消入口是屏幕按钮。V3 不是全天候语音急救设备，不把无 AEC 的能力描述为全双工唤醒。

## 5. AI 可用性与离线资源

`VoiceCapabilityService` 集中判断三套配置、网络状态和最近 Provider 失败：

- 配置不完整、网络明显不可用：KWS 使用 Offline 策略，不发 ASR/LLM/TTS 请求。
- Provider 请求失败后进入 `availability_retry_ms` 冷却，默认 30 秒；冷却后允许一次新的正常尝试，不后台刷 API。
- 可用表示满足尝试条件，不是已主动探测每个模型和 Key。鉴权、模型不存在等仍由真实请求验证。
- UI 明确点击仍可手动重试 V2 链路；KWS 崩溃、禁用或缺文件不取消按钮入口。
- 公网 API 默认 `voice/require_internet=true`；三套 Provider 都在 LAN 时可设为 `false`，此时接受 Local/Site 网络，但仍要求网络连接和有效配置。不能用此字段让公网 API 在断网时变得可用。

离线陪伴目录：`/home/longpet/offline-audio`，可由 `offline/companion_audio_directory` 覆盖。
支持 GStreamer 可解码的 WAV/MP3/OGG/FLAC，推荐 16 kHz 或 48 kHz 单声道 WAV；单文件不超过 16 MiB，不跟随符号链接。
有两条以上文件时避免立即重复；空目录、无权限、空音频、损坏音频均安全报错。
播放会进入现有 ConversationPage，Speaking 动画、状态和“停止”可见，15 秒超时不会打断播放。
离开该页面也会取消音频，避免隐藏播放。

**本仓库尚未附带正式陪伴录音。** 请放入至少两条已确认授权的内容；没有文件时应显示“没有可播放的离线陪伴语音”，不是偷偷请求远程 TTS。
`offline/enabled` 是本地陪伴音频功能的开关；时间、提醒等本地快捷指令不依赖音频库。

## 6. Tool Calling

本轮白名单共 6 个工具：

| 名称 | 参数 / 操作 |
| --- | --- |
| create_reminder | 必填 `title`、`time(HH:mm)`；可选 `date(yyyy-MM-dd)`、`repeat(once/daily/weekdays)`、`type(other/water/medicine)`；复用 ReminderService 写入原 SQLite |
| list_reminders | 无参数，返回现有提醒 ID、时间、日期和状态 |
| delete_reminder | 必填正整数 `id`，先通过列表确认目标；真实删除，非虚拟操作 |
| get_current_time | 无参数，读取设备本地日期时间 |
| get_current_date | 无参数，返回设备日期、星期及本地时间 |
| open_page | `page` 仅限 home/reminders/care/settings/companion；正常回答完成后由 Controller 导航 |

`create_reminder` 未指定重复规则时改为 **once**，避免把一次口头提醒误建成每天。
未给日期时使用设备今天；若单次时间已过则选明天，并把实际日期返回 LLM。显式提供过去日期、非法日期或非法时间会拒绝。
所有额外字段、错误类型、枚举、非整数/非正 ID 在调用业务层前拒绝。没有开放 shell、任意文件、Python 或任意 HTTP 工具。

流程示例：

```text
“小龙小龙” → Listening → “每天十点半提醒我喝水”
→ ASR → LLM tool_calls(create_reminder)
→ {title:"喝水",time:"10:30",repeat:"daily",type:"water"}
→ ReminderService 写入 → tool result {ok:true,id:...,date:...,time:"10:30",...}
→ LLM 最终回答 → SentenceBuffer → TTS → 播放
```

SSE 支持跨 readyRead 的 JSON、UTF-8、函数名和参数分片；工具调用检查 finish_reason，拒绝 `length` 等截断结束。
单轮最多 8 个工具，参数不超过 32 Ki 字符，SSE 总量限制为 1 MiB；最多 `maximum_rounds`（默认 3）次工具循环。
同一 session 同 ID 同参数重复调用复用结果，不重复写入；同 ID 不同参数和批次重复 ID 会拒绝。
工具之间让出 Qt 事件循环，可在未执行前取消；已成功的数据库操作不会因为取消对话或最终 LLM 失败而假装撤销。
跨轮 history 仍只保留正常 user/assistant 对话；本轮工具消息用于后续 LLM 请求，错误文本和被取消的半回答不记作完整历史。

语音实时性的取舍：携带工具的 LLM 请求先缓冲待播文本，确认该轮没有工具调用才交给 TTS，避免播报中间计划或 JSON。
达到工具轮数上限后的无工具请求、或 `tools.enabled=false` 时，沿用 V2 的边生成边切句播放。
并非每个携带工具的普通回答都能在完整生成结束前发出第一段声音；这是本版明确的安全/延迟取舍。

## 7. 修改文件说明

- `deploy/kws/longpet_kws_bridge.py`：PR 词表、参数、带 ID 的 JSONL 确认、暂停启动、采集回收和模型自检。
- `src/platform/KwsProcessAdapter.{h,cpp}`、`src/services/KwsPorts.h`：加载/暂停定义、过期确认隔离、输出上限、启动期限、Linux 子进程组回收。
- `src/services/MediaSessionCoordinator.{h,cpp}`：将原有简单互斥锁补成异步音频交接点。
- `VoiceInteractionService.{h,cpp}`：等待麦克风、确认取消后释放、排队 restart 隔离、可取消/幂等工具批次、完成语义信号。
- `LocalCompanionService.{h,cpp}`、`OfflineAudioLibraryAdapter.cpp`：等待交接、停止/失败回收、资源限制。
- `VideoCallService.{h,cpp}`：来电/去电/远端接听都等待 KWS 释放后才开音频；原有通信协议不变。
- `VoiceCommandDispatcher.{h,cpp}`：Online/Offline 策略和本地动作，不再自己维护另一套音频暂停逻辑。
- `VoiceToolRegistry.cpp`、`OpenAiCompatibleProviders.{h,cpp}`：执行校验、日期工具、SSE 工具完整性和大小检查。
- `Application.cpp`、`AppController.{h,cpp}`：组合根接线；本地导航/音量/时间/陪伴 UI 由 Controller 处理。
- `AiModels.{h,cpp}`、`AiConfigRepository.cpp`：新配置和数值校验；三套 Provider 结构保留。
- `NetworkStatusAdapter.{h,cpp}`、`SystemModels.h`、`SystemService.{h,cpp}`、`VoiceCapabilityService.cpp`：区分 LAN 与互联网，支持自建 LAN AI 服务。
- `tests/V02Test.cpp`、`tests/test_kws_bridge.py`、`tests/fake_kws_bridge.py`、`CMakeLists.txt`：回归、协议和资源生命周期测试。
- `deploy/longpet-ai*.ini.example`、`deploy/配置说明.md`、本报告与历史报告入口：配置和交付说明。

## 8. Windows 测试与复现

环境：Qt 6.11.0 MinGW 64-bit、GCC 13.1、Qt 自带 CMake/Ninja，Release 构建目录 `build-v3-release`。

```powershell
$env:PATH = 'D:\Qt\Tools\mingw1310_64\bin;D:\Qt\6.11.0\mingw_64\bin;D:\Qt\Tools\Ninja;' + $env:PATH
& D:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build-v3-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DLONGPET_BUILD_TESTS=ON `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.11.0/mingw_64 `
  -DPython3_EXECUTABLE=<可导入numpy的Python路径>
& D:\Qt\Tools\CMake_64\bin\cmake.exe --build build-v3-release --parallel 3
& D:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-v3-release --output-on-failure
```

Python 缺失时 CMake 仍能构建产品，但进程测试会跳过；numpy 缺失时不注册 Python bridge 测试。本轮已提供 Python/numpy，两个 CTest 入口均实际运行。

本轮实际结果：

| 检查 | 结果 |
| --- | --- |
| Windows Release 配置与构建 | 通过 |
| CTest | 2 / 2 入口通过 |
| Qt 详细回归 | 45 passed，0 failed，0 skipped |
| Python bridge 单元测试 | 7 项通过 |
| 离屏页面渲染 | 9 个 1024×600 页面生成；对话页中文、Speaking 表情及停止/重新说话按钮已目视检查 |
| 真实 ONNX 模型自检 | 加载与静音推理通过，11 个词条 token 覆盖通过；不代表声学准确率 |
| Git 差异空白检查 | `git diff --check` 通过 |
| LoongArch 编译 / 开发板录音、唤醒与声学性能 | 本轮未执行 |

离屏截图可选设置如下；`QT_QPA_FONTDIR` 让 Windows 离屏插件找到系统中文字体，不需要修改产品字体资源：

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:/Windows/Fonts'
$env:LONGPET_TEST_CAPTURE_DIR = 'D:/code_qt/longpet_main/longpet/build-v3-release/screens'
& .\build-v3-release\LongPetV02Tests.exe renderV02Pages -o build-v3-release/render-result.txt,txt
```

覆盖重点：

- 原有 V2 正常闭环、失败/超时/断网/取消、顺序 TTS、history、旧 session 隔离测试继续执行。
- KWS 释放确认前不开录音；取消音频未结束时通话返回 busy；暂停超时不开录音；迟到确认不复活已失败会话。
- 30 轮 AI 获取/取消释放；已排队 restart 再 cancel 不会重启。
- 来电提示音和双向音频延迟到 KWS 确认；来电等待中取消；去电被家属接听也不能绕过确认。
- Online 忽略离线快捷词；Offline 直接陪伴；时间/提醒/音量路由；通话忙时屏蔽普通词；播放页不超时且 UI 可停止。
- 真实 SQLite 提醒写入/删除；缺字段、错误类型、日期、枚举、额外字段和未知工具拒绝；待执行时取消；重复 ID 不重复写入；最终 LLM 失败不回滚已成功操作。
- 恶意/损坏 SSE 的索引、参数、结束原因和类型拒绝。
- 实际 QProcess 执行测试 bridge：加载时不伪造 ACK；20 次快速 resume/pause；故意过期的 command_id 被忽略；停止只确认一次。
- Python 单元测试 7 项：命令协议/EOF、词表 token 覆盖、暂停启动、30 轮 arecord 生命周期、30 轮 sounddevice 队列全部关闭、采集子进程死亡清理、重复 resume 回显最新 command_id。

真实 ONNX 自检另行执行：使用仓库内模型，实际加载并推理静音，不打开任何麦克风。
本机一次结果 `model_load_ms=109`，11 个词条覆盖通过；**这是 Windows 数据，不是板端性能或声学准确率**。
用于检查的 ONNX Runtime/sounddevice 只安装在忽略的 `build-v3-release/kws-test-deps`，没有改变板端运行环境，也未新增产品运行时。
模型 SHA256：`6febd9f7f15c47caed88d434d810651e34215334c66961b9ba66251fa04d98c4`。

Windows 构建有原有的 Linux-only 辅助函数未使用、聚合成员默认初始化等编译警告，以及可选 Vulkan headers 未找到提示；它们不阻止本项目 Widgets/Release 构建。

## 9. 板端快速部署和验收

板端地址按当前信息为 `10.240.178.51`，服务 `longpet.service`，程序 `/home/longpet/LongPet`。
本轮没有操作板子。Windows `.exe` 不能部署到 LoongArch，请使用你现有的交叉编译流程产出 Linux 程序。

需要一起部署：

1. 新的 LoongArch `LongPet`；
2. `deploy/kws/longpet_kws_bridge.py` → `/home/longpet/longpet-kws/longpet_kws_bridge.py`；
3. 已存在的 `components/longpet-kws` → `/home/longpet/longpet-kws/upstream`（有同版文件无需重复上传）；
4. 已有 `ai.ini` 保留三个 Provider 的真实配置，只补新字段；不要用示例覆盖 Key；
5. 至少两条已授权陪伴 WAV → `/home/longpet/offline-audio/`。

先停止服务并备份旧程序/bridge/config，再成对替换程序和 bridge。建议权限为 `longpet:longpet`；音频文件 0640、目录 0750。
不要再独立自启动上游 `run.py`，否则会出现两套采集抢麦克风。

配置增量：

```ini
[voice]
require_internet=true
availability_retry_ms=30000

[kws]
command_threshold=0.05
pause_timeout_ms=5000
resume_cooldown_ms=1200
restart_delay_ms=2000
```

`sounddevice` 使用 `input_device`，不是 `alsa_device`；备用 `arecord` 使用 `alsa_device`。
不要写死某次 USB 枚举序号。保留之前验证可用的设备配置，见配置说明的两种后端示例。
新增覆盖变量：`LONGPET_VOICE_REQUIRE_INTERNET`、`LONGPET_KWS_COMMAND_THRESHOLD`。

只检查模型、不占麦克风：

```bash
/usr/bin/python3 /home/longpet/longpet-kws/longpet_kws_bridge.py \
  --kws-root /home/longpet/longpet-kws/upstream \
  --model /home/longpet/longpet-kws/upstream/assets/fsmn/fsmn_ctc.onnx \
  --tokens /home/longpet/longpet-kws/upstream/assets/fsmn/tokens.txt --check-model
```

启动服务后观察：

```bash
systemctl restart longpet.service
journalctl -u longpet.service -f -o cat
```

建议按此顺序快测：

1. Online：“小龙小龙”→看到聆听→说一句普通话→听到回答→日志出现 KWS resumed。连做 10 次。
2. 工具：“每天十点半提醒我喝水”→提示正在执行→提醒列表真实出现。再查列表、删除明确的那一条；不要用真实用药任务作为破坏性测试数据。
3. 每个阶段点停止，再立即重新说话；旧文字/旧音频不应返回；没有持续 device busy。
4. 在可安全本地操作的条件下断开网络，再直接说“陪我说话”→播放本地 WAV。反复两次检查不立即重复，点停止。不要为了测试断开你唯一的远程管理链路。
5. Offline 测“打开提醒/现在几点/返回主页/音量大点/音量小点”。“联系家人”只能发起已有通话流程，真正连接仍需要 LAN。
6. 在 KWS 监听的普通页面测“救命”；Online 与 Offline 都进入紧急页面，不等云服务。
7. 家属来电、板端去电各做语音/视频一次，检查提示音期间没有 AI 采集、结束后能再次唤醒。
8. 真正对着 USB 麦克风逐个测试 10 个正式关键词，每词至少 10 次，并记录距离、安静/音乐背景、误触和漏检；同音映射尤其需要测“返回主页”。

如遇问题，请提供本轮关键日志，不要贴 API Key。不要把 Windows mock 的通过结果当作完成上述步骤。

## 10. 性能日志与后续边界

日志格式示例（`N` 为实际数值占位，不是实测性能）：

```text
KWS ready startup_ms=N model_load_ms=N paused=1
KWS keyword=小龙小龙 score=0.9000 timestamp_ms=N
KWS paused command_id=N release_ms=N
Audio ownership ready owner= voice_interaction
Voice interaction session=N stage=Tool name=create_reminder success=1 duration_ms=N cached=0
Voice metrics session=N result=completed recording_ms=N asr_ms=N llm_ttft_ms=N ... total_ms=N
KWS resumed command_id=N
```

旧 V2 的 recording/ASR/TTFT/首句/首 TTS/总耗时统计保留。`speech_latency_ms` 当前是音频进程启动事件到录音结束的差，
不是声卡实际发出第一声的硬件测量；ALSA 缓冲、设备延迟需在板端另外测。日志不输出 API Key 或完整工具参数。

后续仍未实现：AEC、声学全双工打断、流式 ASR、真正流式 TTS 协议、长期记忆、本地 LLM、自动 Provider fallback、可靠的全时紧急语音检测。
本轮也没有重新训练关键词模型，没有给未实际测过的识别率、板端 CPU/内存/长稳指标作保证。

板端最终需要记录：模型启动耗时、KWS 监听 CPU/RSS、pause→AI 录音耗时、AI 完成→KWS 恢复耗时、每词成功次数、30–50 轮后的音频可用性。
