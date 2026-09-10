# LongPet KWS / VAD CPU 与语音延迟检查报告

日期：2026-09-11  
分支：`vision-v1.1`  
检查基线：`d2d5baf95e13241bd964cb39305e90c202d13a1f`  
开发板：`192.168.137.32`（本轮仅只读检查，未部署、未改配置）

## 1. 结论

本轮确认了三个相互独立的问题：

1. **持续高 CPU 的主体是 KWS Python/ONNX，不是 LongPet UI 或 Vision。** 开发板为单核，检查期间旧版
   `longpet_kws_bridge.py` 长期约占 `23%~31% CPU`，`LongPet` 约占 `0.8%~1.3%`。服务没有配置
   `LONGPET_VISION_*`，因此本次高占用不是视觉检测造成的。
2. **旧 KWS VAD 可能被 USB 麦克风启动底噪锁死。** 旧算法只有在当前帧已被判定为静音时才学习噪声；
   如果启动底噪一开始就高于 `-60 dBFS`，该帧立即被当作人声，噪声基线再也无法上调，ONNX 便持续处理全部音频。
3. **语音交互 12 秒才结束是另一套 VAD 门限不适配。** 板端 `/etc/longpet/ai.ini` 没有显式的
   `[voice]` VAD 参数，旧程序使用固定 `-42 dBFS`。日志中的有效讲话电平多次只在约
   `-41.1~-37.9 dBFS`，与门限过于接近，导致部分会话一直没有稳定确认讲话，最终只能触发
   `recording_maximum_reached`。

KWS VAD 与语音交互 VAD 原来不是同一个实现：前者是 Python 持续门控，后者是 C++ 单轮句尾检测。
本轮将二者统一为相同的**自适应判定语义**，但没有强行做成一个跨进程对象。这是更合适的统一层级：

- KWS VAD：安静时阻止音频进入 ONNX，主要目标是降低持续 CPU；
- 语音交互 VAD：用户讲话后检测连续静音，主要目标是自动结束录音；
- 两者都使用“滚动背景噪声低分位 + 相对倍率 + 绝对灵敏度下限”；
- Python KWS 与 C++ 语音交互仍由 `MediaSessionCoordinator` 仲裁麦克风，不并发占用设备。

## 2. 现有调用链

### 2.1 KWS

```text
Application
  → VoiceCommandDispatcher
  → KwsProcessAdapter
  → longpet_kws_bridge.py
  → Capture(arecord / sounddevice)
  → Python EnergyVad
  → FsmnKws / ONNX（仅有声片段）
  → keyword JSON event
  → VoiceCommandDispatcher
  → VoiceInteractionService / 本地命令 Service
```

### 2.2 一次联网语音交互

```text
Homepage / KWS
  → AppController / VoiceCommandDispatcher
  → VoiceInteractionService
  → MediaSessionCoordinator 暂停 KWS 并取得麦克风
  → VoiceAudioAdapter 固定 20 ms 音频帧
  → C++ VoiceActivityDetector
  → ASR Provider → LLM Provider → TTS Provider
  → VoiceAudioAdapter 播放
  → MediaSessionCoordinator 释放媒体并恢复 KWS
```

页面没有直接访问 ALSA、ONNX 或网络 API，修改仍符合
`Application → Controller/Dispatcher → Service → Adapter/Provider → UI` 分层。

## 3. 板端只读证据

检查时服务运行的是修改前版本，关键观测如下：

- CPU 核心数：1；
- KWS 后端：`arecord`，输入 `48000 Hz`，VAD 参数 `-60 dBFS / 2.5`；
- 最后一次采样：KWS Python `23.0%`，LongPet `0.8%`；更早的连续采样中 KWS 常见 `27%~31%`；
- Vision 未在服务环境中启用；
- 多次语音日志到约 11.3 秒才出现 `recording_maximum_reached`；另一些会话能正常检测并结束，
  说明录音链路可用，问题集中在门限稳定性而不是麦克风完全无数据；
- 网络时延波动明显：一次 DashScope DNS 阶段约 `11.106 s`，后续同主机约 `0.202/0.216 s`；
  另一 LLM 地址首次连接约 `2.568 s`，后续约 `0.257/0.183 s`；
- 历史会话中 ASR 总耗时约 `0.6~25.4 s`、LLM TTFT 约 `0.35~11.6 s`、
  TTS 约 `0.97 s` 到 `30 s` 超时，证明“语音交互变慢”中有相当一部分是 DNS、连接或 Provider 推理波动。

因此，CPU 与句尾检测可以在本地代码中修复；公网服务端的长尾延迟只能缓解冷连接，不能由 VAD 消除。

## 4. 代码修改

### 4.1 KWS 静音门控和 CPU

- `components/longpet-kws/src/longpet_kws/vad.py`
  - 使用最近 1 秒能量历史的第 20 百分位估计稳定背景噪声；
  - 未进入 active 前持续更新噪声，不再要求当前帧先被旧门限判为静音；
  - 实际门限为 `max(absolute_threshold, noise_floor × noise_ratio)`；
  - 暴露 `noise_floor_db`、`effective_threshold_db` 供板端诊断。
