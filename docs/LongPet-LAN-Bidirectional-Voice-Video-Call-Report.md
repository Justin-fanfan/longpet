# LongPet 局域网双向语音 / 视频通话实施报告

日期：2026-08-29  
LongPet 目录：`D:\code_qt\longpet_main\longpet`  
家属端目录：`D:\code\family-desktop`  
目标板：`10.240.178.51`

## 1. 本轮结论

上一版只完成 FamilyLink HTTP 呼叫信令，`connected` 后没有媒体 Adapter，API 的 `mediaReady` 也永久写死为 `false`，因此实测一直停在“信令连接成功，等待媒体通道”。本轮补齐了从 UI、Controller、Service、平台 Adapter 到摄像头/USB 音频的完整纵向链路。

第一版采用目标板报告建议的轻量路线：

```text
LongPet USB Camera MJPEG -> GStreamer/V4L2 -> JPEG 原帧 -> Qt WebSocket
Windows Camera -> Canvas 等比裁切 -> JPEG -> WebSocket -> LongPet QImage

LongPet USB Mic/Speaker <-> PCM S16_LE / 16 kHz / Mono <-> Windows 默认音频设备
```

目标板不解码再重编码自己的摄像头画面，优化后实际发送约 10 FPS。电脑发往板端使用 480×360、8 FPS，板端另有约 8 FPS 解码硬上限。视频堵塞时只保留最新帧；音频使用有界低延迟缓冲。

## 2. 分层与调用链

LongPet 保持现有分层：

```text
Application
  -> AppController / FamilyLinkController
  -> VideoCallService
  -> VideoCallMediaPort / CallPromptPlayerPort
  -> VideoCallMediaAdapter / CallPromptPlayerAdapter
  -> Qt WebSocket + GStreamer(V4L2/ALSA) / aplay
  -> VideoCallPage
```

- `Application` 创建并注入 Service 与两个 Adapter；
- `FamilyLinkController` 只解析/输出 HTTP JSON，不访问摄像头和 ALSA；
- `VideoCallService` 是状态机和资源生命周期唯一所有者；
- `VideoCallPage` 只接收快照与 `QImage`，不访问网络、硬件或平台 API；
- `AppController` 负责家属呼入时切页、结束后回首页，并在通话期间继续禁用 15 秒页面超时。

媒体协议位于 `MediaFrameProtocol`，Service 只依赖 `VideoCallMediaPort` 抽象，便于单元测试和以后替换成 Opus/WebRTC。

## 3. 呼叫状态机

| 状态 | 说明 |
|---|---|
| `idle` | 无通话 |
| `outgoing_ringing` | LongPet 首页主动视频呼叫，等待家属接听 |
| `notifying_device` | 家属端呼叫已鉴权，板端正在播放一次来电提示 |
| `connecting_media` | 提示音结束，正在启动 USB 音频与媒体鉴权 |
| `connected` | WebSocket 已鉴权且音频进程就绪，`mediaReady=true` |
| `rejected` | 家属端拒绝 LongPet 主动呼叫 |
| `ended` | 正常取消/挂断 |
| `failed` | 摄像头、麦克风、扬声器、权限或网络失败 |

设备活跃状态包括 ringing/notifying/connecting/connected。家属端再次 `POST /api/v1/video-call` 会得到 HTTP 409 `DEVICE_BUSY`，且不会播放提示音。

## 4. 家属端主动呼叫与提示音

家属端调用：

```http
POST /api/v1/video-call
Authorization: Bearer <token>
Content-Type: application/json

{ "mode": "voice" }
```

视频模式使用 `:/sounds/zh_video_call.wav`，语音模式使用 `:/sounds/zh_voice_call.wav`。两个 WAV 已加入 `resources.qrc`，随可执行文件部署，不需要另拷贝声音目录。

顺序保证：

1. HTTP 鉴权与 busy 检查成功；
2. 视频模式可启动摄像头并监听媒体端口，语音模式只监听端口；
3. `aplay` 播放一次提示音；
4. 提示音结束后状态进入 `connecting_media`；
5. 此时才启动板端麦克风和双向音频，提示音不会被采集发送；
6. WebSocket 鉴权且音频进程就绪后进入 `connected`。

提示音期间收到 `hangup` 会终止 `aplay` 并释放预热资源。资源缺失、`aplay` 启动失败或异常退出会写明确日志，然后继续自动建链，不会永久卡住。

## 5. 媒体协议

默认监听 `0.0.0.0:8788`，仅在活跃通话期间存在。可用 `LONGPET_MEDIA_ADDRESS` 和 `LONGPET_MEDIA_PORT` 覆盖。家属端从已配置 FamilyLink URL 的 host 和快照 `mediaPort` 推导地址，源码没有写死 `10.240.178.51`。

