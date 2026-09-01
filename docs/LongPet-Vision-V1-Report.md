# LongPet Vision V1 实现与板端基准报告

日期：2026-09-01  
分支基线：`main` / `42ca6a9a5f73b8d32bf4c148177e2556d64f8af8`  
目标板：Loongson 2K0300，LoongArch64，单核 1 GHz，无 LSX/LASX

## 1. 本版目标和边界

Vision V1 只完成以下闭环：

```text
共享 USB Camera Source
    → 640×480 MJPEG JPEG 原帧
    → latest-frame-only 异步调度
    → OpenCV 解码/预处理
    → FastestDet 352×352 / ONNX Runtime CPU
    → PersonDetection / VisionFrameResult
    → 日志或独立 Benchmark
```

本版没有实现人物跟随、电机控制、跌倒检测、Pose、手势、云端 VLM、FamilyLink
视觉接口、自动报警或正式视觉页面。UI 页面不访问摄像头、GStreamer、OpenCV 或 ONNX
Runtime。

## 2. 模型选择和工程判断

此前 YOLO11n-pose 640×640 与 PalmDetection + HandLandmark 在 2K0300 上均为数秒一帧。
V1 选择 FastestDet FP32 352×352，是为了用更小的单尺度检测器回答“当前板端 CPU 推理到底
能有多快”，而不是预设它必然满足实时需求。

模型来自 FastestDet 官方仓库：

- 项目：<https://github.com/dog-qiuqiu/FastestDet>
- 模型：`example/onnx-runtime/FastestDet.onnx`
- 许可证：BSD-3-Clause，copyright 2022 xuehao.ma
- 固定 SHA-256：`31cb14c017fce347cb4c846ec62fdf00d76bb3beecdf4be21e7116ac67feb880`

模型二进制不提交到 LongPet，也不在 CMake configure/build 时下载。显式准备脚本为
`scripts/vision/prepare-fastestdet-model.sh`，来源和再分发要求记录在
`scripts/vision/FASTESTDET-NOTICE.md`。

### 2.1 对方案保持的两个保留意见

1. FastestDet 官方 COCO 模型精度有限，只适合验证人物检测基础链路，不能直接当作安全、跌倒
   或跟随功能的最终感知模型。
2. 官方预处理是把 4:3 的 640×480 直接缩放到 352×352，会产生几何变形。V1 为了与官方
   ONNX 示例保持可比而复现该路径；预处理已封装在 Adapter 内，后续可以用相同 Benchmark
   比较 letterbox，但不应在没有精度数据时悄悄改变模型输入语义。

## 3. 分层和调用链

```text
Application（组合根）
    ├─ CameraCaptureAdapter : CameraSourcePort
    │      └─ QProcess / gst-launch-1.0 / /dev/video0
    │
    ├─ FastestDetAdapter : VisionDetectorPort
    │      └─ OpenCV + ONNX Runtime CPUExecutionProvider
    │
    ├─ VisionService
    │      └─ VisionInferenceThread（latest-frame-only）
    │
    ├─ VideoCallMediaAdapter
    │      └─ 消费同一个 CameraSourcePort，JPEG 原帧直传
    │
    └─ VideoCallService
           └─ callActivityChanged(bool) → VisionService pause/resume
```

层次职责：

- Model：`CameraFrame`、`PersonDetection`、`VisionFrameResult`、`VisionDetectorInfo`；
- Service Port：`CameraSourcePort`、`VisionDetectorPort`；
- Service：异步调度、丢帧、暂停和结构化结果信号；
- Platform Adapter：GStreamer 摄像头进程、JPEG 分帧、OpenCV/ORT 推理；
- Application：实例创建、依赖注入、视频通话负载协调和日志；
- UI：V1 不新增页面，也不接触硬件/API。

## 4. 共享 Camera Source

原 `VideoCallMediaAdapter` 内的本地 USB 摄像头采集被抽到 `CameraCaptureAdapter`。唯一采集链仍为：

```text
gst-launch-1.0 -q
    v4l2src device=<configured device>
    ! image/jpeg,width=640,height=480,framerate=30/1
    ! fdsink fd=1 sync=false
```

