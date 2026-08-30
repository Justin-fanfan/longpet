# LongPet AI Provider 拆分与阿里云百炼接入报告

日期：2026-08-30

项目：`D:\code_qt\longpet_main\longpet`

本报告描述联网语音交互 V1 的 Provider 拆分。本轮没有重做页面、录音播放或状态机；旧报告 `LongPet-Network-AI-Voice-V1-Report.md` 仍记录第一版闭环，本报告取代其中“单一 AI Gateway 配置和单一 Provider”的部分。

## 1. 完成结果

ASR、LLM、TTS 现在可以分别配置：

- Provider 类型；
- API Base URL；
- API Key；
- 模型；
- ASR 语言和 TTS voice。

三者可以来自不同厂商、不同服务器并使用不同密钥。页面和 `VoiceInteractionService` 不包含阿里云、OpenAI 或 URL 拼接逻辑。

当前实际实现：

| 能力 | `provider` | 实现 |
|---|---|---|
| ASR | `aliyun` / `dashscope` | 百炼 DashScope 原生非实时语音识别，WAV 以 Base64 Data URI 上传 |
| ASR | `openai-compatible` / `openai` | `POST audio/transcriptions` |
| LLM | `openai-compatible` / `openai` | `POST chat/completions` |
| LLM | `aliyun` / `dashscope` | 复用 OpenAI-compatible Chat Completions 协议，Base URL 仍需填写百炼 compatible-mode 地址 |
| TTS | `aliyun` / `dashscope` | 百炼原生非实时语音合成，解析音频 URL 后下载 |
| TTS | `openai-compatible` / `openai` | `POST audio/speech` |

## 2. 新调用链

```text
HomePage / ConversationPage
        │ UI signal / VoiceInteractionSnapshot
        ▼
AppController
        ▼
VoiceInteractionService
        ├── AsrProviderPort ──┬── AliyunAsrProvider
        │                    └── OpenAiAsrProvider
        ├── LlmProviderPort ───── OpenAiCompatibleLlmProvider
        ├── TtsProviderPort ──┬── AliyunTtsProvider
        │                    └── OpenAiTtsProvider
        ├── VoiceAudioPort ────── VoiceAudioAdapter
        └── MediaSessionCoordinator

Application
        ├── AiConfigRepository
        └── AiProviderFactory（集中选择三个实现）

各 HTTP Provider
        └── ProviderHttpClient（鉴权、超时、取消、HTTP/网络错误归一化）
```

`VoiceInteractionService` 仍只负责：

- `Idle → Recording → Recognizing → Thinking → Speaking → Idle`；
- 内存中的多轮上下文；
- ASR → LLM → TTS 顺序；
- `sessionId` 旧响应隔离；
- 统一取消和媒体资源释放。

## 3. 配置格式

默认配置文件仍为：

```text
/etc/longpet/ai.ini
```

完整的百炼模板见 `deploy/longpet-ai.ini.example`：

```ini
[asr]
provider=aliyun
api_base_url=https://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api/v1
api_key=replace-with-bailian-api-key
model=qwen-audio-3.0-asr-flash
language=zh

[llm]
provider=openai-compatible
api_base_url=https://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/compatible-mode/v1
api_key=replace-with-bailian-api-key
model=qwen-plus

[tts]
provider=aliyun
api_base_url=https://dashscope.aliyuncs.com/api/v1
api_key=replace-with-bailian-api-key
model=qwen3-tts-flash
voice=Cherry

[voice]
system_prompt=你是 LongPet，一位耐心、温和、回答简短的陪伴助手。请使用中文回答，避免使用复杂术语。
request_timeout_ms=30000
recording_maximum_ms=12000
history_turns=4
```

注意：

1. `{WorkspaceId}` 必须替换为百炼控制台中的业务空间 ID，不能保留花括号占位符；
2. 北京、新加坡等地域的 API Key 与域名不能混用；
3. TTS 的 voice 必须是所选模型支持的音色；
4. 不要把真实 API Key 提交到 Git；
5. 配置在 LongPet 启动时加载，改完后需要重启服务。

### 3.1 三家混用

`deploy/longpet-ai-mixed.ini.example` 展示以下组合：

```text
阿里云 ASR（A 厂 Key/URL）
  → OpenAI-compatible LLM（B 厂 Key/URL）
  → OpenAI-compatible TTS（C 厂 Key/URL）
```

三个 `api_base_url` 和 `api_key` 完全独立。示例中的 `vendor-b.example`、`vendor-c.example` 是占位地址，不能直接请求。

