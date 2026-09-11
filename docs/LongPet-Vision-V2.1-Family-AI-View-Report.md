# LongPet Vision V2.1：Family AI View 与 V2.0 封版报告

日期：2026-09-11  
状态：代码、Windows 测试与 LoongArch 交叉构建完成；真实 2K300 联调由用户执行

## 1. 本轮结论

Vision V2.0 已用真人持续走动数据完成动态验收并结束算法开发阶段：CNN 平均推理
993.922 ms、detector 实际约 0.246821 Hz，而 Detector + Sparse LK 输出的目标位置达到
7.22833 Hz；6 次 tracker failure 全部成功 reacquire，420 次 observation 中 stale 为 0。

Vision V2.1 新增独立的家属端“AI 视野”：家属进入页面后，LongPet 才把共享摄像头已经产生的
原始 JPEG 以默认 7 FPS 发送到家属端，同时发送 V2.0 `TargetObservation` 元数据。bbox 在 Windows
客户端绘制，板端没有 JPEG 解码、画框或重新编码。退出页面、网络断开或认证失败都会释放 AI View
的 camera consumer，本地 Vision 生命周期不受影响。

## 2. 架构与调用链

```text
Electron AI 视野页面
  -> preload IPC
  -> FamilyLinkService (Windows)
  -> HttpFamilyLinkAdapter
  -> POST /api/v1/vision-monitor/sessions (Bearer)
  -> FamilyLinkController
  -> FamilyLinkService (LongPet)
  -> FamilyVisionMonitorService
  -> FamilyVisionStreamPort
  -> FamilyVisionStreamAdapter (WebSocket :8789)

CameraCaptureAdapter
  +-> VisionService -> Detector + Sparse LK -> TargetObservation --+
  +-> FamilyVisionMonitorService -> raw CameraFrame JPEG -----------+-> WebSocket

WebSocket -> VisionMonitorAdapter -> createImageBitmap + Canvas bbox overlay
```

分层保持为：Application 负责装配，Controller 负责 HTTP 协议入口，Service 负责会话和共享摄像头
生命周期，Adapter 负责 WebSocket、认证、二进制 framing 和背压，React 页面只调用 preload 暴露的
业务接口。页面不直接访问板端设备或数据库。

## 3. 为什么在客户端画框

板端发送 `CameraCaptureAdapter` 已有 JPEG，不执行以下额外链路：

```text
JPEG decode -> OpenCV rectangle -> JPEG encode
```

这避免在单核 2K300 上增加解码、绘制和编码开销。家属端 Canvas 使用归一化 bbox，根据 JPEG
在画布内按比例完整显示后的实际区域换算坐标，因此不会因 Windows 窗口大小变化导致方框错位。

## 4. CameraCaptureAdapter 复用与生命周期

`FamilyVisionMonitorService` 是一个新的 `CameraSourcePort` consumer：

1. 应用启动时 WebSocket 监听端口可以存在，但不 acquire 摄像头，也不上传视频；
2. 家属端必须先通过带 Bearer Token 的 HTTP 请求获取 30 秒有效的一次性会话令牌；
3. WebSocket 验证该短时令牌后，Service 才调用 `CameraCaptureAdapter::acquire(this)`；
4. 摄像头帧按 `LONGPET_VISION_MONITOR_FPS` 降频发送，范围 1～10，默认 7；
5. 页面退出发送 `stop` 并关闭 WebSocket；异常断开也触发相同的 release；
6. 重复开关页面不会重复打开 `/dev/video0`，最后一个 consumer 离开后仍由共享 Adapter 决定是否
   停止底层 capture；
7. AI View 关闭只释放远程查看 consumer，不停止本地 `VisionService`。

WebSocket 发送发生背压时只保留最新 JPEG，旧帧被覆盖，避免网络变慢后延迟持续累积。

## 5. AI View 与 VideoCall 的区别

| 场景 | Camera | Vision | 音频 | 会话语义 |
|---|---|---|---|---|
| AI View | 共享 consumer | 继续运行 | 不使用 | 家属短时查看 |
| VideoCall | 共享 consumer | 按 V2.0 原策略暂停/reset | 双向 | 正式通话 |

AI View 没有调用 `VideoCallService`、`callActivityChanged` 或 `MediaSessionCoordinator`，因此不会触发
Vision suspend。正式视频通话的既有暂停与恢复连接保持不变，并继续由原测试覆盖。

