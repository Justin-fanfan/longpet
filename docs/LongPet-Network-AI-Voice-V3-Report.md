# LongPet AI 语音交互 V3 实现与测试报告

日期：2026-08-31  
目标平台：LongPet / LoongArch64 / Qt 6  
配置文件：`/etc/longpet/ai.ini`

## 1. 本轮结论

V3 已在 V2 `VAD → ASR → SSE LLM → SentenceBuffer → TTS Queue` 链路上增加：

- 基于上游 `ycxuan0517/loongpet_kws` 的本地 KWS 进程接入；
- 版本 1 JSON Lines 稳定协议和 `start/pause/resume/stop/restart` 生命周期；
- “在线 AI / 离线语音”统一可用性判断和关键词策略；
- KWS、ASR/TTS/离线播放之间的 USB 麦克风/扬声器互斥与冷却；
- 随机、不立即重复、可停止的本地陪伴音频；
- OpenAI-compatible 流式 Tool Calling；
- ReminderService/时间/页面白名单工具；
- 工具循环上限、参数验证、session 取消隔离和分阶段日志；
- 本地“救命”紧急页面入口。

未重新设计或替换 V1/V2。页面仍不访问数据库、网络、Python、ALSA 或文件系统。

## 2. 上游 KWS 核对结果

