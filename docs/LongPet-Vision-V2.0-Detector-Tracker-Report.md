# LongPet Vision V2.0：Detector + Tracker 报告

- 日期：2026-09-11
- 开发分支：`vision-v1.1`
- 基线提交：`c6028f68e3f5732f899899c730391fa69cc855d4`
- 目标板：Loongson 2K300，LoongArch64，单核 1 GHz
- 板端地址：`192.168.137.32`
- 本轮范围：持续人物位置观测；不包含电机、人物跟随控制、跌倒或手势

## 1. 结论

Vision V2.0 已把 V1 的“每次位置更新都运行 CNN”改为：

```text
共享 USB Camera / latest frame
  → SEARCHING: Tinyissimo V1.2 检测
  → DETECTED: 用检测框初始化 sparse LK tracker
  → TRACKING: 轻量光流持续更新目标框
  → CORRECTED: 低频 Tinyissimo 重新定位并重置 tracker
  → LOST: tracker 不可信时回到积极检测
  → REACQUIRED: 再次检测到人物并恢复跟踪
```

最终 tracker 选择 **稀疏 Lucas-Kanade 光流**。实际 2K300 数据表明，它把稳定人物位置更新从
约 0.7～1.3 Hz 提高到 **7.7～8.8 Hz**。tracker P50 约 17～19 ms，明显低于一次
Tinyissimo 的 0.7～1.5 s。在 KWS 同时运行的 45 秒样本中，4 次周期 detector 校正成功、
tracker 0 次丢失。

这已经满足 V2.0 的软件闭环与板端静态/边缘目标、KWS 并发探索条件。提示词列出的十组受控
人体动作仍需操作者站在摄像头前按场景运行 Benchmark，尤其要继续确认快速尺度变化、遮挡和
多人情况下的漂移；因此当前可以进入 V2.1 观测语义研究，但在这组人工验收完成前不应接电机。

## 2. 固定 detector 与交接材料

本轮没有训练或量化模型。部署 detector 固定为交接材料中的 V1.2：

| 项目 | 值 |
|---|---|
| 模型 | TinyissimoYOLO-v1-small person-only |
| 输入 | 128×128 |
| 精度/格式 | FP32 ONNX |
| 文件 | `03_models/V1.2_FINAL/tinyissimo-person-128-longpet-v1.onnx` |
| SHA256 | `cb3defedb3ac01006d4caa5312c21d4a674e5a808e885067f2bc67e8f822f89c` |
| 参数量 | 402,057 |
| 文件大小 | 1,620,685 bytes |

模型来自 `D:\LongPet-Vision-Final-Handoff`，训练数据、checkpoint 和 runs 没有复制回主仓。
默认板端运行路径已改为：

```text
/home/longpet/models/tinyissimo-person-128-longpet-v1.onnx
```

## 3. 为什么选择 sparse LK

先检查了 WSL 交叉 SDK 和实际板端 OpenCV 4.10：

```text
HAVE_OPENCV_CORE
HAVE_OPENCV_IMGCODECS
HAVE_OPENCV_IMGPROC
HAVE_OPENCV_VIDEO
HAVE_OPENCV_VIDEOIO
```

实际有 `libopencv_video.so.4.10.0`，但没有 `opencv_tracking` 或 contrib `opencv_optflow`。
`cv::calcOpticalFlowPyrLK` 已由现有 `opencv_video` 提供，因此无需重做 Buildroot rootfs。

选择理由：

- 只增加一个现成的 OpenCV `video` 链接模块，不引入新运行时；
- 跟踪稀疏特征点，单核开销远低于 KCF/CSRT 一类相关滤波/特征 tracker；
- 可以从任意 detector bbox 初始化，不依赖 Tinyissimo 输出格式；
- 平移由一致光流点的中值估计，尺度由相对中心距离的稳健比率近似；
- detector 每隔数秒重置框和特征点，限制长期漂移；
- 特征点不足、一致性失败、目标大部分出框或连续低可信时明确进入 LOST。

这里没有把 `opencv_contrib` 加入系统。若十组受控测试证明 sparse LK 对纹理不足衣物或长遮挡
不够稳定，再单独比较 MOSSE/KCF；不建议现在为了“可选算法更多”增大镜像。