Adapter 从 stdout 解析 JPEG SOI/EOI，维护 sequence、UTC timestamp 和 latest JPEG，向上只暴露
`CameraFrame`，不暴露 `QProcess` 或 GStreamer 细节。解析缓存上限为 4 MiB，异常数据不会无限增长。

### 4.1 多 consumer 生命周期

`acquire(QObject *consumer)` / `release(QObject *consumer)` 使用 owner token：

- 第一个 consumer 出现时启动摄像头；
- 同一 owner 重复 acquire 不重复计数；
- consumer 销毁时自动 release；
- 任一 consumer 释放不影响其余 consumer；
- 最后一个 consumer 释放才停止 GStreamer；
- 摄像头异常退出发出明确的 unavailable/failed 信号，不使 LongPet 主进程崩溃。

使用 `QObject *` 而不是固定字符串 ID，避免不同模块误用同名 token 或忘记释放。

### 4.2 设备配置兼容

解析顺序：

```text
LONGPET_CAMERA_DEVICE
    → LONGPET_CALL_CAMERA_DEVICE（旧配置兼容）
    → /dev/video0
```

## 5. VideoCall 摄像头复用

视频通话不再自行创建第二个 `v4l2src`，而是 acquire 同一个 `CameraSourcePort`：

- 视频模式开始：`VideoCallMediaAdapter::startCamera()` acquire；
- 每收到 3 个 30 FPS JPEG 发送 1 个，保留约 10 FPS 降频；
- payload 仍是摄像头原始 JPEG，不经过 OpenCV 解码或重编码；
- 通话 stop/hangup：release；
- Vision 与 VideoCall 同时订阅时 `/dev/video0` 仍只有一个采集进程；
- 音频 GStreamer、媒体 WebSocket 和二进制帧协议未修改。

Windows 自动化测试验证了 Vision + VideoCall 两个 consumer 只启动一次 Camera、通话释放后
Vision 继续持有、最后释放才停止。由于本轮没有替换板端正式 `/home/longpet/LongPet`，真实家属端
视频通话的板端端到端回归仍需在部署候选主程序后完成。

## 6. FastestDet ONNX 接入

`FastestDetAdapter` 实现 `VisionDetectorPort`，正式路径不依赖 Python、PyTorch 或 Ultralytics。

### 6.1 graph 校验

初始化时验证：

- 恰好一个输入和一个输出；
- 输入、输出均为 float32；
- 输入固定为 `[1,3,352,352]`；
- 输出为单尺度 NCHW 且 channels 至少为 6；
- 官方模型实测输出为 `[1,85,22,22]`；
- ORT provider 为 `CPUExecutionProvider`，实测版本 1.17.1。

模型不匹配、缺失或加载失败会把 Vision 标记为 unavailable，不影响提醒、语音、FamilyLink
和视频通话等其他功能。

实现过程中发现并修复了一个真实板端生命周期错误：`GetInputTypeInfo(0)` 临时拥有者析构后，
其 `GetTensorTypeAndShapeInfo()` 非拥有 view 会悬空，最初表现为 `std::length_error`。现在显式保留
两个 `TypeInfo` 拥有者，并先校验维度数量、再通过固定四维数组读取 shape；所有 ORT/标准异常都
转为可诊断错误，不再直接 abort。

### 6.2 预处理和后处理

预处理按官方 ONNX 示例：

1. `cv::imdecode` 解码当前选中的 JPEG；
2. BGR 图像直接 resize 到 352×352；
3. 转 float32 并除以 255；
4. BGR HWC 拆成 NCHW `[1,3,352,352]`。

后处理不是套用 YOLOv5/v8/v11：

- channel 0：objectness；
- channel 1~4：x/y/w/h；
- channel 5+：类别得分；
- `score = objectness^0.6 × bestClassScore^0.4`；
- center offset 使用 `tanh`，宽高使用 `sigmoid`；
- 只保留 COCO person class 0；
- 按阈值过滤和 IoU NMS；
- 归一化 bbox 映射回原始 JPEG 尺寸并裁剪到 `[0,1]`。

`PersonDetection` 同时提供 pixel `boundingBox`、`normalizedCenter`、`normalizedSize`、confidence，
未来 V2 可以只消费通用 model，不依赖 FastestDet 输出细节。

## 7. 异步调度与 latest-frame-only