每个 WebSocket Binary Message 含一个 24-byte 大端帧头：

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `LPMF` magic |
| 4 | 1 | protocol version（当前 1） |
| 5 | 1 | stream type |
| 6 | 2 | flags |
| 8 | 4 | sequence |
| 12 | 8 | Unix epoch timestamp（微秒） |
| 20 | 4 | payload length |
| 24 | N | payload |

流类型 1–5 分别为板端视频、家属端视频、板端音频、家属端音频、JSON 控制。payload 上限 2 MiB。第一帧必须携带当前 `callId + mediaToken` 的 Control 鉴权消息。

## 6. 媒体实现与积压控制

### 6.1 2026-08-30 板端性能修正

首轮上板时视频通话出现 LongPet CPU 100% 和明显卡顿。热点在板端 GUI 进程：每秒约 15 次解码家属端 640×480 JPEG，并对每帧做平滑满屏缩放；同时还会周期性解码板端自己的摄像头 JPEG，仅用于右上角本地预览。这些工作都发生在资源有限的板端，语音通话稳定也进一步说明瓶颈不是 PCM 音频链路。

本次采用非对称媒体参数，把计算量留给 Windows：

- 家属端发往 LongPet：由 640×480 / 约 15 FPS 调整为 480×360 / 8 FPS，JPEG quality 由 0.68 调整为 0.60；
- LongPet 接收：用单调时钟设置 120 ms 最小解码间隔，即使旧版家属端发送更快也最多约 8.3 FPS；
- LongPet 摄像头：仍直接转发相机输出的 MJPEG，不做重编码，发送由约 15 FPS 调整为约 10 FPS；
- 删除 LongPet 本地小窗及其本机 JPEG 解码链路；家属端仍可看到 LongPet 摄像头；
- 满屏绘制继续保持等比居中裁切，但关闭高开销的平滑缩放提示；视频积压仍只保留最新帧。

按解码像素吞吐量估算，仅“家属端视频解码”就由约 4.61 Mpx/s 降至约 1.38 Mpx/s，下降约 70%；本地预览解码则完全消失。最终 CPU 数字仍必须以新产物上板后的 `top` 数据为准。

板端摄像头子进程等价于：

```text
gst-launch-1.0 -q v4l2src device=/dev/video0 !
  image/jpeg,width=640,height=480,framerate=30/1 ! fdsink fd=1 sync=false
```

Adapter 解析 JPEG SOI/EOI 并每三帧发送一帧，约 10 FPS。不对板端 JPEG 重编码。WebSocket 待发送量超过 256 KiB 时不继续排队，只覆盖一个“最新待发帧”。接收家属视频只保留最新待解码帧，并用单调时钟限制为约 8 FPS；显示使用快速等比铺满裁切，不拉伸。板端不再解码自己的摄像头 JPEG 做本地小窗预览。

音频格式为 PCM S16_LE、16 kHz、mono、每包 20 ms/640 bytes。板端播放队列 3 包起播，最多保留 12 包，过量时丢弃旧包；GStreamer queue 另限制到 200 ms 并启用 downstream leaky。家属端播放目标缓冲约 60–250 ms。

默认硬件环境变量：

```ini
LONGPET_CALL_CAMERA_DEVICE=/dev/video0
LONGPET_CALL_CAPTURE_DEVICE=plughw:CARD=Device,DEV=0
LONGPET_CALL_PLAYBACK_DEVICE=plughw:CARD=Device,DEV=0
LONGPET_MEDIA_ADDRESS=0.0.0.0
LONGPET_MEDIA_PORT=8788
```

全部都有上述默认值，正常部署可以不写。若 USB 声卡名称变化，再在 `longpet.service` 的 `[Service]` 中加入对应 `Environment=`。

## 7. LongPet 页面

- 视频：远端画面覆盖整个页面，按比例居中裁切；等待远端视频时为纯黑背景；不再显示板端本地预览；状态和控制均为覆盖层，不压缩视频区域；
- 语音：使用项目统一的 `#121210` 背景，`PetFaceWidget(PetExpression::Speaking)` 像 CompanionPage 一样铺满舞台；
- 双模式均显示家属端、状态和时长；
- connected 后挂断控制默认隐藏，触屏显示醒目挂断按钮，4 秒无操作自动隐藏；
- 挂断、失败或网络中断后自动返回首页；
- 首页原“视频通话”入口仍保留并发起 LongPet -> 家属端视频呼叫；今日关怀进入提醒设置的入口未改动。

## 8. 生命周期与 KWS 预留