## 4. 分层和调用链

```text
Application（组合根）
  ├─ CameraCaptureAdapter : CameraSourcePort
  ├─ TinyissimoYoloAdapter : VisionDetectorPort
  ├─ SparseOpticalFlowTracker : VisionTrackerPort
  ├─ VisionService
  │    └─ VisionInferenceThread
  │         ├─ latest-frame-only
  │         ├─ detector/tracker 调度
  │         └─ TargetObservation
  └─ VideoCallService
       └─ callActivityChanged(bool) → VisionService pause/reset/resume
```

职责保持现有分层：

- Model：`TargetTrackingStatus`、`VisionTrackerResult`、`TargetObservation`；
- Service Port：检测器与 tracker 的抽象接口；
- Service：状态机、目标选择/关联、调度、过期判定、latest-frame；
- Platform：OpenCV JPEG 解码和 sparse LK 细节；
- Application：创建并注入具体 Adapter，记录状态迁移；
- UI：本轮不直接访问摄像头、OpenCV、ONNX 或平台 API，也未大改页面。

上层统一订阅：

```cpp
VisionService::targetObservationReady(const TargetObservation&)
```

便可获得人物是否存在、bbox、归一化中心/大小、最后 detector confidence、tracker confidence、
特征点数、来源时延、frame sequence、采集时间、发布时刻、age/fresh 状态和状态机阶段。

## 5. sparse LK 实现

`SparseOpticalFlowTracker` 的主要步骤：

1. 仅在 tracker 初始化/更新时解码当前最新 JPEG 为灰度图；
2. 默认缩小到 0.5 倍，即约 320×240；
3. 在 detector bbox 中用 `goodFeaturesToTrack` 选最多 60 个角点；
4. 用 15×15 window、两层 pyramid 的 `calcOpticalFlowPyrLK` 更新；
5. 过滤 OpenCV 标记失败、误差过大、越界的点；
6. 用位移中值和残差筛选抵抗离群点；
7. 估计 bbox 平移和单帧限制在 0.92～1.08 的尺度变化；
8. 特征点变少时在当前 bbox 内补点；
9. 有效点少于 6、光流不一致或 bbox 可见面积低于 35% 时 LOST。

tracker confidence 综合剩余特征点比例和中位光流误差。默认连续 3 帧低于 0.15 才 LOST，
避免 detector 推理后的第一张跨度较大帧偶发低分就立即重搜。

## 6. detector/tracker 调度

默认策略：

| 状态 | 行为 | 默认周期 |
|---|---|---:|
| SEARCHING | 对最新帧运行 Tinyissimo | 250 ms 下限；实际受单次 inference 限制 |
| DETECTED | 发布 detector 框并初始化 tracker | 立即 |
| TRACKING | sparse LK 更新最新帧 | 100 ms 下限 |
| CORRECTED | Tinyissimo 与当前目标关联，成功后重置 tracker | 每 8,000 ms |
| LOST | 发布丢失并积极运行 Tinyissimo | 250 ms 下限 |
| REACQUIRED | 发布新检测框并恢复 tracker | 检测成功时 |

周期 detector 不是“一票否决”：如果一次 detector 没检测到或没有与当前框匹配，会在同一帧尝试
推进仍健康的 tracker；只有 detector 与 tracker 都失败才 LOST。这样可降低 V1.2 偶发漏检导致
的画面位置断裂。

多人关联使用 IoU、归一化中心距离和检测置信度。关联框 IoU 很低且中心距离超过 0.35 时拒绝
跳到远处另一人；真正 LOST 后重新搜索允许选择新目标。当前不含 appearance embedding/re-ID。

worker 始终只保存一个 pending `CameraFrame`，新帧覆盖旧帧。detector 运行期间到达的几十张帧
不会排队补算。需要说明：同步 ORT inference 期间 tracker 也会暂停约 0.7～1.5 秒，完成后直接
处理最新帧；当前单核平台不能让 detector 和 tracker 真正并行而不造成更严重竞争。

## 7. Camera 与 VideoCall 集成

摄像头仍只有一个 `CameraCaptureAdapter`：Vision 和视频通话以 consumer token 共享采集源，
没有新增第二个 `/dev/video0` 进程。