所有 JPEG decode、resize、ORT inference 和 NMS 都在 `VisionInferenceThread`，不在 Qt GUI 线程。

worker 只保存一个 `std::optional<CameraFrame>`：

- worker 空闲时取出当时最新帧；
- worker busy 时，新帧覆盖旧 pending frame；
- pending queue 最大为 1；
- 摄像头 30 FPS 不代表解码 30 FPS，只有真正进入推理的 JPEG 才被 OpenCV 解码；
- 每次推理串行执行，不创建并发 ORT Run；
- `LONGPET_VISION_INTERVAL_MS` 限制最小启动间隔，默认 300 ms；
- 实际周期自然为 `max(模型处理耗时, 配置间隔)`。

板端 60 秒日志中的 sequence 从 `1 → 87 → 169 → 254 ...` 跳跃，证明约 30 FPS 输入期间没有
补算中间帧，而是始终选择最新帧。

## 8. 视频通话负载协调

`Application` 将 `VideoCallService::callActivityChanged(bool)` 连接到
`VisionService::setVideoCallActive(bool)`：

- 通话 active：清空 pending frame，暂停后续推理；
- 通话结束：允许下一张最新帧进入推理；
- Camera Source 不因 Vision pause 而关闭，VideoCall 继续收到 JPEG；
- Vision 不持有 `MediaSessionCoordinator`，不会长期独占麦克风媒体会话。

ORT 1.17 的同步 `Run()` 在本版没有中途取消；如果通话恰好在一次推理已开始后进入，当前 Run
会结束，但结果因 paused 状态被丢弃，之后不再启动新推理。当前板端一次 Run 很慢，这会使通话
开始后的最初约 2.8 秒仍可能有 CPU 竞争，是已知限制。

## 9. 配置参数

| 环境变量 | 默认值 | 说明 |
|---|---:|---|
| `LONGPET_VISION_ENABLED` | false | 正式 LongPet 是否启动 VisionService；仅 `1/true/yes/on` 开启 |
| `LONGPET_CAMERA_DEVICE` | 空 | 通用共享摄像头节点，优先级最高 |
| `LONGPET_CALL_CAMERA_DEVICE` | 空 | 旧视频通话摄像头配置，保留兼容 |
| `LONGPET_VISION_MODEL_PATH` | `/home/longpet/models/fastestdet.onnx` | 模型路径 |
| `LONGPET_VISION_INTERVAL_MS` | 300 | 最小推理启动间隔，1~60000 ms |
| `LONGPET_VISION_CONFIDENCE_THRESHOLD` | 0.65 | person score 阈值 |
| `LONGPET_VISION_NMS_THRESHOLD` | 0.45 | NMS IoU 阈值 |
| `LONGPET_VISION_INFERENCE_THREADS` | 1 | ORT intra-op thread 数；当前板只有 1 核 |
| `LONGPET_VISION_BENCHMARK_BIN` | `/home/longpet/LongPetVisionBench` | 板端脚本使用的 benchmark 路径 |
| `LONGPET_VISION_BENCHMARK_DURATION` | 60 | 摄像头 benchmark 秒数 |
| `LONGPET_VISION_BENCHMARK_WARMUP` | 10 | 摄像头 warmup 数 |

`deploy/longpet.service` 当前明确设置 `LONGPET_VISION_ENABLED=0`。这既是安全默认值，也符合本次
实测性能结论。

## 10. CMake 和交叉编译

新增 CMake option：

```text
LONGPET_ENABLE_VISION=OFF
```

默认 OFF，Windows 开发机没有 OpenCV/ORT 时仍能构建 LongPet 和所有 fake-based tests。启用时：

- `find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)`；
- `find_path(onnxruntime_cxx_api.h)`；
- `find_library(onnxruntime)`；
- 可用 `ONNXRUNTIME_ROOT` cache/env 提供非标准前缀；
- 不写死 SDK 路径，不下载或编译另一份 ORT；
- 只在 ON 时构建独立 `LongPetVisionBench`。

LoongArch 交叉编译固定在 WSL Ubuntu 24.04：

```bash
cd /mnt/d/code_qt/longpet_main/longpet
BUILD_DIR=/tmp/longpet-vision-v1-loongarch64-release \
LONGPET_BUILD_JOBS=4 \
LONGPET_ENABLE_VISION=ON \
bash scripts/build-loongarch.sh
```