### 3.2 环境变量覆盖

配置文件路径：

```text
LONGPET_AI_CONFIG
```

ASR：

```text
LONGPET_ASR_PROVIDER
LONGPET_ASR_BASE_URL
LONGPET_ASR_API_KEY
LONGPET_ASR_MODEL
LONGPET_ASR_LANGUAGE
```

LLM：

```text
LONGPET_LLM_PROVIDER
LONGPET_LLM_BASE_URL
LONGPET_LLM_API_KEY
LONGPET_LLM_MODEL
```

TTS：

```text
LONGPET_TTS_PROVIDER
LONGPET_TTS_BASE_URL
LONGPET_TTS_API_KEY
LONGPET_TTS_MODEL
LONGPET_TTS_VOICE
```

公共语音配置：

```text
LONGPET_VOICE_SYSTEM_PROMPT
LONGPET_VOICE_REQUEST_TIMEOUT_MS
LONGPET_VOICE_RECORDING_MAXIMUM_MS
LONGPET_VOICE_HISTORY_TURNS
```

环境变量优先于 INI。为便于已有设备迁移，旧 `[ai]` 和旧的 `LONGPET_AI_*` 地址/密钥/模型环境变量仍可读取；同时存在新旧 INI 时，新分组优先并输出迁移提示日志。

## 4. 阿里云协议实现

### 4.1 ASR

当前 `qwen-audio-3.0-asr-flash` 请求：

```http
POST <asr.api_base_url>/services/aigc/multimodal-generation/generation
Authorization: Bearer <asr.api_key>
Content-Type: application/json
X-DashScope-SSE: disable
```

核心 JSON：

```json
{
  "model": "qwen-audio-3.0-asr-flash",
  "input": {
    "messages": [{
      "role": "user",
      "content": [{
        "type": "input_audio",
        "input_audio": {"data": "data:audio/wav;base64,..."}
      }]
    }]
  },
  "parameters": {
    "format": "wav",
    "sample_rate": "16000",
    "language_hints": ["zh"]
  }
}
```

识别文本读取 `output.text`。Provider 也兼容 `qwen3-asr-flash` 的 DashScope 消息格式与 `output.choices[0].message.content[].text` 响应，方便已有百炼模型迁移。

### 4.2 LLM

百炼 LLM 使用 OpenAI-compatible：

```http
POST <llm.api_base_url>/chat/completions
```

请求仍包含 `model`、`messages` 和 `stream=false`，读取 `choices[0].message.content`。system prompt 和历史消息由 Service 构造，Provider 不管理业务上下文。

### 4.3 TTS

`qwen3-tts-*` / `qwen-tts-*` 使用：

```http
POST <tts.api_base_url>/services/aigc/multimodal-generation/generation
```

`qwen-audio-*` / `cosyvoice*` 使用：

```http
POST <tts.api_base_url>/services/audio/tts/SpeechSynthesizer
```

非流式响应返回 `output.audio.url`。Provider 随后下载音频数据再交给 `VoiceAudioAdapter`。下载签名 URL 时不会转发 DashScope API Key，避免密钥泄露到 OSS 主机。

## 5. 统一错误和取消

Provider 向 Service 返回以下标准错误码：

```text
NetworkError
Timeout
Unauthorized
RateLimited
ServerError
InvalidResponse
EmptyResult
UnsupportedProvider
Cancelled
```

HTTP 映射：

- 401/403 → `Unauthorized`；
- 429 → `RateLimited`；
- 5xx → `ServerError`；
- 其他异常 HTTP/JSON → `InvalidResponse`；
- DNS、拒绝连接等 → `NetworkError`；
- 定时器中止 → `Timeout`。

UI 只显示普通用户能理解的短消息。日志保留 provider、标准错误码、HTTP status、API error code/message，不记录完整 API Key。

取消时 Service 会同时取消三个 Provider 和音频 Adapter。当前请求被 `abort`，所有回调继续携带 `sessionId`；旧 session 即使晚到，也不能更新页面、进入下一阶段或播放 TTS。

## 6. Windows 构建与测试

Qt 6.11.2 MSVC 示例：

```powershell
cmake -S . -B build `
  -DLONGPET_BUILD_TESTS=ON `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build --config Release --parallel

$env:PATH = "C:\Qt\6.11.2\msvc2022_64\bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
```

本轮实测：