`VideoCallService::callActivityChanged(true)` 会：

- 清空 Vision pending frame；
- 暂停新 detector/tracker step；
- 在 worker 线程重置 tracker；
- 丢弃暂停期间已经完成但尚未交付的视觉结果。

通话结束后恢复 Vision，从 SEARCHING 重新检测，而不是使用通话前的旧框。现有共享摄像头和连续
10 次 KWS-gated 语音/视频通话生命周期测试继续通过。

ORT 1.17 的同步 `Run()` 尚不能在通话开始瞬间中止；若通知恰好发生在 detector 内，当前 Run
结束前仍可能有最多约一次 inference 的 CPU 竞争，但结果不会发布，也不会启动下一次检测。

## 8. 配置

部署示例位于 `deploy/vision/longpet-vision.conf.example`。关键变量：

| 环境变量 | 默认值 | 说明 |
|---|---:|---|
| `LONGPET_VISION_ENABLED` | `0`（service 示例） | 是否启动正式视觉服务 |
| `LONGPET_VISION_MODEL_PATH` | V1.2 板端路径 | detector 模型 |
| `LONGPET_VISION_TRACKING_ENABLED` | `1` | 启用 V2 调度；关闭时回退 detector-only |
| `LONGPET_VISION_TRACKER_INTERVAL_MS` | 100 | tracker 更新下限 |
| `LONGPET_VISION_DETECTOR_CORRECTION_MS` | 8000 | 稳定跟踪校正周期 |
| `LONGPET_VISION_SEARCH_INTERVAL_MS` | 250 | 初始搜索周期下限 |
| `LONGPET_VISION_LOST_INTERVAL_MS` | 250 | 丢失后搜索周期下限 |
| `LONGPET_VISION_FRESHNESS_MS` | 2500 | observation 新鲜度阈值 |
| `LONGPET_VISION_TRACKER_SCALE` | 0.5 | tracker 灰度图缩放 |
| `LONGPET_VISION_TRACKER_MAX_POINTS` | 60 | 最大特征点 |
| `LONGPET_VISION_TRACKER_MIN_POINTS` | 6 | 最少有效点 |
| `LONGPET_VISION_TRACKER_QUALITY` | 0.01 | Shi-Tomasi quality level |
| `LONGPET_VISION_TRACKER_MIN_DISTANCE` | 5 | 特征点最小间距 |
| `LONGPET_VISION_TRACKER_MAX_ERROR` | 20 | LK 单点误差上限 |
| `LONGPET_VISION_TRACKER_MIN_CONFIDENCE` | 0.15 | 低可信阈值 |
| `LONGPET_VISION_TRACKER_LOW_CONFIDENCE_FRAMES` | 3 | 连续低可信丢失门限 |

100 ms / 8 s 是首轮实测起点，不是不可修改常量。受控动作测试若发现快速左右移动易丢失，可先把
tracker interval 调到 70～80 ms；若长期静止仍稳定，可以把 detector correction 增到 10～12 s。

## 9. Benchmark 扩展

`LongPetVisionBench` 保留原单图和 detector-only 模式，并新增：

```text
--tracking
--tracker-interval-ms
--correction-ms
--search-interval-ms
--lost-interval-ms
```

逐帧日志包含：

```text
target_status=TRACKING frame=... present=true fresh=true age_ms=...
detector_ran=false detector_ms=0 tracker_ms=... tracker_confidence=...
points=... bbox=x,y,w,h
```

汇总新增 detector 触发频率、target update rate、tracker 平均/P50/P95、纠偏、失败、重捕获和
SEARCHING 次数。仍保留 detector preprocess/inference/postprocess、CPU、RSS 和 P50/P95。

## 10. 自动化测试

Windows Qt 6.11 / MinGW 13.1、Release、Vision OFF：

```text
LongPet.V02       Passed  18.40 s
LongPet.VisionV1  Passed   2.73 s
2/2 tests passed, total 21.16 s
```

Vision 测试套件新增并验证：

- DETECTED → CORRECTED → TRACKING；
- tracker 失败 → LOST；
- detector 自动重捕获 → REACQUIRED；
- 视频通话暂停期间不发布 observation；
- 通话结束后清空旧 tracker 并重新检测；
- 原 latest-frame-only、共享摄像头、模型/Camera 降级测试无回归。