## 6. HTTP 会话接口

### 6.1 创建会话

```http
POST /api/v1/vision-monitor/sessions
Authorization: Bearer <family-link-token>
```

成功响应（字段示例，不是固定值）：

```json
{
  "sessionId": "uuid",
  "sessionToken": "one-time-random-token",
  "port": 8789,
  "protocolVersion": 1,
  "mediaFrameVersion": 1,
  "frameRate": 7,
  "expiresAt": "2026-09-11T07:00:30Z"
}
```

该接口由现有 FamilyLink Bearer 鉴权保护。长期配对令牌留在 Electron 主进程的 HTTP Adapter 中，
Renderer 只获得短时一次性 WebSocket 会话令牌。当前 V1 每次仅允许一个 viewer；已有 viewer 时返回
`409 VISION_MONITOR_BUSY`。

## 7. WebSocket 与二进制帧

地址由已配置 FamilyLink URL 的 scheme/host 与服务端返回端口推导：

```text
ws://<family-link-host>:8789/vision-monitor/v1
```

没有在源码中写死当前开发板 IP。底层继续复用 `MediaFrameProtocol` 的 LPMF v1 帧头：

| 偏移 | 字段 | 长度 |
|---:|---|---:|
| 0 | magic `LPMF` | 4 |
| 4 | version | 1 |
| 5 | stream type | 1 |
| 6 | flags | 2 |
| 8 | sequence | 4 |
| 12 | timestamp usec | 8 |
| 20 | payload length | 4 |

本连接只使用既有流类型：

- `DeviceVideo (1)`：原始 JPEG；header sequence 取 camera frame sequence 的低 32 位，timestamp
  取采集时间；
- `Control (5)`：鉴权、启动、错误、停止和 `vision_target` JSON。

认证控制帧：

```json
{
  "type": "authenticate",
  "protocol_version": 1,
  "session_id": "uuid",
  "token": "one-time-random-token"
}
```

未认证连接不会 acquire Camera，也不会收到 JPEG/metadata；6 秒未认证即关闭。

## 8. Vision metadata

```json
{
  "type": "vision_target",
  "protocol_version": 1,
  "frame_sequence": 1718,
  "capture_timestamp": "2026-09-11T07:00:00.123Z",
  "published_at": "2026-09-11T07:00:00.172Z",
  "present": true,
  "fresh": true,
  "state": "TRACKING",
  "age_ms": 49,
  "bbox": { "x": 0.477, "y": 0.0, "w": 0.523, "h": 0.691 },
  "detector_confidence": 0.86,
  "tracker_confidence": 0.93,
  "tracked_points": 49,
  "detector_ran": false,
  "detector_ms": 0.0,
  "tracker_ms": 21.5,
  "target_update_hz": 7.2,
  "detector": "TinyissimoYOLO-v1.2",
  "tracker": "Sparse LK"
}
```

bbox 为 `[0,1]` 的左上角 `x/y` 加 `w/h`，服务端再次裁剪到合法范围。以下情况 `bbox` 为 null，
客户端也会主动隐藏旧框：

- `present == false`；
- `fresh == false`；
- `SEARCHING` 或 `LOST`；
- metadata 到达客户端后超过 1000 ms 没有刷新；
- 服务端报告的 `age_ms > 1000`。

V2.1 使用 latest JPEG + latest fresh observation，不做严格逐帧缓存配对。视频帧头和 metadata 都保留
sequence/timestamp，页面 debug 模式显示两者序号，便于后续判断偏差并升级精确配对。

## 9. 家属端 UI

侧边栏新增独立“AI 视野”入口，未并入“语音 / 视频通话”。页面包含：

- 保持宽高比的摄像头 Canvas；
- 客户端绘制的人物框；
- SEARCHING、DETECTED、TRACKING、CORRECTED、LOST、REACQUIRED 中文状态；
- 普通模式的状态和最近更新时间；
- 可选“显示 AI 调试信息”开关；
- 实际 JPEG FPS、目标更新 Hz、视频/目标帧序号、Detector/Tracker、置信度、特征点和真实延迟；
- 隐私说明与手动重新连接按钮。

DETECTED/CORRECTED/REACQUIRED 使用橙色框，TRACKING 使用绿色框，使比赛展示时能直观看出低频 CNN
检测/校正与高频 Sparse LK 跟踪的区别。没有真实数据的指标显示 `--`，不会伪造。