接入依据：[loongpet_kws](https://github.com/ycxuan0517/loongpet_kws)。本轮完整检查了
`README.md`、`run.py`、`src/longpet_kws/cli.py`、`fbank.py`、`vad.py`、模型和词表。

上游当前特征：

- WeKWS FSMN-CTC，约 75.6 万参数；
- `fsmn_ctc.onnx` 约 3.07 MB；
- 依赖 NumPy、ONNX Runtime、sounddevice；
- 板端默认 48 kHz 采集，重采样至模型使用的 16 kHz；
- LoongArch 单核适配中，采集位于独立 Python 进程；
- 音频队列最多约 400 ms，满时丢弃旧块；
- 模型推理线程数限制为 1；
- 已有 NumPy 能量 VAD 和 `arecord` 备用后端。

当前上游代码实际声明且标定的关键词只有：

| 关键词 | V3 策略 | 当前可用 |
|---|---|---|
| 小龙小龙 | 在线可用时启动 AI；否则进入离线指令窗口 | 是 |
| 你好 | 明确忽略，不作为正式唤醒词 | 检测可用，业务忽略 |
| 陪我说话 | 离线指令窗口播放本地陪伴音频 | 是 |
| 救命 | 不依赖网络，进入本地紧急页面 | 是 |
| 停止 | 复用取消入口 | 调度已预留，当前模型不支持 |
| 打开提醒 / 现在几点 / 联系家人 / 返回主页 | 对应本地动作 | 当前模型不支持，未伪造 |

LongPet 把上游源码与模型（`fsmn_ctc.onnx`、`tokens.txt`）作为 vendored 依赖放入
`third_party/longpet-kws`，随仓库一并提交；部署时整目录拷贝到板端 `/home/longpet/longpet-kws/upstream`。
LongPet 仍不复制或重写 FBank、VAD 或推理代码，只在二者约好的目录结构（`kws_root/src`）之上提供协议 bridge。

## 3. V3 架构

```text
HomePage / ConversationPage / EmergencyPage
                         │
                    AppController
                         │
              VoiceCommandDispatcher
        ┌────────────────┼────────────────┐
        │                │                │
VoiceCapability     KwsPort          LocalCompanion
  Service              │               Service
 config + network  KwsProcessAdapter       │
 + provider health     │          OfflineAudioLibraryPort
        │          JSONL bridge             │
        │               │      OfflineAudioLibraryAdapter
        │        upstream loongpet_kws       │
        └───────────────┬┴───────────────────┘
                        │ pause/release/resume
               VoiceInteractionService (V2)
                ┌───────────────┴──────────────┐
       OpenAI-compatible LLM             VoiceAudioPort
        SSE content/tool_calls          VoiceAudioAdapter
                │                       GStreamer / USB Audio
        VoiceToolRegistry
                │
          ReminderService / local time / navigation signal
                │
        Repository（仅 ReminderService 内部）
```

职责边界：

- `Application`：创建并连接对象；
- `AppController`：页面导航和 UI 语义动作；
- `VoiceCommandDispatcher`：关键词优先级、在线/离线策略、音频交接；
- `VoiceInteractionService`：V2 会话、工具轮次、最终回答和 TTS；
- `VoiceToolRegistry`：固定工具 schema、参数验证和 Service 调用；
- `KwsProcessAdapter`：QProcess、JSONL 协议、异常重启；
- Python bridge：封装上游 KWS 的模型、VAD 和采集，不包含 LongPet 业务策略；
- UI：只显示状态和发信号。

## 4. 在线可用性与关键词优先级

`VoiceCapabilityService` 集中判断在线 AI 是否可用：

```text
ASR/LLM/TTS 配置有效
        AND
SystemService 报告网络可用
        AND
最近 Provider 请求未进入失败冷却期
```

它不是某个页面的布尔值，也不只判断 Wi-Fi。ASR/LLM/TTS Provider 失败后进入
`availability_retry_ms` 冷却；完整闭环成功会立即恢复。麦克风或本地播放错误不会被误判成
远程 Provider 不可达。UI 按钮不受 KWS 缺失影响，仍允许手工尝试在线链路。

优先级：

1. `救命`：取消当前语音/离线播放，立刻进入本地紧急页面；
2. `停止`：取消入口已实现，但当前模型不会产生该事件；
3. `小龙小龙`：在线 AI 或离线指令窗口；
4. 离线窗口内的 `陪我说话`；
5. `你好`：记录“策略忽略”，不执行；
6. 未知词：只记录，不执行。

## 5. KWS 协议与生命周期

Qt 与 Python 之间使用逐行 JSON，协议名 `longpet-kws`，版本 `1`。

关键词事件示例：

```json
{"protocol":"longpet-kws","version":1,"event":"keyword","keyword":"小龙小龙","score":0.1832,"timestamp_ms":1788109000000}
```

其他事件：

```text
ready / paused / resumed / error / stopped
```

Qt 发给 bridge 的命令：

```json
{"protocol":"longpet-kws","version":1,"command":"pause"}
{"protocol":"longpet-kws","version":1,"command":"resume"}
{"protocol":"longpet-kws","version":1,"command":"stop"}
```

重要行为：

- 模型随 bridge 启动加载一次；
- `pause` 会真正停止并释放采集设备，但不卸载 ONNX Session；
- `resume` 重新打开采集并清空 VAD/FSMN 流状态；
- ASR、TTS 或离线播放开始前，Dispatcher 等待 `paused`；
- 超过 `pause_timeout_ms` 后允许业务继续，以免 UI 永久卡住，音频层仍会安全报告设备占用；
- 会话结束后等待 `resume_cooldown_ms`，bridge 丢弃旧队列，再恢复 KWS；
- 进程异常退出后按 `restart_delay_ms` 重启；缺程序/bridge/model/tokens 时只降级，不退出 LongPet；
- Qt 只解析 JSONL，不通过正则解析 README 中的 `WAKE:`、`COMMAND:` 或调试文本。

## 6. 离线陪伴

目录由 `[offline] companion_audio_directory` 配置，默认：

```text
/home/longpet/offline-audio
```

行为：

- 支持 GStreamer 能解码的 WAV/MP3/OGG/FLAC；
- 单文件最大 16 MiB；
- 两条以上音频时不会连续两次播放同一条；
- 读取失败、空目录、空文件和播放失败都会形成普通用户可理解的错误；
- 播放使用现有 `VoiceAudioPort/VoiceAudioAdapter`，不新建第二套 ALSA 实现；
- 取消、紧急求助、页面动作可停止当前播放并释放 MediaSession；
- 不经过 ASR、LLM 或远程 TTS。

仓库只提供 `deploy/offline-audio/README.md`，不提交未经确认授权的录音。

## 7. Tool Calling

### 7.1 OpenAI-compatible 协议

`OpenAiCompatibleLlmProvider` 支持：

- 请求顶层 `tools` 和 `tool_choice=auto`；
- 非流式 `message.tool_calls`；
- SSE `delta.tool_calls[index]`；
- `id`、`function.name`、`function.arguments` 跨任意网络分片累计；
- 多个 tool call 按 index 保序；
- `[DONE]` 或非空 `finish_reason` 正常结束；
- 工具调用和普通 content 共存；
- session cancel 继续复用 V2 的 HTTP Reply 取消。

### 7.2 工具白名单

| 工具 | 作用 | 实际入口 |
|---|---|---|
| `create_reminder` | 创建提醒 | `ReminderService::save` |
| `list_reminders` | 列出提醒及 ID | `ReminderService::reminders` |
| `delete_reminder` | 按 ID 删除提醒 | `ReminderService::remove` |
| `get_current_time` | 获取板端本地日期时间 | `QDateTime::currentDateTime` |
| `open_page` | 打开 home/reminders/care/settings/companion | AppController 导航信号 |

所有工具使用固定 JSON Schema、类型/枚举/时间/日期/ID 校验。未知工具、损坏 JSON、Service 验证错误
都返回结构化失败结果给模型，不执行任意 shell、文件、HTTP 或动态方法。

### 7.3 工具轮信号流

```text
ASR user text
   → LLM request(tools)
   → SSE tool_calls delta（只更新内部缓冲，不送 TTS）
   → VoiceToolRegistry.execute
   → assistant(tool_calls) + tool(result) messages
   → LLM follow-up
   → final assistant text
   → SentenceBuffer → TTS Queue → Playback
```

只有最终回答写入短期 history。工具中间文字、tool JSON、失败文本和取消中的半截回答不会污染下一轮。
达到 `maximum_rounds` 后，下一次 LLM 请求不再携带工具，避免无限循环。

携带工具的“决策轮”必须等结束后才能确认是否会调用工具，因此普通回答在这一轮的第一段 TTS 会比
纯 V2 稍晚；这是避免先播出中间话术、随后又执行工具的正确性取舍。工具后的最终无工具轮仍按 V2
流式切句和播放。

## 8. 取消、资源与异常

UI 取消、未来“停止”关键词和紧急求助共用 Dispatcher 入口：

- 取消录音、ASR、LLM、未完成 TTS；
- 清空 SentenceBuffer、TTS/播放队列；
- 停止本地陪伴；
- 工具回调继续受 session id 检查；
- 已经成功写入数据库的工具操作不回滚；
- 释放 MediaSession 后再恢复 KWS；
- 旧 session 的 HTTP、工具、TTS、Playback 回调不能更新新会话。

工具当前只执行本地、短耗时 Service 调用。SQLite 提醒写入一旦返回成功就是不可变事实；用户在随后
取消回答时只停止生成和播放，不撤销提醒。

## 9. 配置

完整示例：`deploy/longpet-ai.ini.example`。新增分组：

```ini
[voice]
availability_retry_ms=30000

[tools]
enabled=true
maximum_rounds=3

[kws]
enabled=true
python_program=/usr/bin/python3
bridge_script=/home/longpet/longpet-kws/longpet_kws_bridge.py
kws_root=/home/longpet/longpet-kws/upstream
model_path=/home/longpet/longpet-kws/upstream/assets/fsmn/fsmn_ctc.onnx
tokens_path=/home/longpet/longpet-kws/upstream/assets/fsmn/tokens.txt
capture_backend=sounddevice
input_device=
alsa_device=
input_sample_rate=48000
wake_threshold=0.15
hello_threshold=0.10
companion_threshold=0.05
emergency_threshold=0.05
vad_threshold_db=-60.0
vad_noise_ratio=2.5
command_timeout_ms=10000
pause_timeout_ms=1500
resume_cooldown_ms=1200
restart_delay_ms=2000

[offline]
enabled=true
companion_audio_directory=/home/longpet/offline-audio
```

`input_device` 默认空，使用 PortAudio 默认输入；不再永久写死设备序号 `2`。PortAudio 故障时可改为
`capture_backend=arecord` 并配置稳定的 ALSA Card ID。

## 10. 新增与修改文件

新增核心文件：

- `src/services/KwsPorts.h`
- `src/platform/KwsProcessAdapter.*`
- `deploy/kws/longpet_kws_bridge.py`
- `src/services/VoiceCapabilityService.*`
- `src/services/VoiceCommandDispatcher.*`
- `src/services/OfflineVoicePorts.h`
- `src/platform/OfflineAudioLibraryAdapter.*`
- `src/services/LocalCompanionService.*`
- `src/services/VoiceToolRegistry.*`
- `deploy/offline-audio/README.md`
- `docs/LongPet-Network-AI-Voice-V3-Report.md`

主要修改：

- `src/model/AiModels.*`：KWS/离线/工具配置、事件、消息/tool call 模型；
- `src/data/AiConfigRepository.cpp`：INI 和环境变量；
- `src/services/VoiceInteractionPorts.h`：带工具 LLM 请求和完成信号；
- `src/platform/OpenAiCompatibleProviders.*`：工具请求、非流式/流式解析；
- `src/services/VoiceInteractionService.*`：工具循环、最终回答、Provider 健康信号；
- `src/app/Application.*`：V3 对象装配；
- `src/app/AppController.*`：统一 Dispatcher、导航和紧急入口；
- `src/mainwindow.*`：接入已有 EmergencyPage；
- `CMakeLists.txt`、`tests/V02Test.cpp`：构建与回归；
- `deploy/longpet-ai*.ini.example`、`deploy/配置说明.md`：部署说明。

## 11. 本地测试结果

Windows Qt 6.11.2 / MSVC Release：

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

实际结果：

- `LongPet` Release 编译通过；
- `LongPetV02Tests` Release 编译通过；
- CTest `LongPet.V02`：1/1 通过，最终一次 3.87 秒；
- Python bridge `--help` 参数/语法解析通过；
- `git diff --check` 通过（仅存在仓库既有 Windows 行尾提示）。

新增自动化覆盖：

- SSE tool call 跨 HTTP/SSE 分片累计 name 和 arguments；
- 请求确实携带 `tools/tool_choice`；
- 工具决策前的中间文字不会进入 TTS；
- 创建提醒实际经过 ReminderService 并写入 Repository；
- 工具结果回传后只播放最终回答；
- 工具消息不进入下一轮普通 history；
- 最大工具轮后不再发送工具；
- “你好”忽略；
- 在线“小龙小龙”先暂停 KWS 再打开录音；
- “停止”复用取消并恢复 KWS（调度测试，当前模型不产生该词）；
- 离线唤醒和“陪我说话”；
- 两条离线音频不立即重复；
- “救命”本地优先；
- V1/V2、提醒、Family Link、视频、网络、天气和页面回归。

## 12. 板端快速测试步骤

本轮按要求没有交叉编译、上传程序、重启服务或声称板端通过。部署细节见
`deploy/配置说明.md`。建议实测顺序：

1. 安装上游仓库、模型、词表、bridge 和至少两条离线 WAV；
2. 确认 `/usr/bin/python3` 可导入 `numpy/onnxruntime/sounddevice`；
3. 停止 LongPet，使用上游 `run.py --list-devices` 核对 USB 麦克风；
4. 安装新的 `/etc/longpet/ai.ini` 并启动服务；
5. 检查 `KWS ready`，连续说 5 次“小龙小龙”；
6. 在线时验证自动进入 Listening 和完整 ASR→LLM→TTS；
7. 断网后说“小龙小龙”，再说“陪我说话”，确认不发网络请求；
8. 连续触发两次陪伴，确认音频不同；播放中点击停止；
9. 在线/离线分别说“救命”，确认立即进入紧急页面；
10. 播放 TTS 时确认 KWS 暂停，播放后约 1.2 秒恢复且不自唤醒；
11. 语音说“每天十点半提醒我喝水”，检查 UI、数据库和提醒列表；
12. 询问提醒、删除指定提醒、询问时间、打开提醒页；
13. ASR/LLM/TTS 分别制造失败，观察下一次 KWS 进入离线策略；
14. 杀掉 KWS Python 进程，确认 LongPet 不退出且 bridge 自动恢复；
15. 连续 10 次在线/离线切换，检查 USB 声卡无占用和 CPU/内存。

推荐日志：

```bash
journalctl -fu longpet.service
journalctl -b -u longpet.service --no-pager | \
    grep -E 'KWS |KWS keyword=|stage=Tool|Voice metrics'
```

## 13. 已知限制与后续版本

- 当前 KWS 模型只有四个已标定词；离线“停止/提醒/时间/联系家人/主页”待同一模型更新后启用；
- `EmergencyPage` 是本地求助流程，尚未实现向家属端主动推送或电话呼叫；
- 仓库不附带离线陪伴录音，部署者必须提供已授权资源；
- 尚未在板端测量 KWS ONNX 常驻 CPU/内存、误唤醒率和远场识别率；
- Tool Calling 依赖目标 LLM 的 OpenAI-compatible `tools/tool_calls` 支持；
- 工具决策轮为保证不误播中间内容，首段 TTS 可能晚于纯 V2；
- 没有实现全双工抢话、AEC、WebSocket ASR、partial ASR、流式 TTS、长期记忆、Provider fallback；
- 当前本地工具在 Qt 线程执行，但都是短耗时 SQLite/时间/导航操作；未来网络型工具应增加异步 Tool Port，
  不应直接扩展为阻塞调用；
- 本轮（V3 撰写时）未做交叉编译和板端实测，必须按第 12 节完成验收后再判断硬件表现。

> **补充（2026-08-31，板端实测）**：随后在板端做了 KWS 唤醒实测，发现
> `capture_backend=arecord` 时**说“小龙小龙”无反应**，根因是 vendored
> `third_party/longpet-kws/src/longpet_kws/cli.py` 的 `ArecordCapture` 用 `bufsize=0`
> 导致只采到 1 个音频块即退出（识别类误判为 EOF），关键词得分恒为 0。去掉 `bufsize=0`
> 后修复，并在部署服务上实测 `KWS keyword=小龙小龙 score=0.2994` 成功唤醒 AI。
> 完整排查、修复与验证见
> [LongPet-KWS-WakeWord-No-Reaction-Report.md](LongPet-KWS-WakeWord-No-Reaction-Report.md)。

## 14. 四个核心验收场景

### A. 在线 AI

```text
小龙小龙 → KWS pause/release → Listening → ASR → LLM → TTS → resume KWS
```

### B. 在线工具

```text
“每天十点半提醒我喝水”
→ ASR
→ create_reminder tool call
→ ReminderService::save
→ tool result 回传 LLM
→ 最终确认文字和语音
```

### C. 离线陪伴

```text
在线能力不可用
→ 小龙小龙
→ 10 秒离线指令窗口
→ 陪我说话
→ 本地随机音频（不经过 ASR/LLM/TTS）
```

### D. 紧急求助

```text
救命 → 取消当前媒体 → EmergencyPage
```

上述场景的代码路径和自动化替身测试已完成；真实 KWS 声学效果、USB 麦克风交接和板端性能仍以
上板结果为准。