Windows Vision-OFF 仍能编译 tracker stub，不要求开发机安装 OpenCV。真实 OpenCV 路径由 WSL
Vision-ON 交叉构建和板端运行覆盖。

## 11. WSL 24.04 LoongArch 交叉构建

复用现有脚本：

```bash
cd /mnt/d/code_qt/longpet_main/longpet
BUILD_DIR=/tmp/longpet-vision-v2-cross \
LONGPET_BUILD_JOBS=4 \
LONGPET_ENABLE_VISION=ON \
bash scripts/build-loongarch.sh
```

结果：OpenCV 4.10 的 `core/imgcodecs/imgproc/video` 全部找到；`LongPet` 和
`LongPetVisionBench` Release 链接成功，均由 `file` 确认为 LoongArch ELF。

## 12. 2K300 板端实测

### 12.1 测试边界

- 新 Benchmark、V1.2 模型和临时主程序只放到 `/tmp`；
- 模型 SHA256 在板端复核通过；
- 未覆盖正式 `/home/longpet/LongPet`，未修改 systemd unit；
- 无 KWS 测试期间临时停止服务；
- KWS 测试保持现有 `longpet.service` 运行；
- 临时 `/tmp/LongPet-v2` 以 `longpet` 用户和 linuxfb 启动 25 秒，未崩溃；
- 全部测试结束后 `longpet.service` 已恢复为 `active (running)`。

摄像头画面中有一名人物，第一轮目标靠近最右边缘且部分出框，因此同时覆盖了静止/边缘目标的
初步情况，但不等同于完整十场景人工动作验收。

### 12.2 LongPet/KWS 停止，视觉基本独占

参数：45 秒，tracker interval 100 ms，correction 8 s，warmup detector 1。

| 指标 | 实测 |
|---|---:|
| detector avg inference | 734.78 ms |
| detector P50 / P95 | 753.87 / 771.93 ms |
| detector avg total | 763.08 ms |
| tracker avg | 18.84 ms |
| tracker P50 / P95 | 17.16 / 39.81 ms |
| target update rate | **7.68 Hz** |
| detector trigger rate | 0.284 Hz（含丢失后的积极重搜） |
| measured detector runs | 12 |
| target observations | 333（324 present） |
| tracker LOST / REACQUIRED | 3 / 3 |
| successful periodic CORRECTED | 0 |
| process CPU | 34.90% |
| RSS / peak RSS | 43,760 / 43,984 kB |

状态序列包含：

```text
DETECTED → LOST → REACQUIRED → TRACKING
         → LOST → REACQUIRED
         → LOST → SEARCHING → REACQUIRED → TRACKING
```

三次 LOST 的诊断均为“光流一致性检查失败”，之后 detector 均自动恢复目标。部分周期 detector
没有与边缘目标匹配时，健康 tracker 被继续使用，所以 successful CORRECTED 为 0 不代表没有
触发 detector；这也验证了 detector 单次漏检不会强制清空目标。

### 12.3 LongPet/KWS 正常运行

同样运行 45 秒，现有正式 LongPet 与 KWS bridge 保持运行。

| 指标 | 实测 |
|---|---:|
| detector avg inference | 992.33 ms |
| detector P50 / P95 | 864.13 / 1461.66 ms |
| detector avg total | 1030.19 ms |
| tracker avg | 23.34 ms |
| tracker P50 / P95 | 18.89 / 50.30 ms |
| target update rate | **8.75 Hz** |
| detector trigger rate | 0.121 Hz（约每 8.25 秒） |
| measured detector runs | 5 |
| target observations | 362（361 present） |
| CORRECTED | 4 |
| tracker LOST / REACQUIRED | 0 / 0 |
| VisionBench process CPU | 26.76% |
| RSS / peak RSS | 46,016 / 46,016 kB |

状态序列为：

```text
SEARCHING → DETECTED → TRACKING
→ CORRECTED → TRACKING（重复四次）
```