摄像头、麦克风、扬声器、媒体端口和提示音都只在活跃呼叫期间创建。正常挂断、取消、拒绝、媒体进程退出、WebSocket 断开和 Service 析构都调用同一释放路径：停止计时器、关闭 socket/server、终止 GStreamer/aplay、清空音视频队列和帧缓存。

Service 新增 `callActivityChanged(bool)` 信号。当前 main 尚未接入 KWS，因此没有伪造暂停逻辑；未来 KWS Service 只需在 `true` 暂停独占麦克风，在 `false` 恢复。

## 9. 构建与验证结果

### Windows LongPet

- Qt 6.11.2 MSVC Release：编译、RCC、MOC 和链接全部通过；
- Qt WebSockets 正确找到并链接；
- 更新后的测试程序也成功编译、链接；
- `ctest --test-dir build-familylink-ninja -C Release --output-on-failure`：1/1 通过，用时 1.46 秒。

### LoongArch 交叉 Release

- SDK 中 `Qt6WebSocketsConfig.cmake`：存在；
- 45 个构建步骤全部通过；
- 产物：LoongArch64 ELF，动态链接器 `/lib64/ld-linux-loongarch-lp64d.so.1`；
- 两段提示音由 RCC 编入产物。

### 家属端

- `npm run check`：通过；
- `npm test`：20/20 通过，包含 480×360 / 125 ms 低负载视频配置测试；
- `npm run build:release`：通过，生成 `release/win-unpacked/LongPet Family.exe`。

### 当前板端只读检查

未替换 `/home/longpet/LongPet`，未重启服务。检查结果：

- `longpet.service` active；
- `aplay`、`gst-launch-1.0` 存在；
- `v4l2src`、`alsasrc`、`alsasink` 插件存在；
- `longpet` 属于 `video`、`audio`、`input` 组；
- `longpet` 对 `/dev/video0`、USB capture/playback 节点有读写权限；
- USB 声卡 ALSA ID 为 `Device`（card 1），匹配代码默认配置；
- 当前旧程序只监听 8787；新程序进入通话后才会监听 8788。

因此“板端结果”目前是运行环境满足条件和交叉产物可链接；真实双向媒体效果、回声、CPU、延迟和连续呼叫仍需按用户要求由新包上板后验收。

## 10. 快速上板验收

1. 停止服务，备份并替换 `/home/longpet/LongPet`，保持 owner/execute 权限；
2. 启动 `longpet.service`，确认 8787 监听；空闲时 8788 应不监听；
3. 家属端先发起语音通话，确认只播一次 `zh_voice_call.wav`，提示期间页面为“正在通知设备”；
4. 提示结束后确认双方能互相听见，且未打开摄像头；
5. 挂断后立即连续呼叫两次，观察无 `Device or resource busy`；
6. 发起视频，确认双向视频/音频、LongPet 满屏裁切且不再出现本地预览；
7. 在提示期间取消，确认提示立即停、摄像头释放并返回首页；
8. 通话中断网，确认双方错误可理解且设备释放；
9. 通话中再次发起呼叫，确认 `DEVICE_BUSY`；
10. 检查 `journalctl -u longpet.service -b -f` 中媒体进程与错误日志。

建议同时观察：

```sh
ss -lntp | grep 8788
ps -ef | grep -E 'gst-launch|aplay'
pidof LongPet
top -H -p "$(pidof LongPet)"
ps -C gst-launch-1.0 -o pid,pcpu,pmem,args
```

请分别记录空闲、语音通话和视频通话稳定 30 秒后的 LongPet 与三个 GStreamer 进程 CPU。界面还应确认：视频等待阶段为纯黑、通话中无板端本地小窗；语音页面保持 `#121210` 背景且 Speaking 脸铺满主要区域；家属端每次新通话都从 `00:00` 开始计时。

通话结束后 8788、`gst-launch-1.0` 与 `aplay` 应消失。

## 11. 已知限制

- 第一版面向可信局域网；Bearer Token 和 per-call media token 能阻止未配对会话，但 `ws://` 没有 TLS，不应直接暴露公网；
- PCM 单方向约 256 kbit/s，后续可升级 Opus；
- Windows 请求系统回声消除、降噪、自动增益，板端没有专用 AEC，实际扬声器回声需上板评估；
- 固定低延迟缓冲不是自适应 jitter buffer，不做音视频唇音同步；
- GStreamer 子进程在启动后若插件/设备协商失败，会异步进入 `failed` 并释放；
- 当前未集成 KWS，只提供了正确的暂停/恢复信号边界；
- 当前的低负载档位优先保证 2K0300 上的实时性，不追求高清画质；本轮没有替换板端程序，实际 CPU、流畅度和温度等待用户上板验证。
