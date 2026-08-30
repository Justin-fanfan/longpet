# LongPet 联网 AI 语音交互 V2 实现与测试报告

日期：2026-08-30  
目标平台：LongPet / LoongArch64 / Qt 6  
配置文件：`/etc/longpet/ai.ini`

## 1. 本轮目标与结论

V2 在已经跑通的 V1 `录音 → ASR → LLM → TTS → 播放` 链路上增加：

- 基于 16 kHz 单声道 PCM 能量的轻量 VAD；
- OpenAI-compatible Chat Completions SSE 流式解析；
- UI 增量回答；
- 中文 SentenceBuffer；
- 有界、保序的 TTS 与播放队列；
- 任意阶段取消和 Speaking 阶段重新开始；
- session id 隔离旧异步回调；
- 受 `history_turns` 限制的有效对话历史；
- 每轮阶段日志与延迟统计。

主 UI 线程中没有阻塞式网络等待。录音和播放仍复用现有 GStreamer/ALSA Adapter；ASR、LLM、TTS 仍使用三套独立 Provider 配置。

## 2. 架构与调用链

```text
HomePage::m_talkButton / ConversationPage 按钮
                    │
               AppController
                    │
          VoiceInteractionService
          ┌─────────┼──────────┐
          │         │          │
   VoiceActivity  Sentence   Session/Queue
     Detector      Buffer     /Metrics
          │         │          │
 VoiceAudioPort  LlmProviderPort   TtsProviderPort
          │         │          │
 VoiceAudioAdapter  OpenAI SSE  独立 TTS Provider
          │
 GStreamer + USB 麦克风/扬声器
```

职责边界：

- 页面只发送开始、手动结束、取消、重新开始信号，并显示 Snapshot；
- AppController 只负责导航和用户动作映射；
- VoiceInteractionService 负责一轮会话的状态、队列、历史和取消；
- Provider 负责具体 HTTP 协议；
- VoiceAudioAdapter 负责板端采集、播放和 PCM 电平上报；
- 页面和 Widget 不访问网络、ALSA 或数据库。

## 3. VAD

VoiceAudioAdapter 在读取 16-bit little-endian PCM 时计算 RMS dBFS，并上报：

```text
recordingProgress(sessionId, capturedMs, levelDb)
```

VoiceActivityDetector 只保存很小的时间状态：

1. 音量连续超过 `vad_threshold_db` 达到 `vad_minimum_speech_ms` 后确认已经讲话；
2. 已经讲话后，持续低于阈值达到 `vad_silence_timeout_ms` 时结束；
3. `recording_minimum_ms` 防止过早结束；
4. `recording_maximum_ms` 始终作为硬上限；
5. 没有检测到讲话时不会仅因初始静音提前上传空录音。

它不引入神经网络、外部 VAD 模型或新的运行时依赖。

## 4. SSE 流式 LLM

ProviderHttpClient 增加流式 POST 和分片信号。`request_timeout_ms` 对 SSE 表示“无数据活动超时”：每次收到有效网络分片都会重新计时。

SseEventParser 支持：

- 任意 TCP/`readyRead` 分片；
- CRLF 或 LF；
- 多个 `data:` 行；
- keep-alive 注释；
- 一个分片内多个事件；
- 结束时剩余事件 flush。

OpenAiCompatibleLlmProvider 逐事件解析 `choices[0].delta.content`，向 Service 发出 `chatDelta`；以 `[DONE]` 或非空 `finish_reason` 判断正常结束。JSON 损坏或流异常截断会形成明确的 LLM 错误，不会把部分回答写入历史。

## 5. SentenceBuffer 与双队列

SentenceBuffer 按 `。！？；`、英文句末符号、换行和省略号切分。策略包括：

- 句子不足最少字符数时与后文合并；
- 句末标点刚好位于当前 SSE 分片末尾时短暂保留，以吸收下一分片可能到来的闭引号；
- 无标点文本达到最大字符数时优先在逗号、顿号、冒号或空格处切段；
- LLM 正常结束时 flush 最后一段，即使没有句号也会播放。

队列结构：

```text
LLM delta
   │
SentenceBuffer
   │ 完整句
TTS 文本队列（最多保留 8 项，超出时合并尾项）
   │ 同时只允许 1 个 TTS 请求在途
播放队列（由 tts_prebuffer_segments 限制）
   │
USB 扬声器严格按原顺序播放
```

因此第一句播放时，LLM 可以继续生成后文，TTS 也可以预合成下一句；后一句不会抢播。

## 6. 状态与取消

对外状态：

```text
Idle → Listening → Recognizing → Thinking → Speaking → Idle
                      │             │          │
                      └──── Error / Offline ───┘

任意活动状态 → Cancelled（事件）→ Idle
```

`generationActive` 与 `playbackActive` 是独立标志。进入 Speaking 后，LLM 仍可继续生成并更新 UI。

取消会：

- 停止录音上限计时和 UI 合并计时；
- cancel ASR、LLM、TTS Reply；
- 清空 SentenceBuffer、TTS 文本队列和播放队列；
- 停止当前 GStreamer 录音或播放；
- 释放 MediaSessionCoordinator 所有权；
- 不写入未完成历史；
- 保留旧 session id，使迟到回调被拒绝。

Speaking 时点击“重新说话”，Service 先取消旧 session，并等待 VoiceAudioAdapter 明确发出 `cancellationFinished`，再创建新 session、重新打开麦克风。这避免旧 `alsasink` 尚未退出时立即启动 `alsasrc` 导致设备占用。

`clearConversationHistory()` 可在 Idle/Error/Offline 状态清空短期上下文，预留给以后设置页或 KWS 指令使用。

## 7. 配置

完整示例见 `deploy/longpet-ai.ini.example`。V2 新增字段：

