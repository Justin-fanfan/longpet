# LongPet 第一版联网大模型语音交互实施报告

日期：2026-08-30

项目：`D:\code_qt\longpet_main\longpet`

目标链路：麦克风录音 → ASR → LLM → TTS → USB 扬声器

## 1. 完成范围

本轮完成按键式、非流式的第一版闭环。用户在 HomePage 点击“陪我说话”后进入 ConversationPage：

```text
Idle
  -> Recording（点击“我说完了”，最长录音时间到达时也会自动结束）
  -> Recognizing（上传 WAV 并等待 ASR）
  -> Thinking（请求 LLM；随后仍在该状态请求 TTS）
  -> Speaking（板端播放返回音频）
  -> Idle
```

ConversationPage 显示当前阶段、识别文本、模型回答和简短错误；录音阶段可以手动结束，其余阶段可以停止。整个网络和音频流程均为 Qt 信号驱动，不在 UI 线程等待 HTTP 或 GStreamer 结束。

第一版没有实现流式 ASR/LLM/TTS、VAD、KWS、Tool Calling 和离线推理，这些边界保持在独立 Port 后面，可在后续替换。

## 2. 分层与调用链

```text
HomePage / ConversationPage
        │ UI signal / VoiceInteractionSnapshot
        ▼
AppController
        ▼
VoiceInteractionService
        ├── AiProviderPort
        │      └── OpenAiCompatibleProvider
        │              └── QNetworkAccessManager
        ├── VoiceAudioPort
        │      └── VoiceAudioAdapter
        │              └── QProcess + GStreamer + ALSA
        └── MediaSessionCoordinator
               └── 与 VideoCallService 互斥占用音频媒体资源

Application
  └── AiConfigRepository -> /etc/longpet/ai.ini
```

- 页面不访问网络、ALSA、GStreamer 或配置文件；
- `AppController` 只连接 UI、状态快照和导航；
- `VoiceInteractionService` 拥有状态机、多轮上下文、取消和资源生命周期；
- `AiProviderPort` 隔离具体云服务协议；
- `VoiceAudioPort` 隔离板端音频实现；
- `MediaSessionCoordinator` 防止视频通话和 AI 语音同时打开 USB 麦克风/扬声器；
- `Application` 负责读取配置并注入实现。

## 3. AI 配置

模板为 `deploy/longpet-ai.ini.example`。板端默认读取：

```text
/etc/longpet/ai.ini
```

也可设置：

```ini
Environment="LONGPET_AI_CONFIG=/custom/path/ai.ini"
```

配置格式：

```ini
[ai]
api_base_url=https://api.example.com/v1
api_key=replace-with-real-token
asr_model=whisper-1
llm_model=your-chat-model
tts_model=your-tts-model
tts_voice=alloy
language=zh
system_prompt=你是 LongPet，一位耐心、温和、回答简短的陪伴助手。请使用中文回答。
request_timeout_ms=30000
recording_maximum_ms=12000
history_turns=4
```

`api_base_url` 是公共 API 前缀，程序分别追加：

```text
audio/transcriptions
chat/completions
audio/speech
```

因此 OpenAI-compatible 服务通常配置为 `https://host/v1`。API Key 可以为空，以支持无鉴权的局域网龙芯 AI Server。

以下环境变量会覆盖 INI，适合 systemd secret/drop-in 或临时切换服务：

```text
LONGPET_AI_BASE_URL
LONGPET_AI_API_KEY
LONGPET_AI_ASR_MODEL
LONGPET_AI_LLM_MODEL
LONGPET_AI_TTS_MODEL
LONGPET_AI_TTS_VOICE
```

配置文件不存在或配置无效不会阻止 LongPet 启动；点击“陪我说话”时会显示具体的缺失字段。配置在进程启动时读取，修改后需重启 `longpet.service`。

## 4. OpenAI-compatible 请求

### ASR

```http
POST <base>/audio/transcriptions
Authorization: Bearer <api_key>
Content-Type: multipart/form-data
```

表单字段：

- `model`：ASR 模型；
- `language`：默认 `zh`；
- `response_format=json`；
- `file`：`recording.wav`，PCM S16_LE / 16 kHz / mono。

预期响应：

```json
{"text":"识别出的文字"}
```

### LLM

```http
POST <base>/chat/completions
Content-Type: application/json
```

```json
{
  "model": "your-chat-model",
  "messages": [
    {"role":"system","content":"..."},
    {"role":"user","content":"..."}
  ],
  "stream": false
}
```

预期读取 `choices[0].message.content`。Service 最多保留 `history_turns` 轮 user/assistant 对话；历史只存在内存中，重启后清空。

### TTS

```http
POST <base>/audio/speech
Accept: audio/wav
Content-Type: application/json
```

```json
{
  "model": "your-tts-model",
  "voice": "alloy",
  "input": "LLM 回答",
  "response_format": "wav"
}
```

Provider 把非空音频交给音频 Adapter。播放端使用 GStreamer `decodebin`，除 WAV 外也可以接受目标板已有解码插件支持的音频容器；为保证可移植性，Gateway 应优先遵守 WAV 请求。

## 5. 板端音频

录音管线等价于：

```text
gst-launch-1.0 -q
  alsasrc device=plughw:CARD=Device,DEV=0 provide-clock=false !
  audioconvert ! audioresample !
  audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved !
  fdsink fd=1 sync=false
```

Adapter 在录音结束后为 PCM 添加标准 44-byte RIFF/WAV 头，再交给 ASR。

播放管线等价于：

```text
fdsrc fd=0 ! decodebin ! audioconvert ! audioresample !
alsasink device=plughw:CARD=Device,DEV=0 sync=false
```

