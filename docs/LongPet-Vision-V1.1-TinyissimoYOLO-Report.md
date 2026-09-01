# LongPet Vision V1.1：TinyissimoYOLO person-only 性能探索报告

- 日期：2026-09-01
- LongPet 分支：以本轮工作区当前 `main` 为基线（提交前）
- 开发板：Loongson 2K300，LoongArch64，单核 Linux
- 板端地址：`192.168.137.32`
- 本轮范围：本地 `person` detection、训练/导出复现、通用 benchmark；不包含跟随、电机和跌倒检测

## 1. 结论摘要

本轮已将 TinyissimoYOLO-v1-small person-only 接入既有 `VisionDetectorPort`，保留
FastestDet 作为可切换基线。应用默认 detector 改为 `tinyissimo`，但正式服务仍保持
`LONGPET_VISION_ENABLED=0`，不会未经验收就在单核板端常驻抢占 KWS。

最终判断：

| 问题 | 结论 |
|---|---|
| Tinyissimo 是否能检测人物 | 能检测清晰、较近人物；同图板端稳定 `person_count=1`，但 COCO 子集 recall 仅约 0.176 |
| FP32 单图 inference | 平均 `635.4 ms`，P50 `635.2 ms`，P95 `637.3 ms` |
| 相比 FastestDet speedup | inference `3.78×`，单图 total `3.65×`；不是数量级提升 |
| USB Camera 完整链路 | inference `695.1 ms`，total `725.6 ms`，`1.377 FPS` |
| KWS 并发影响 | inference 增至 `1148.7 ms`（+65.2%），吞吐降至 `0.814 FPS` |
| 是否建议作为本地人物 detector | 建议替代 FastestDet 做低频 detector + tracker 研究，不建议作为安全或高召回 detector |
| 是否已满足进入 Vision V2 跟随条件 | 仅满足算法原型条件；准确率、KWS 并发周期和运动安全尚不满足直接驱动电机 |

## 2. 为什么正式输入是 128×128，而不是名义 112×112

Tinyissimo 官方资料讨论过 112×112 变体，但当前官方仓库的
`tinyissimo-v1-small.yaml` 包含 5 次 2 倍下采样，总步长为 32。官方训练/导出入口会通过
Ultralytics `check_imgsz` 自动把请求的 112 上调为 128。实测：

- 直接请求 112：导出器警告并产生 `[1,3,128,128]`；
- 绕过检查强制 112：最后特征网格只有 3×3，输出为 `[1,5,9]`；
- 正常 128：4×4 网格，输出为 `[1,5,16]`。

因此不能把一个实际 128 模型标成 112，也不应把强制 112 的异常图当作正式正确性基线。
LongPet adapter 会拒绝边长不是 32 倍数的图。若后续还要探索更小输入，正确候选是 96，
不是 88。

参考：