```ini
[voice]
vad_enabled=true
vad_threshold_db=-42.0
vad_silence_timeout_ms=900
recording_minimum_ms=600
vad_minimum_speech_ms=160
recording_maximum_ms=12000
llm_stream_enabled=true
sentence_tts_enabled=true
sentence_minimum_characters=6
sentence_maximum_characters=120
tts_prebuffer_segments=2
history_turns=4
```

每项均有对应 `LONGPET_VOICE_*` 环境变量覆盖，详见 `deploy/配置说明.md`。`[asr]`、`[llm]`、`[tts]` 的独立 Provider、URL、Key、模型和 voice 没有改变。

## 8. 性能日志

每轮最终产生一行不含密钥和请求正文的统计：

```text
Voice metrics session=17 result=completed recording_ms=2840 asr_ms=722 llm_ttft_ms=438 llm_total_ms=2180 first_sentence_ms=811 first_tts_ms=534 speech_latency_ms=2109 total_ms=6842 tts_ok=3 tts_failed=0
```

字段含义：

- `recording_ms`：实际录音时长；
- `asr_ms`：ASR 请求耗时；
- `llm_ttft_ms`：LLM 请求到首个 delta；
- `llm_total_ms`：LLM 流总耗时；
- `first_sentence_ms`：LLM 开始到第一完整句；
- `first_tts_ms`：第一段 TTS 请求耗时；
- `speech_latency_ms`：用户说完到第一段声音开始播放；
- `total_ms`：整轮总耗时；
- `tts_ok/tts_failed`：分句合成结果计数。

查看命令：

```bash
journalctl -b -u longpet.service --no-pager | \
    grep -E 'Voice interaction session=|Voice metrics session='
```

## 9. 新增和修改文件

新增：

- `src/platform/SseEventParser.*`
- `src/services/SentenceBuffer.*`
- `src/services/VoiceActivityDetector.*`
- `docs/LongPet-Network-AI-Voice-V2-Report.md`

主要修改：

- `src/model/AiModels.*`：V2 状态、并行标志和配置；
- `src/data/AiConfigRepository.cpp`：INI/环境变量读取与校验；
- `src/services/VoiceInteractionPorts.h`：LLM delta、音频电平和取消完成信号；
- `src/platform/ProviderHttpClient.*`：SSE 分片传输；
- `src/platform/OpenAiCompatibleProviders.*`：流式 Chat Completions；
- `src/platform/VoiceAudioAdapter.*`：PCM dBFS 与音频释放确认；
- `src/services/VoiceInteractionService.*`：VAD、流式 UI、双队列、取消、历史、指标；
- `src/app/AppController.cpp`、`src/pages/ConversationPage.cpp`：重新说话和新状态显示；
- `deploy/longpet-ai*.ini.example`、`deploy/配置说明.md`：部署配置；
- `tests/V02Test.cpp`、`CMakeLists.txt`：V2 自动化覆盖与构建清单。

## 10. 本地测试

Windows Qt 6.11.2 / MSVC Release：

```powershell
cmake --build build --config Release --parallel
$env:Path = "C:\Qt\6.11.2\msvc2022_64\bin;$env:Path"
ctest --test-dir build -C Release --output-on-failure
```

实测结果：

- Windows Release 主程序和测试程序编译通过；
- CTest `LongPet.V02`：1/1 通过，最终一次用时 2.95 秒；
- Buildroot LoongArch64 Release 交叉构建：39/39 步骤通过，成功链接 `LongPet`；
- 未在本轮自动上传或重启开发板服务。

自动测试覆盖：

- V1 完整闭环、错误恢复、连续会话和历史裁剪；
- VAD 检出讲话与静音自动结束；
- PCM dBFS 计算；
- SSE 跨任意分片、正常结束和损坏 JSON；
- SentenceBuffer 分片标点、闭引号、无标点长句和 flush；
- LLM 尚未结束时第一句已开始 TTS/播放；
- 多段 TTS 和播放顺序；
- 单句 TTS 失败后队列恢复；
- Recognizing/Thinking/Speaking 取消；
- Speaking 时重新启动；
- 旧 session 的 LLM/TTS/Playback 回调不污染新 session；
- 正常历史写入、取消不写入和主动清空。

## 11. 板端快速测试

本轮没有自动覆盖或重启板端服务。交叉打包并替换 `/home/longpet/LongPet` 后：

```bash
install -o root -g longpet -m 0640 \
    /tmp/longpet-ai.ini /etc/longpet/ai.ini
systemctl restart longpet.service
journalctl -fu longpet.service
```

建议按顺序验证：

1. 短句：说完后无需点“我说完了”；
2. 带一秒停顿的长句：确认不会过早结束；
3. 长回答：观察文字逐步出现，第一句在完整回答结束前播放；
4. 多句回答：确认播放顺序；
5. Thinking 点击“停止”；
6. Speaking 点击“重新说话”；
7. 连续进行 5~10 轮，观察 USB 声卡是否被占用；
8. 断开 Wi-Fi 后重试，再恢复 Wi-Fi；
9. 检查 `Voice metrics` 并记录 `speech_latency_ms`。

## 12. 当前限制与后续版本

本轮按范围不实现：

- KWS；
- WebSocket/partial 流式 ASR；
- Provider 原生流式 TTS；
- 全双工抢话、AEC；
- Tool Calling 和 ReminderService 调用；
- 长期记忆；
- Provider fallback；
- 本地模型和完整离线助手。

当前 VAD 是能量阈值算法，强噪声、扬声器回采和距离变化需要按现场调节。当前“重新说话”依赖 UI，未来 KWS 只需复用 `startInteraction/restartInteraction/cancelInteraction`，不需要改写 Provider 或页面。