- `components/longpet-kws/src/longpet_kws/cli.py`
  - 对板端固定的 `48 kHz → 16 kHz` 整数倍率场景使用直接抽取，避免每 100 ms 构造两组插值坐标；
  - 未改关键词、FSMN 缓存和 CTC 判定逻辑，避免引入声学行为变化。
- `deploy/kws/longpet_kws_bridge.py`
  - 每 30 秒输出 `vad_stats`：音频窗口、ONNX 输入占比、噪声、实际门限、语音启动次数和 active 状态；
  - KWS 暂停/恢复后重置统计窗口。
- `src/platform/KwsProcessAdapter.cpp`
  - 将 `vad_stats` 记为单行 `KWS VAD ...` 日志。

VAD 的节能边界需要明确：它降低的是**安静时 ONNX 推理占比**，不会让采集进程、Python 主循环、
48 kHz 重采样和能量计算变成零开销；有人持续讲话时 ONNX 恢复运行，CPU 上升属于预期。

### 4.2 语音交互句尾检测

- `src/model/AiModels.*`、`src/data/AiConfigRepository.cpp`
  - 新增 `[voice] vad_noise_ratio` / `LONGPET_VOICE_VAD_NOISE_RATIO`；
  - 默认绝对门限从 `-42` 改为 `-55 dBFS`，默认相对倍率为 `2.0`；
  - 增加 `1.0~20.0` 配置校验。
- `src/platform/VoiceAudioAdapter.*`
  - 不再把大小不定的 `QProcess::readyRead` 数据块当作 VAD 帧；
  - 始终按 `16 kHz / S16_LE / mono` 的固定 20 ms（640 bytes）帧上报；
  - RMS 计算先移除 DC 偏置，与 Python KWS 的能量语义一致。
- `src/services/VoiceActivityDetector.*`
  - 使用最近至少 2 秒的低分位噪声历史生成自适应门限；
  - 在背景噪声基线稳定后可回看缓存，兼容“录音一开始用户已经在讲话”；
  - 检出人声后冻结本句门限，连续静音达到配置时间即结束。
- `src/services/VoiceInteractionService.cpp`
  - 日志新增 `level_db / noise_db / threshold_db`，可以区分输入太小、底噪过高和门限配置不当。

### 4.3 网络冷连接缓解

- `src/services/VoiceInteractionPorts.h`
  - Provider Port 新增默认空实现 `prepare()`，不把具体厂商逻辑放进 Service。
- `src/platform/ProviderHttpClient.*`、`OpenAiCompatibleProviders.*`、`AliyunProviders.*`
  - 使用 Qt `QNetworkAccessManager` 的异步预连接；
  - 只记录 Provider、主机和端口，不记录 API Key 或请求正文。
- `src/services/VoiceInteractionService.cpp`
  - 用户开始本轮交互且成功取得媒体所有权后，同时预热 ASR/LLM/TTS 连接；
  - DNS/TCP/TLS 可以与用户录音并行，预连接失败不会阻塞录音，也不会绕过原有请求超时和错误处理。

该优化可减少首次 DNS/TLS 冷启动，但不能修复 Provider 自身排队、推理慢或公网丢包。最终仍应以
`Voice metrics` 中的 ASR、TTFT、TTS 分阶段耗时判断。

### 4.4 配置和测试

- 更新 `deploy/longpet-ai.ini.example`、`deploy/longpet-ai-mixed.ini.example`、`deploy/配置说明.md`；
- 更新 `components/longpet-kws/README.md`；
- `tests/V02Test.cpp` 增加 C++ 自适应 VAD、稳定高底噪、开头讲话、配置解析和 DC 偏置测试；
- `tests/test_kws_bridge.py` 增加真实 Python `EnergyVad` 的稳定噪声门控和讲话释放测试。

## 5. 本机验证结果

### 5.1 Release 构建

```text
D:\Qt\Tools\CMake_64\bin\cmake.exe \
  --build build-kws-vision-release --config Release -j 4
```

结果：成功生成 `LongPet.exe`、`LongPetV02Tests.exe`、`LongPetVisionV1Tests.exe`。
构建中仅有原有未使用函数和 `AiChatMessage` 聚合初始化警告，本轮代码无编译错误。

### 5.2 CTest

```text
LongPet.V02        Passed  11.86 sec
LongPet.KwsBridge  Passed   0.88 sec
LongPet.VisionV1   Passed   1.53 sec
100% tests passed, 0 tests failed out of 3
Total Test time: 14.31 sec
```

Python bridge 套件共 8 个用例，全部通过。`git diff --check` 通过。

本轮按要求没有交叉编译、没有替换 `/home/longpet/LongPet`，也没有在板端安装新 Python 文件；
板端 CPU 改善幅度和真实麦克风门限仍需使用新版本实测。