- [TinyissimoYOLO 官方仓库](https://github.com/ETH-PBL/TinyissimoYOLO)
- [Ultra-Efficient On-Device Object Detection 论文](https://arxiv.org/abs/2311.01057)

## 3. 现有架构与本轮调用链

本轮没有让 UI、页面或 Widget 接触 ONNX Runtime、摄像头或平台 API。运行链保持为：

```text
Application
  -> VisionDetectorFactory（读取 LONGPET_VISION_DETECTOR）
      -> TinyissimoYoloAdapter 或 FastestDetAdapter
  -> VisionService（工作线程、latest-frame-only、暂停/恢复）
      -> CameraSourcePort
          -> CameraCaptureAdapter（/dev/video0，共享消费者生命周期）
      -> VisionDetectorPort
          -> OpenCV preprocess + ONNX Runtime + detector-specific decode/NMS
  -> Controller/UI 的既有消费端
```

摄像头共享关系没有被破坏：Vision 和视频通话仍复用一个 `CameraCaptureAdapter`。只有消费者
进入活动状态才打开设备；VisionService 忙于推理时保留最新帧而不累积旧帧。视频通话可暂停
Vision，结束后再恢复。

## 4. 上游边界与可重复训练流程

### 4.1 上游固定方式

- 上游：`ETH-PBL/TinyissimoYOLO`
- 固定提交：`19bea4bd1ea1e2c29a6ee6b14bd7494ce8c6ba25`
- 使用结构：`ultralytics/cfg/models/tinyissimo/tinyissimo-v1-small.yaml`
- LongPet 不复制整个 Ultralytics fork；只保存提交号、最小补丁、依赖清单和自动化脚本。

最小补丁包含：

1. 按官方 v1 训练入口需要将 `Detect.reg_max` 设为 16；
2. 强制使用 Torch legacy ONNX exporter。新 Torch 的默认 dynamo exporter 曾产生名义 opset
   17、实际却带 opset 18 `Split(num_outputs)` 属性的图，ONNX Runtime 无法加载。

`setup_tinyissimo_upstream.py` 会固定 checkout、验证工作树只包含完全一致的补丁，拒绝其他
脏改动。训练数据、`runs` 和 checkpoint 均位于仓库之外。

### 4.2 COCO 2017 person-only 子集

数据由 `prepare_coco_person_dataset.py` 自动生成，不手改标签：

| split | person 图片 | negative 图片 | person boxes |
|---|---:|---:|---:|
| train2017 | 2,000 | 500 | 7,881 |
| val2017 | 500 | 125 | 1,903 |

- 原 COCO `person` 映射为 class 0；
- crowd-only person 图片不会被误当作 negative；
- 固定随机种子 `20260901`；
- manifest SHA-256：`d0ba366f64bf4e873bf44e9a5f3f80e3574b7e53dda1d7f7262961074d6b5660`；
- train annotation SHA-256：`610fce4944abdeb15354cc765333805529359d12d88f2f711393ca586901d01d`；
- val annotation SHA-256：`e8c7f7908f1d7278341fae127d0da654f102f11bd7b21d8aeefa635b8c810b6f`。

这是为了在可接受 CPU 训练时间内先回答板端性能问题的代表性子集，不等价于完整 COCO
训练。准确率结论必须带上这一限制。

### 4.3 训练与静态 FP32 ONNX

- 从头训练（上游无可直接使用的正式 v1-small person-only checkpoint）；
- 输入 128×128，`nc=1`，batch 64，最多 100 epochs，early-stop patience 25；
- SGD、固定 seed、CPU；
- 导出静态 FP32 ONNX opset 17；
- ONNX input：`[1,3,128,128]`；
- ONNX output：`[1,5,16]`，依次为 `cx, cy, w, h, person_score`；
- PyTorch 参数量：402,057；融合推理图报告 401,297 parameters / 3.2 GFLOPs；
- ONNX 大小：1,620,598 bytes；
- ONNX SHA-256：`f647b4704a1856195a3c362c8134f0633c17db86ecfeb7de5d1fe31ec7f4c149`。

详细复现命令见 `third_party/tinyissimo-yolo/README.md` 和 `deploy/配置说明.md`。

### 4.4 许可证提示

固定上游仓库当时没有顶层 `LICENSE` 文件，但源码头和导出 metadata 标注 AGPL-3.0。
仓库当前不复制完整 fork或上游权重；LongPet 自训 ONNX 是否对外分发仍应在正式发布前完成
许可证审查。本报告不把“代码可运行”解释为已经完成法务确认。

## 5. C++ detector 实现

`TinyissimoYoloAdapter` 根据真实导出 graph 执行：

1. 校验单输入/单输出、静态 float32、方形且 32 对齐；
2. JPEG 解码；
3. RGB、114 灰边的居中 letterbox、`/255`、NCHW；
4. ONNX Runtime CPUExecutionProvider，默认 1 个 inference thread；
5. 按 `[1,5,N]`（兼容 `[1,N,5]`）解析输出；
6. 将 `cxcywh` 逆映射回原图，裁剪，person-only NMS；
7. 返回统一 `VisionFrameResult`，包含 decode/preprocess/inference/postprocess/total。

适配器不认识 YOLO 的训练类，也没有 Python/Ultralytics 板端依赖。缺模型、graph 不匹配、
摄像头不可用或推理异常均通过既有 Vision 降级路径报告，不使 LongPet 主程序退出。

## 6. PC 正确性验证

验证分两层：

1. 对真实图片使用完全相同的 letterbox tensor，比较 PyTorch 与 ONNX Runtime 原始输出；
2. 在同一 val 子集上分别执行 PyTorch checkpoint 与 ONNX 的完整验证，比较 P/R/mAP。

50 张真实验证图使用同一输入 tensor 比较原始输出：

- 最大 absolute difference：`8.3923e-5`；
- 平均 absolute difference：`6.5612e-6`；
- 验收阈值：`1e-4`，通过；
- 每张图 `score >= 0.25` 的 PyTorch/ONNX candidate 数完全相同。

625 张、1,903 个 person 实例的完整验证：

| backend | precision | recall | mAP50 | mAP50-95 |
|---|---:|---:|---:|---:|
| PyTorch best checkpoint | 0.45824 | 0.16553 | 0.18497 | 0.07927 |
| ONNX Runtime | 0.44609 | 0.17604 | 0.19205 | 0.08114 |

两组完整指标有轻微差异，来自微小浮点差异经过置信度排序/NMS/PR 曲线后的放大；原始输出误差、
逐图阈值 candidate 和总体指标均在可接受一致性范围内。准确率本身偏低，尤其 recall，后续不能
把“导出一致”误解为“模型精度已经足够”。

正式 ONNX 在清晰网球运动员图片 `COCO val 000000019432.jpg` 上得到 0.869 person
confidence；同一文件上传板端后，20 次 measured inference 均返回 `person_count=1`。这证明
训练、导出和 C++ decode 链路不是空模型；泛化能力仍以完整验证集的低 recall 为准。

## 7. 自动化测试与构建

### 7.1 自动化覆盖

- Tinyissimo `[1,5,N]` decode；
- letterbox 逆映射；
- 低置信度过滤；
- 重叠框 NMS；
- Tinyissimo 模型缺失时安全降级；
- detector 工厂默认 Tinyissimo、显式 FastestDet 回退；
- 既有 latest-frame-only、摄像头共享和视频通话生命周期回归。

Windows Release（Vision OFF，用于验证无 OpenCV/ORT 的普通开发构建）重新链接成功，CTest：

```text
LongPet.V02       Passed  27.16 s
LongPet.VisionV1  Passed   0.99 s
100% tests passed, 0 failed
```

首次运行曾在进入第一个用例前卡住。GDB 线程栈显示 QtTest crash handler 加载了
`C:\mingw64\bin` 的 `libstdc++/libwinpthread`，而应用由 Qt 配套 MinGW 13.1 构建；将
`D:\Qt\6.11.0\mingw_64\bin` 和 `D:\Qt\Tools\mingw1310_64\bin` 放到 `PATH` 前面后立即恢复。
这是本机运行库混用问题，不是 Vision 线程或 detector 回归。

### 7.2 LoongArch Release 交叉构建

仅在 WSL Ubuntu 24.04 交叉编译。最终产物：

| artifact | 类型 | SHA-256 |
|---|---|---|
| `LongPet` | LoongArch64 ELF | `bcd4b6c63a0c814397209c6fcc49a0533ef8d469d3ad5813440015fc046d22ed` |
| `LongPetVisionBench` | LoongArch64 ELF | `7e5c052cd44758d3fa12264f9849800ce8d3f80e08376fe23b3ee86fc8f6d13a` |

## 8. 2K300 板端 Benchmark

### 8.1 方法

- 板端：`192.168.137.32`，单核；
- provider：ONNX Runtime CPUExecutionProvider，1 thread；
- 阈值：FastestDet 0.65；Tinyissimo 0.25；NMS 0.45；
- 纯模型比较使用同一张 COCO JPEG，停止 `longpet.service` 排除 KWS；
- USB Camera 测试复用 `/dev/video0` 和 `VisionService` latest-frame-only 完整链路；
- KWS 并发测试保持正式 `longpet.service` 运行；
- 每组记录 warmup，性能统计只使用 measured frames；
- 所有停止服务的测试均在清理路径恢复并检查 `longpet.service=active`。

### 8.2 无 KWS：同图纯 detector

| detector | model | input | decode avg | preprocess avg | inference avg / P50 / P95 | post avg | total avg / P50 / P95 | FPS | CPU | RSS/peak |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| FastestDet | 998,169 B | 352 | 33.695 | 33.366 | 2399.16 / 2396.17 / 2407.17 | 0.394 | 2466.68 / 2463.83 / 2475.08 | 0.405 | 99.75% | 47,536 / 47,536 kB |
| Tinyissimo | 1,620,598 B | 128 | 33.367 | 6.060 | 635.41 / 635.19 / 637.33 | 0.033 | 674.93 / 674.95 / 676.73 | 1.481 | 99.80% | 41,840 / 41,840 kB |

每个模型 warmup 5 次、measured 20 次，使用同一张 COCO JPEG 和同一个最终 benchmark binary。
FastestDet 与 Tinyissimo 均稳定返回一个 person；表格没有混用开发期未训练 smoke 模型数据。

### 8.3 USB Camera 完整链路

| detector / 场景 | measured frames | preprocess | inference P50/P95 | total P50/P95 | FPS | process CPU | RSS/peak |
|---|---:|---:|---:|---:|---:|---:|---:|
| FastestDet，无 KWS | 16 | 40.209 ms | 2623.07 / 2661.10 ms | 2689.18 / 2727.34 ms | 0.355 | 95.58% | 39,376 / 47,136 kB |
| Tinyissimo，无 KWS | 76 | 6.639 ms | 694.67 / 699.40 ms | 725.27 / 730.35 ms | 1.377 | 95.55% | 41,776 / 41,840 kB |
| Tinyissimo，KWS/LongPet 并发 | 43 | 10.885 ms | 1132.07 / 1212.72 ms | 1196.05 / 1250.16 ms | 0.814 | 61.69% | 42,224 / 42,224 kB |

并发组前后 KWS 进程约占 37.0% / 33.5% CPU、约 62 MB RSS；benchmark 与 KWS 合计接近
占满单核。并发期间 detector 进程 CPU 比例下降，等待 CPU 的壁钟时间直接反映在 inference
延迟上。摄像头测试期间画面中人物并非始终存在，因此 positive frame 数只作为链路冒烟证据，
不作为准确率指标。

### 8.4 Speedup 与判断区间

同图纯 detector：

- inference speedup：`2399.16 / 635.41 = 3.78×`；
- total speedup：`2466.68 / 674.93 = 3.65×`。

USB Camera 完整链路：

- inference speedup：`2626.73 / 695.13 = 3.78×`；
- total speedup：`2692.24 / 725.56 = 3.71×`。

Tinyissimo 输入像素数只有 FastestDet 的 1/7.56，但模型参数和文件反而约为 FastestDet 的
1.67× / 1.62×，所以实际只有约 3.8× 加速而非 7.6×，更没有达到 10×。无 KWS 的 695 ms
落入“500–1000 ms，可考虑 detector + tracker”档；KWS 并发的 1149 ms 落入“1–1.5 s，仍需
优化”档。INT8 和 96×96 没有在本轮贸然加入：FP32 的主要结论已经明确，而低 recall 比继续
压低输入更值得先解决。

用户给定判断区间以 inference 为准：

- `<300 ms`：非常理想；
- `300–500 ms`：可进入 Vision V2；
- `500–1000 ms`：适合 detector + tracker；
- `1–1.5 s`：仍需优化；
- `>1.5 s`：不适合作为直接跟随 detector。

## 9. 配置与部署

核心配置：

```ini
Environment="LONGPET_VISION_ENABLED=0"
Environment="LONGPET_VISION_DETECTOR=tinyissimo"
Environment="LONGPET_VISION_MODEL_PATH=/home/longpet/models/tinyissimo-yolo-v1-small-person-128.onnx"
Environment="LONGPET_VISION_CONFIDENCE_THRESHOLD=0.25"
Environment="LONGPET_VISION_NMS_THRESHOLD=0.45"
Environment="LONGPET_VISION_INFERENCE_THREADS=1"
Environment="LONGPET_VISION_INTERVAL_MS=300"
```

完整 drop-in 示例位于 `deploy/vision/longpet-vision.conf.example`。FastestDet 回退只需改
detector 和 model path，不修改 Service/UI。

## 10. 已知限制与后续建议

1. 128×128 对小人物和远距离人物天然不利；COCO 总体 mAP 不能代表近距离单人跟随的全部体验，
   但它能揭示检测模型的上限风险。
2. 当前训练是代表性 COCO 子集从头训练，不是完整 COCO，也没有官方 person-only 预训练权重。
3. 当前 FP32 CPU 已回答第一优先级；只有 FP32 正确性通过且模型值得保留时，才应投入 INT8。
4. 单核上 KWS 与 detector 必然争用。进入 V2 后应采用低频 detector + 轻量 tracker，而不是
   每个摄像头帧都跑 CNN。
5. V1.1 不包含 tracker、目标选择、丢失重找、电机边界或安全策略，不能直接驱动底盘。
6. `LONGPET_VISION_ENABLED` 继续默认关闭；最终是否打开取决于本报告的准确率、摄像头和 KWS
   并发结论。

最终建议：Tinyissimo 应替代 FastestDet 成为 LongPet 下一轮的**实验性低频人物 detector**，
配合 tracker、目标丢失重检和 KWS 调度继续做 Vision V2 原型；它不应以当前权重直接驱动
人物跟随电机。下一步最小工作应先采集 LongPet 实际摄像头视角的近/中/远距离人物验证集，
评估 recall 并做场景微调，再实现不接电机的 detector + tracker 可视化闭环。只有跟踪稳定性、
丢失保护、控制频率和紧急停止均验证后，才进入真实运动测试。

本轮板端测试结束后最终确认：

```text
longpet.service: ActiveState=active, SubState=running
/dev/video0: present
```

## 11. 新增/修改文件说明

核心运行代码：

- `src/platform/TinyissimoYoloAdapter.{h,cpp}`：graph 校验、letterbox、ORT 推理；
- `src/platform/TinyissimoYoloPostProcessor.{h,cpp}`：person-only decode、逆映射和 NMS；
- `src/platform/VisionDetectorFactory.{h,cpp}`：按环境选择 Tinyissimo/FastestDet；
- `src/app/Application.{h,cpp}`：组合根改为持有通用 `VisionDetectorPort`；
- `src/model/VisionModels.h`：补充 detector/model/参数量可观测信息；
- `src/platform/FastestDetAdapter.cpp`：补齐基线 detector、模型大小和参数量信息；
- `tools/LongPetVisionBench.cpp`：通用双 detector benchmark、P50/P95、CPU/RSS；
- `CMakeLists.txt`：纳入新 adapter/factory。

训练、部署和测试：

- `scripts/vision/setup_tinyissimo_upstream.py`：固定上游并严格应用最小补丁；
- `scripts/vision/prepare_coco_person_dataset.py`：可重复 COCO person-only/negative 数据准备；
- `scripts/vision/train_export_tinyissimo.py`：训练、静态 ONNX 导出、shape/checker/ORT 冒烟；
- `scripts/vision/verify_tinyissimo_onnx.py`：PyTorch/ONNX 原始输出和完整指标对比；
- `scripts/run-vision-benchmark.sh`：兼容 V1 旧调用并支持 detector、阈值和线程配置；
- `tests/VisionV1Test.cpp`：Tinyissimo decode/NMS、缺模型、factory 回归；
- `deploy/longpet.service`、`deploy/vision/*`、`deploy/配置说明.md`：默认 detector、drop-in、
  新板地址和复现/安装步骤；
- `third_party/tinyissimo-yolo/*`：固定提交、补丁、训练依赖、模型 provenance 和正式 ONNX；
- `.gitignore`：阻止 dataset/runs/checkpoint/Python cache 误入仓库；
- 本报告：完整实验过程、数字、限制和后续判断。