实测环境：Ubuntu 24.04.4 LTS，Buildroot SDK
`/opt/loongarch64-buildroot-linux-gnu_sdk-buildroot`，G++ 13.3，sysroot 内 OpenCV 4.10、
ONNX Runtime 1.17.1。Release 交叉构建成功，两个产物均由 `file` 确认为 LoongArch64 ELF：

```text
LongPet:            ELF 64-bit LSB executable, LoongArch
LongPetVisionBench: ELF 64-bit LSB executable, LoongArch
```

## 11. 独立 Benchmark

单图模式：

```bash
LongPetVisionBench \
  --model /home/longpet/models/fastestdet.onnx \
  --image test.jpg \
  --iterations 100 \
  --warmup 10
```

摄像头模式：

```bash
LongPetVisionBench \
  --model /home/longpet/models/fastestdet.onnx \
  --camera /dev/video0 \
  --duration 60 \
  --warmup 10 \
  --interval-ms 1
```

`scripts/run-vision-benchmark.sh` 会先检查模型、摄像头字符设备、GStreamer、`ldd` 和缺失动态库，
再运行 60 秒 camera benchmark。它不安装包、不修改系统配置、不停止服务；若要排除 KWS 负载，
操作者需在命令外明确 stop/start LongPet。

## 12. 自动化测试

`tests/VisionV1Test.cpp` 使用 Fake Camera / Fake Detector，不依赖真实摄像头或模型，覆盖：

- Camera acquire/release、多 consumer、最后释放才 stop；
- consumer 销毁自动释放；
- JPEG 跨 chunk SOI/EOI 解析和 latest frame；
- 新旧摄像头环境变量优先级；
- FastestDet bbox 原图映射和 normalized center/size；
- confidence threshold、person class filtering；
- Vision worker 异步运行、latest-frame-only、无积压；
- VideoCall active pause / ended resume；
- VideoCall + Vision 共用一次 Camera start；
- 模型缺失、Camera unavailable graceful degradation；
- VisionService start/stop 生命周期。

Windows Release 构建使用 Qt 自带 MinGW 13.1，避免误加载 `C:\mingw64` 14.2 与 Qt ABI 混用：

```powershell
$env:PATH = 'D:\Qt\Tools\mingw1310_64\bin;D:\Qt\6.11.0\mingw_64\bin;' + $env:PATH
cmake --build build-vision-v1-win-qt-release --parallel 4
ctest --test-dir build-vision-v1-win-qt-release -C Release --output-on-failure
```

结果：

```text
LongPet.V02       Passed  27.62 sec
LongPet.VisionV1  Passed   1.03 sec
2/2 tests passed, total 28.67 sec
```

## 13. 2K0300 板端验证

### 13.1 环境和操作边界

- 板端 IP：`10.240.178.51`；
- `/dev/video0` 存在，640×480 MJPEG 单帧采集成功；
- 板端 OpenCV 4.10 和 ONNX Runtime 1.17.1 动态库可解析；
- 官方模型 SHA-256 校验通过；
- 只把 Benchmark、模型和测试图复制到 `/tmp`；
- 未替换 `/home/longpet/LongPet`，未修改 systemd 配置；
- 纯视觉基准期间临时停止 `longpet.service`，完成后已恢复为 `active`。

### 13.2 单图正确性和纯模型数据

测试图使用 FastestDet 官方 `example/onnx-runtime/3.jpg`，图中有 4 人。阈值 0.65 时 warmup
和 3 次正式测量均返回 `person_count=4`。

条件：LongPet/KWS 已停，threads=1，warmup=1，measured=3。

| 指标 | 结果 |
|---|---:|
| avg inference | 2548.35 ms |
| min inference | 2523.53 ms |
| max inference | 2593.25 ms |
| P50 inference | 2528.28 ms |
| P95 inference | 2593.25 ms |
| avg total | 2583.93 ms |
| effective inference FPS（正式测量窗口） | 0.387 |
| total / measurement runtime | 10.45 / 7.75 s |
| RSS / Peak RSS | 46,400 / 46,400 kB |
| 外部 `/usr/bin/time` 平均 CPU | 93% |