## 6. 板端部署时不能遗漏的文件

只替换编译后的 `LongPet` **不能验证 KWS CPU 修复**。至少还要同步以下运行时文件：

```text
deploy/kws/longpet_kws_bridge.py
  → /home/longpet/longpet-kws/longpet_kws_bridge.py

components/longpet-kws/src/longpet_kws/vad.py
  → /home/longpet/longpet-kws/upstream/src/longpet_kws/vad.py

components/longpet-kws/src/longpet_kws/cli.py
  → /home/longpet/longpet-kws/upstream/src/longpet_kws/cli.py
```

以及新编译程序：

```text
LongPet → /home/longpet/LongPet
```

建议在 `/etc/longpet/ai.ini` 明确保留以下配置，避免以后编译默认值变化导致测试不可复现：

```ini
[voice]
vad_enabled=true
vad_threshold_db=-55.0
vad_noise_ratio=2.0
vad_silence_timeout_ms=900
recording_minimum_ms=600
vad_minimum_speech_ms=160
recording_maximum_ms=12000

[kws]
vad_threshold_db=-60.0
vad_noise_ratio=2.5
```

## 7. 板端快速验收

重启服务并确认新 bridge 已就绪：

```bash
systemctl restart longpet.service
journalctl -b -u longpet.service --no-pager | grep -E 'KWS ready|KWS VAD|KWS error'
```

### 7.1 安静环境 CPU

保持 1~2 分钟不讲话，查看至少两条 30 秒统计：

```bash
journalctl -f -u longpet.service | grep 'KWS VAD'
```

期望：

- `active=false`；
- `inference_ratio` 明显低于 `100%`，安静窗口通常应接近 `0%`；
- KWS Python CPU 较旧版 `23%~31%` 明显下降。

另开终端观察进程：

```bash
ps -eo pid,comm,pcpu,pmem,args --sort=-pcpu | head -n 8
```

`ps %CPU` 是进程自启动以来的平均值，不会在升级后瞬间下降；判断即时效果时应连续采样，或重启服务后再测。

### 7.2 KWS 唤醒

安静 5 秒后分别说关键词，确认 `inference_ratio` 在讲话窗口短暂上升且关键词仍能识别。讲话结束后下一窗口应回落。
如果一直 active：

```ini
[kws]
vad_noise_ratio=3.0
```

如果轻声完全触发不了，逐步降到 `2.2` 或 `2.0`，每次只改一个参数。

### 7.3 语音交互句尾

连续测试短句、停顿句、轻声和正常音量：

```bash
journalctl -f -u longpet.service | \
  grep -E 'vad_speech_detected|vad_end_of_speech|recording_maximum_reached|Voice metrics'
```

期望：

- 讲话后出现 `vad_speech_detected`；
- 说完约 `900 ms` 后出现 `vad_end_of_speech`；
- 正常会话不再依赖 `recording_maximum_reached`；
- 日志中讲话 `level_db` 应高于 `threshold_db`。

轻声检测不到时先把 `[voice] vad_noise_ratio` 从 `2.0` 降到 `1.8`；环境噪声被当作讲话时提高到
`2.5`。不要优先把绝对门限调回 `-42`，否则会重新把正常讲话卡在门限附近。

### 7.4 交互延迟

```bash
journalctl -b -u longpet.service --no-pager | \
  grep -E 'AI network preconnect|Voice metrics session='
```

至少连续做 5 轮，对比 `asr_ms`、`llm_ttft_ms`、`first_tts_ms` 和 `speech_latency_ms`：

- 只有第一轮慢、后续快：多为 DNS/TCP/TLS 冷连接；
- 多轮 ASR 都慢：检查 ASR Provider 或上传链路；
- `llm_ttft_ms` 单独高：LLM 排队/推理慢；
- `first_tts_ms` 单独高：TTS 服务慢；
- 各阶段随机出现十几秒长尾：优先检查热点网络、DNS 和公网 Provider，不应继续调 VAD。

## 8. 已知限制

- 自适应能量 VAD 不能区分人声与同等响度的电视、音乐或机械噪声；这是轻量算法的边界；
- 本轮没有引入神经网络 VAD，以避免在单核 LoongArch 板上增加持续负载；
- KWS 持续讲话时仍需运行 ONNX，不能期望任何场景都接近零 CPU；
- `48 kHz → 16 kHz` 快速整数抽取没有新增重型重采样依赖，适合当前语音频段，但不是高保真音频转换器；
- 预连接是 best-effort，公网服务端推理长尾仍然存在；
- 新程序和 Python KWS 文件尚未上板，最终数值需以上述板端验收结果为准。

## 9. 提交状态

本轮未创建 Git 提交。当前工作区还包含此前 KWS → `vision-v1.1` 迁移的未提交修改，不能仅凭整个
`git diff` 将本轮优化误认为独立补丁。请完成板端验证后再确认提交范围。