## 10. 配置与部署

`deploy/longpet-familylink.conf.example` 已补充：

```ini
[Service]
Environment="LONGPET_FAMILY_LINK_ADDRESS=0.0.0.0"
Environment="LONGPET_FAMILY_LINK_PORT=8787"
Environment="LONGPET_FAMILY_LINK_TOKEN=replace-with-a-random-token"
Environment="LONGPET_VISION_MONITOR_PORT=8789"
Environment="LONGPET_VISION_MONITOR_FPS=7"
Environment="LONGPET_CAMERA_ROTATION=180"
```

AI bbox 还要求按现有 Vision 部署说明启用 `LONGPET_VISION_ENABLED=1` 并配置有效模型路径。Vision
未启用时 AI View 仍可显示摄像头，但不会伪造目标 metadata。若 10 FPS 对板端负载过高，优先使用
默认 7 FPS 或降到 5 FPS。

`LONGPET_CAMERA_ROTATION` 支持顺时针 `0/90/180/270`。方向随共享 `CameraFrame` 传播：Detector 和
Sparse LK 对解码图像做同一旋转；WebSocket 控制消息把角度通知 Windows，Canvas 只在显示板端
JPEG 时旋转。原始 MJPEG 不在 2K300 上重新编码，家属端摄像头也不受影响。

家属端仍连接：

```text
http://192.168.137.32:8787
```

并填写与板端一致的 FamilyLink Token。网络需允许 TCP 8787 和 8789。

## 11. 测试结果

### 11.1 LongPet Windows Release

- Qt 6.11 / MinGW Release 编译通过；
- `LongPetVisionV1Tests`：旋转补充后 16 passed，0 failed；
- `LongPetV02Tests`：43 passed，0 failed，2 skipped；跳过项分别需要 Python KWS bridge 配置和
  UI capture 环境变量，与 V2.1 无关。

新增自动测试覆盖：

- AI View 与 Vision 同时 acquire 同一个 Camera Adapter，底层只启动一次；
- AI View active 时 `VisionService::isPaused() == false`；
- 无 target 时仍发送 JPEG，出现人物后继续发送 telemetry；
- normalized bbox 序列化和边界裁剪；
- SEARCHING/LOST/stale 不发送可绘制 bbox；
- 断开与 5 轮重复开关均正确 release consumer；
- 未认证连接收不到 camera frame，错误 token 被拒绝；
- 正确短时 token 能启动并接收 LPMF `DeviceVideo`；
- 既有 VideoCall + Vision/KWS 生命周期测试继续通过。

Windows 测试时发现系统 PATH 优先加载 `C:\mingw64` 的 `libstdc++-6.dll`，与 Qt 6.11 自带 MinGW
运行库混用会卡在 QtTest crash handler 初始化。将以下目录置于 PATH 前部后测试正常：

```text
D:\Qt\Tools\mingw1310_64\bin
D:\Qt\6.11.0\mingw_64\bin
```

这是本机测试环境问题，不是 AI View 线程死锁。

### 11.2 家属端

- Node 测试：旋转补充后 27 passed，0 failed；
- `npm run check`：JS syntax check 与 Vite production build 通过；
- Electron mock smoke：AI 视野导航、画面区域、状态卡、debug 开关和隐私说明渲染正常；
- 新测试覆盖 HTTP session、LPMF 复用、URL 推导、normalized bbox 解析、SEARCHING/LOST/absent/
  stale 隐藏策略。

Vite 仍报告现有第三方 `lottie-web` direct-eval 和 bundle 大小警告，本轮没有引入该依赖，也不影响
构建产物。

### 11.3 WSL 24.04 LoongArch Release

执行：

```bash
cd /mnt/d/code_qt/longpet_main/longpet
BUILD_DIR=/tmp/longpet-vision-v21-cross \
LONGPET_BUILD_JOBS=4 \
LONGPET_ENABLE_VISION=ON \
bash scripts/build-loongarch.sh
```

`LongPet` 与 `LongPetVisionBench` 均编译链接成功，`file` 确认为 LoongArch 64-bit ELF，解释器为
`/lib64/ld-linux-loongarch-lp64d.so.1`。

## 12. 板端状态与用户实测清单

按用户要求，本轮不继续代为执行真实 2K300 AI View 性能测试。交叉产物曾上传为
`/tmp/LongPet-v21`，没有覆盖 `/home/longpet/LongPet`；正式 `longpet.service` 已恢复并确认
`active`。