### 13.3 60 秒 USB Camera 完整链路

条件：LongPet/KWS 已停，`/dev/video0` 640×480 MJPEG @ 30 FPS，threads=1，interval=1 ms，
warmup=2，measured=18。

| 指标 | 结果 |
|---|---:|
| total / measurement runtime | 59.67 / 52.81 s |
| avg decode | 24.46 ms |
| avg preprocess | 41.33 ms |
| avg inference | 2761.98 ms |
| min inference | 2726.04 ms |
| max inference | 2806.77 ms |
| P50 inference | 2735.38 ms |
| P95 inference | 2806.77 ms |
| avg total | 2828.28 ms |
| effective inference FPS（正式测量窗口） | 0.341 |
| 结束时 RSS | 47,184 kB |
| Peak RSS | 47,184 kB |
| 外部 `/usr/bin/time` 平均 CPU | 91% |

摄像头当时画面没有人物，所以 live `person_count=0`；人物正确性由上一节官方测试图验证。

### 13.4 与常驻 KWS 并发时

板端 LongPet 的 KWS Python 进程单独约占 35% CPU。未停止服务的 60 秒 camera benchmark 得到：

| 指标 | 结果 |
|---|---:|
| avg inference | 5716.74 ms |
| avg total | 5811.42 ms |
| measured frames | 7（另有 2 warmup） |
| 完成吞吐（含 warmup 的诊断估算） | 约 0.154 FPS |
| benchmark 平均 CPU | 49% |
| KWS 观测 CPU | 约 34.7% |

这说明单核上的 KWS 与视觉存在严重竞争。即便 `LONGPET_VISION_INTERVAL_MS=300`，当前模型一次
Run 自身已超过 2.7 秒，配置较短 interval 不能提高吞吐。

### 13.5 性能结论

按照任务给出的工程参考，`> 2 s/frame` 原则上应淘汰该方案。实测结论是：

- 共享 Camera、异步调度、FastestDet 解码和结构化结果链路正确；
- FastestDet FP32 + 当前 ORT scalar CPU 后端不适合作为 LongPet 常驻实时视觉基础；
- 正式 `longpet.service` 必须继续默认关闭 Vision；
- 不应为了好看的 FPS 跳过 JPEG decode、模型 Run 或后处理；
- 不应直接进入人物跟随 V2，否则闭环控制周期至少为数秒且会拖慢 KWS/视频通话。

下一次性能探索应保持同一个 Benchmark，分别验证可信的 FastestDet INT8（如能取得）、更小输入
的专门模型、或针对 2K0300 优化的推理后端。当前 CPU 无 SIMD，不能预设 INT8 一定更快，也不能
伪造量化收益。

## 14. 已验证和未验证

已验证：

- Windows Vision OFF Release 构建；
- Windows 全部现有测试和 Vision fake tests；
- WSL Ubuntu 24.04 LoongArch Vision ON Release 交叉构建；
- 板端 ORT 模型加载、shape、provider/version；
- 官方多人图检测结果；
- `/dev/video0` live 完整链路和 60 秒性能；
- latest-frame-only 的 sequence 跳跃；
- RSS 与外部 CPU 数据；
- 基准结束后 Camera consumer 释放；
- 临时停止的 `longpet.service` 已恢复 active。

尚未验证：

- 将本轮新 `LongPet` 主程序部署后进行真实家属端视频通话回归；
- Vision enabled 的正式 GUI 进程长时间稳定性（性能结论决定默认不启用）；
- 视频通话恰好在一次约 2.8 秒 ORT Run 中开始时的用户体感；
- INT8 模型；
- 复杂光照、遮挡、远距离下的 person 精度；
- letterbox 与官方 direct resize 的精度/性能对比。

## 15. V2 接口准备情况

未来业务只需订阅：

```text
VisionService::visionResultReady(const VisionFrameResult &)
```

便可获得 confidence、归一化中心、bbox 大小、frame sequence 和 timestamp，不需要理解
FastestDet。替换为 YOLO-FastestV2、NanoDet 或其他 `VisionDetectorPort` 实现时，上层业务接口
保持不变。

不过基于本次结果，不建议立即实现人物跟随 V2；应先用独立小步骤找到能在目标周期内运行的
本地 person detector，再决定是否进入控制闭环。