- Windows Release 编译通过；
- CTest `LongPet.V02`：1/1 通过；
- QtTest：25 通过、0 失败、1 个截图测试因未配置截图目录而按设计跳过；
- 测试覆盖独立配置/环境变量、旧配置迁移、三个 OpenAI-compatible Provider、百炼原生 ASR、两类百炼 TTS endpoint 和音频二次下载、401/429/503、无网络、超时、取消、旧结果隔离、混用工厂及完整语音闭环。

自动化测试使用本地 HTTP stub，不消耗真实百炼额度。

## 7. LoongArch 构建结果

本轮在 WSL Ubuntu 24.04 + Buildroot SDK 上执行 Release 交叉构建，54/54 步骤通过。产物：

```text
ELF 64-bit LSB executable, LoongArch
interpreter /lib64/ld-linux-loongarch-lp64d.so.1
for GNU/Linux 6.6.0
```

## 8. 板端部署与快速验收

开发电脑：

```powershell
scp .\build-loongarch-video-signaling\LongPet root@10.240.178.51:/tmp/LongPet
scp .\deploy\longpet-ai.ini.example root@10.240.178.51:/tmp/ai.ini
```

先在 `/tmp/ai.ini` 中填入真实 WorkspaceId、三个 API Key/模型/voice，再安装：

```sh
systemctl stop longpet.service
install -o longpet -g longpet -m 0755 /tmp/LongPet /home/longpet/LongPet
install -d -o root -g longpet -m 0750 /etc/longpet
install -o root -g longpet -m 0640 /tmp/ai.ini /etc/longpet/ai.ini
systemctl start longpet.service
journalctl -u longpet.service -b -f
```

HTTPS 调用前确认：

```sh
date
test -r /etc/ssl/certs/ca-certificates.crt && echo CA_OK
```

快速验收：

1. 首页点击“陪我说话”；
2. 说一句短句后点击“我说完了”；
3. 确认依次显示正在识别、正在思考、正在生成语音、正在回答；
4. 确认识别文字、LLM 回答和 USB 扬声器播放均正常；
5. 连续完成三轮，确认没有 ALSA busy；
6. 在 ASR、LLM、TTS 阶段分别停止一次，确认不会稍后突然播报；
7. 临时断开 Wi-Fi，确认显示网络错误且下一轮可重试；
8. 用错误 Key 验证鉴权提示，再恢复正确 Key；
9. 检查日志中的 provider/HTTP/API code，同时确认没有输出完整密钥。

本轮没有直接替换板端程序，也没有使用真实百炼 Key 发起收费请求；最终云端额度、地域、模型授权、音色和板端 CA 证书仍需按上述步骤实测。

## 9. 当前未支持

- 流式 ASR、SSE LLM、流式 TTS；
- KWS、VAD、对话打断和 TTS Queue；
- Tool Calling / ReminderService 调用；
- 自动 Provider failover、负载均衡；
- 本地 ASR/TTS/LLM；
- 百炼异步长录音识别；
- 长期记忆和离线降级。

## 10. 新增 Provider 的最小步骤

以新增 `LoongsonAsrProvider` 为例：

1. 新增一个实现 `AsrProviderPort` 的 `.h/.cpp`；
2. 在实现内部完成龙芯服务自己的请求体、响应解析和错误转换；
3. 网络请求可复用 `ProviderHttpClient`，也可使用完全不同的传输实现；
4. 在 `AiProviderFactory::createAsr()` 增加一个 `provider` 名称映射；
5. 将新源文件加入 `CMakeLists.txt`；
6. 为成功、错误、超时和取消补测试；
7. 增加配置示例。

不需要修改 `VoiceInteractionService`、`ConversationPage` 或 `HomePage`。新增 LLM/TTS Provider 分别遵循同样步骤并实现对应 Port。

## 11. 官方协议依据

- [阿里云百炼：Qwen-Audio-3.0-ASR-Flash/Fun-ASR-Flash 非实时语音识别 API](https://help.aliyun.com/zh/model-studio/non-real-time-speech-recognition-for-fun-asr-flash)
- [阿里云百炼：Qwen-ASR API 参考](https://help.aliyun.com/zh/model-studio/qwen-asr-api-reference)
- [阿里云百炼：OpenAI-compatible 模型调用](https://help.aliyun.com/en/model-studio/compatibility-of-openai-with-dashscope)
- [阿里云百炼：非实时语音合成](https://help.aliyun.com/zh/model-studio/non-realtime-tts-user-guide)
- [阿里云百炼：Qwen-TTS API 参考](https://help.aliyun.com/en/model-studio/qwen-tts-api)