CPU 百分比是 VisionBench 自身在单核竞争下获得的时间，不含另一个 LongPet/KWS 进程；并发时
tracker P95 上升但目标更新仍超过 8 Hz，说明本方案在 KWS 负载下仍有实际价值。

### 12.4 与 V1.2 detector-only 的含义对比

V1.2 detector-only 历史实测约 1.285 FPS（独占）/ 0.738 FPS（KWS）。V2.0 的 detector 本身
没有变快，而是把 detector 降到稳定时约 0.12 Hz，并由 tracker 提供 7.7～8.8 Hz 位置更新。
因此它解决的是“连续位置感知”和常驻 CPU 占用，不是提升 CNN 推理速度。

## 13. 板端部署与复测

将交叉产物和冻结模型上传后：

```bash
install -d -o longpet -g longpet -m 0750 /home/longpet/models
install -o longpet -g longpet -m 0640 \
  /tmp/tinyissimo-person-128-longpet-v1.onnx \
  /home/longpet/models/tinyissimo-person-128-longpet-v1.onnx
install -o longpet -g longpet -m 0750 \
  /tmp/LongPetVisionBench /home/longpet/LongPetVisionBench
```

独占测试：

```bash
systemctl stop longpet.service
/home/longpet/LongPetVisionBench \
  --detector tinyissimo \
  --model /home/longpet/models/tinyissimo-person-128-longpet-v1.onnx \
  --camera /dev/video0 --tracking --warmup 1 --duration 60 \
  --tracker-interval-ms 100 --correction-ms 8000 \
  | tee /tmp/vision-v2-no-kws.log
systemctl start longpet.service
systemctl is-active longpet.service
```

KWS 并发测试时不要停止 `longpet.service`，直接重复 Benchmark。不要同时发起视频通话；正式服务
能协调 VideoCall，但独立 Benchmark 不知道应用内通话状态。

正式启用前复制 `deploy/vision/longpet-vision.conf.example` 为 systemd drop-in，确认模型路径，
再把 `LONGPET_VISION_ENABLED` 改为 1。仓库示例仍保持 0，等待十组人工场景验收。

## 14. 尚未完成的人工场景

| 场景 | 本轮状态 |
|---|---|
| 人静止 | 已初步验证 |
| 人缓慢左右移动 | 待操作者受控复测 |
| 中央走到边缘 | 仅验证了边缘目标，完整过程待测 |
| 靠近/远离 | 待测，重点检查尺度估计 |
| 部分离开画面 | 已遇到边缘/部分出框样本，仍需受控复测 |
| 完全离开 | 未受控执行；自动 LOST 路径已在实测和单测出现 |
| 重新进入 | 自动 REACQUIRED 已实测 3 次，仍需受控执行 |
| 坐姿 | 待测 |
| 背身 | 待测，主要取决于 V1.2 detector |
| KWS 同时运行 | 已验证 |

## 15. 已知限制

1. Sparse LK 没有外观模型，长时间完全遮挡后不能保证找回同一个人。
2. bbox 尺度是轻量启发式估计，快速靠近/远离可能滞后，依赖 detector 校正。
3. detector 同步运行时 tracker 暂停，位置更新会有约一次 inference 的空档。
4. V1.2 detector 的绿衣左侧困难场景仍然存在；tracker 不能初始化一个从未检测到的目标。
5. 多人目标关联仅用几何信息；没有稳定身份/re-ID。
6. tracker confidence 是工程质量分，不是统计校准概率。
7. 本轮没有增加视觉 UI overlay；目标通过 Service 信号和 Benchmark 日志输出。
8. 正式 service 仍默认禁用 Vision；这避免在人工动作验收前直接改变常驻负载。

## 16. V2.1 建议

1. 先用同一 Benchmark 完成十组受控动作，各跑无 KWS/KWS 两轮并保存日志。
2. 根据 LOST 与漂移数据调整 100 ms tracker 周期、8 s correction 周期和低可信门限。
3. 在上层增加 LEFT/CENTER/RIGHT、bbox 面积/远近和观察稳定窗口，但仍不直接驱动电机。
4. 增加目标切换策略和短时 LOST grace，必要时再评估 MOSSE，而不是直接引入重 tracker。
5. 电机阶段必须增加 observation freshness、最大转向时间、急停和视频通话/语音优先级保护。