设备可以分别覆盖：

```text
LONGPET_AI_CAPTURE_DEVICE
LONGPET_AI_PLAYBACK_DEVICE
```

未设置时先复用 `LONGPET_CALL_CAPTURE_DEVICE` / `LONGPET_CALL_PLAYBACK_DEVICE`，最后才使用 USB 声卡默认值。音量 Adapter 只操作 mixer，不长期占用 PCM。

## 6. 取消、超时和旧结果隔离

每次交互生成单调递增的 `sessionId`。所有音频和 Provider 回调都携带它，并且 Service 同时检查当前状态：

```text
取消 session 7
  -> 停止录音/播放
  -> abort 当前 QNetworkReply
  -> 释放媒体会话
  -> 回到 Idle

稍后到达的 session 7 回调
  -> session/state 不匹配
  -> 丢弃，不更新 UI、不播放
```

录音有最长时限；每个 API 请求有独立超时定时器。HTTP 非 2xx、DNS/连接错误、超时、异常 JSON、空 ASR/LLM/TTS、GStreamer 启动或播放失败都会释放资源并进入可重试的 Failed 页面，不会退出主程序。

## 7. 与视频通话的互斥

`VoiceInteractionService` 与 `VideoCallService` 都通过 `MediaSessionCoordinator` 获取媒体所有权：

- 正在视频/语音通话时点击“陪我说话”，返回“设备正在通话”；
- 正在 AI 语音交互时，家属端发起通话会得到 busy；
- 正常完成、失败、取消和析构都会释放所有权；
- 音频进程退出后才允许下一次打开，避免 ALSA `Device or resource busy`。

这也是未来 KWS 接入的暂停边界：监听 `VoiceInteractionService::activityChanged(bool)` 和现有 `VideoCallService::callActivityChanged(bool)` 即可。

## 8. 安装配置

开发电脑上传：

```powershell
scp .\deploy\longpet-ai.ini.example root@10.240.178.51:/tmp/longpet-ai.ini
```

板端编辑 `/tmp/longpet-ai.ini` 后安装：

```sh
install -d -o root -g longpet -m 0750 /etc/longpet
install -o root -g longpet -m 0640 /tmp/longpet-ai.ini /etc/longpet/ai.ini
systemctl daemon-reload
systemctl restart longpet.service
```

不要把真实 API Key 提交到 Git。若使用 HTTPS，目标板 rootfs 必须有正确 CA 证书和系统时间；局域网自建服务第一版可以用 HTTP，但不要暴露到公网。

## 9. 构建和自动化测试

本轮验证：

- Windows Qt 6.11.2 MSVC Release：编译、MOC、链接通过；
- CTest：`LongPet.V02` 1/1 通过；
- LoongArch Buildroot SDK Release：50/50 构建步骤通过；
- 交叉产物确认为 LoongArch64 ELF，动态链接器 `/lib64/ld-linux-loongarch-lp64d.so.1`。

自动化测试覆盖：

1. INI 配置读取、校验和 WAV 封装；
2. Recording → ASR → LLM → TTS → Speaking → Idle 完整闭环；
3. 真实 Qt HTTP 请求到本地 OpenAI-compatible stub 的完整三接口闭环；
4. system prompt 和两轮对话上下文；
5. ASR、LLM、TTS、播放分别失败；
6. HTTP 503、异常 JSON、请求超时和离线连接；
7. 任意阶段取消及旧 session 回调隔离；
8. 连续启动多次语音交互；
9. 视频通话与 AI 语音媒体互斥；
10. HomePage → ConversationPage 导航和页面按钮语义。

当前没有可用的真实 Gateway 地址/API Key，且本轮检查时板端 SSH 超时，因此没有声称已完成真实云模型和板端扬声器的最终联调。自动化闭环使用本机 HTTP stub 返回 OpenAI-compatible 数据，LoongArch 侧已完成编译验证。

## 10. 快速板端验收

先检查插件和声卡：

```sh
gst-inspect-1.0 alsasrc
gst-inspect-1.0 alsasink
gst-inspect-1.0 decodebin
gst-inspect-1.0 audioconvert
gst-inspect-1.0 audioresample
cat /proc/asound/cards
```

实时日志：

```sh
journalctl -u longpet.service -b -f
```

操作步骤：

1. 点击“陪我说话”；
2. 对 USB 麦克风说一句短句；
3. 点击“我说完了”；
4. 观察“正在识别 → 正在思考 → 正在生成语音 → 正在回答”；
5. 确认识别文本和回答显示，USB 扬声器播放回答；
6. 分别在识别、思考、播放阶段点“停止”；
7. 连续完成三次对话，检查无 ALSA busy；
8. 断开 Wi-Fi 再试，确认显示网络错误并可再次点击；
9. AI 交互期间从家属端呼叫，确认返回 busy；
10. 完成/停止后执行 `ps -ef | grep gst-launch`，不应残留本次语音进程。

## 11. 第一版限制与扩展点

尚未实现：

- VAD 自动检测说话结束；
- KWS 唤醒和 KWS 暂停/恢复实现；
- SSE 流式 LLM、partial ASR、流式 TTS；
- 播放中打断后立即开始新一轮的 barge-in；
- TTS 分句队列和音视频同步；
- Tool Calling / ReminderService 调用；
- 历史持久化、长期记忆和内容安全策略；
- 本地、公网、龙芯 AI Server 自动切换；
- 断网离线 ASR/TTS/LLM。

扩展时应新增 `AiProviderPort` 或 `VoiceAudioPort` 实现，不需要修改 ConversationPage；SSE 可以在现有 Provider 增加增量信号，VAD/KWS 可以通过音频 Port 和 `activityChanged` 接入。