用户部署新二进制、启用 Vision 并重启服务后，建议依次检查：

1. 家属端进入 AI 视野，确认 8789 会话连接并出现 JPEG；
2. 人物进入/移动/离开，确认 TRACKING 方框跟随且 LOST 后旧框在 1 秒内消失；
3. 打开 debug，记录 JPEG FPS、target update Hz、detector/tracker latency；
4. 保持 KWS 正常运行，记录 `top` 中 LongPet CPU 与 RSS；
5. 退出页面，确认日志出现 camera consumer release；
6. 连续打开/关闭 10 次，确认摄像头没有 busy、consumer 不累积；
7. 再发起正式视频通话，确认原 VideoCall 能 acquire camera，Vision 按原策略暂停并在挂断后恢复；
8. 断开 Windows 网络后恢复，确认页面自动重新申请短时 session 并重连。

建议板端日志命令：

```bash
journalctl -u longpet.service -f
ss -lntp | grep -E ':8787|:8789'
top -b -d 1 -p "$(pidof LongPet)"
```

## 13. 当前限制

1. V1 只允许一个 AI View viewer。
2. 当前局域网使用 HTTP/WS 明文传输；已有 Bearer + 一次性 token 防止未授权直接查看，但不能抵御
   同一不可信局域网中的流量窃听。公网化前必须加 TLS 或可信隧道。
3. JPEG 与 bbox 是 latest 策略，不是逐帧严格锁步；快速移动时可能有短暂空间偏差。
4. 实际 2K300 上 Vision + KWS + AI View 的 CPU/RSS/FPS 尚待用户本轮实测，报告不填写推测数据。
5. VideoCall 与 AI View 可使用同一共享相机；正式通话仍会暂停 Vision，因此通话期间 AI View 不应
   期待持续 bbox 更新。后续如有需要可定义明确的通话优先级或主动关闭 monitor viewer。
6. 尚未加入本地屏幕上的“摄像头正在被家属查看”提示；从隐私产品角度，正式发布前建议补充。

## 14. 后续建议

下一步不要立即进入底盘控制。先完成上述板端联调并保存一组 Vision + KWS + AI View 数据；若 7 FPS
对 target update rate 影响明显，先降到 5 FPS。数据通过后，再建立语义层（LEFT/CENTER/RIGHT、
NEAR/MEDIUM/FAR、稳定窗口）和 MotionService 安全边界，为头部朝向与人物跟随提供输入。

## 15. 补充：倒装摄像头统一方向校正

实机 AI View 确认摄像头物理倒装 180°，视频通话中的板端画面也同样倒置。板端只读检查表明，
该 UVC 摄像头未暴露 V4L2 rotate/flip 控件，当前 Buildroot 也没有 GStreamer `videoflip`。没有采用
`jpegdec -> videoflip -> jpegenc`，因为它会让单核 2K300 对 30 FPS MJPEG 全量解码和重编码。

本轮增加统一配置：

```ini
Environment="LONGPET_CAMERA_ROTATION=180"
```

实现语义为顺时针旋转，允许 `0/90/180/270`，缺省及非法值回退为 `0`。方向由
`CameraCaptureAdapter` 写入 `CameraFrame::rotationDegrees`：

1. Tinyissimo 和 FastestDet 在 JPEG 解码后、resize/letterbox 前旋转；
2. Sparse LK 在灰度 JPEG 解码后做同一旋转，Detector/Tracker 始终共享坐标系；
3. AI View `stream_started.camera_rotation` 通知 Windows Canvas 旋转板端 JPEG，bbox 继续使用旋转后
   的 normalized 坐标；
4. 视频通话 `authenticated.cameraRotation` 执行相同显示校正，只影响 LongPet 发出的 DeviceVideo，
   不旋转 Windows 本地摄像头和 LongPet 收到的 FamilyVideo；
5. 网络仍发送摄像头原始 MJPEG，不增加板端 JPEG 重编码负载。

补充验证结果：Windows Qt Release 编译通过，完整 CTest 2/2 通过；Vision 测试 16/16 通过；
家属端 Node 测试 27/27 通过，Vite production build 通过。按用户要求终止本次 LoongArch 交叉构建，
未将不完整构建计为通过，也未部署本补充版本。板端需要验证画面、bbox 和移动方向一致，并复测视频
通话中只有板端画面被校正。
