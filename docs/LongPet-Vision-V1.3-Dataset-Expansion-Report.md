# LongPet Vision V1.3 实拍数据集扩充报告

本轮在 `vision-v1.1` 分支完成了 LongPet 实拍数据扩充、视频级划分、YOLOv8x teacher 标注、人工复核、V1.2 checkpoint 微调、PyTorch/ONNX 对比和可视化。V1.2 数据集及其 85 张冻结 test 全程未修改；V1.3 模型仍是候选模型，没有替换部署默认模型，也没有执行板端操作。

## 结论

V1.3 对新增场景有明显收益，但出现了旧场景回退，因此当前推荐仍是 V1.2 模型作为兼顾旧场景的主模型，V1.3 模型保留为新场景候选和后续 replay 微调的起点。

- 新 V1.3 test：正样本逐帧检测率由 **47.37%** 提高到 **71.05%**；confirmed-empty 的 negative false-positive rate 由 **28.57%** 降到 **0%**。
- 冻结 V1.2 test：正样本逐帧检测率由 **96.47%** 降到 **88.24%**；mAP50-95 由 **0.8488** 降到 **0.8086**。
- 原困难视频 `2场景1人绿左.mp4` 仍然是 **0/3**，V1.3 没有改善该场景。
- V1.3 ONNX 通过 `onnx.checker`、ONNX Runtime 加载和 PyTorch/ONNX parity；候选文件没有复制到 `third_party`，也没有写入板端。

## 新素材审计

原始素材目录：`C:\Users\18214\Downloads\4场景及补充`

共发现 **19 段视频、0 张 standalone 图片**，总时长 **174.375 秒**。视频均为 MP4；前两段是 1280×720，其余为 1920×1080；帧率约 29.4–30.0 FPS。原始文件保留在原目录，同时复制到新的只读快照目录：

`D:\ai-work\longpet-realworld-person-v2\raw`

完整文件级审计、SHA-256、分辨率、FPS、时长、场景、距离、位置、姿态、光照和 confirmed-empty 标记见：

`D:\ai-work\longpet-realworld-person-v2\audit\source_inventory.json`

| 场景/素材 | 视频数 | 时长约 | 内容审计 |
|---|---:|---:|---|
| 1场景 | 2 | 23.4 s | 空走廊/室内视角，无人 |
| 2场景 | 2 | 13.1 s | 空窗户、门和室内视角，无人 |
| 3场景 | 2 | 13.2 s | 空房间/床铺/窗帘视角，无人 |
| 4场景空场 | 1 | 7.5 s | 空门口/室内视角，无人 |
| 4场景白 | 6 | 63.0 s | 单人，白光；左/右/正/后/远/坐 |
| 4场景绿 | 6 | 54.2 s | 单人，绿色偏光；左/右/正/后/远/坐 |

7 段空场视频先通过全时长抽帧总览确认无人，再用 teacher 检测作为交叉检查。teacher 在其中 13 张空帧产生了误报候选；这些帧经人工总览确认仍然无人，因此作为真实 negative 保留。没有把“有人视频的 teacher 无输出”自动当作 negative。

审核图：

- `D:\ai-work\longpet-realworld-person-v2\audit\contact_sheet.jpg`
- `D:\ai-work\longpet-realworld-person-v2\audit\empty_teacher_hits.jpg`
- `D:\ai-work\longpet-realworld-person-v2\audit\no_detection_review.jpg`

## 数据集生成

V1.3 数据集根目录：`D:\ai-work\longpet-realworld-person-v2\dataset`

目录包含：

- `images/train|val|test`
- `labels/train|val|test`
- `data.yaml`
- `manifest.json`
- `quality_report.md`
- `review/frame_review.csv`、teacher review 图和 contact sheet

抽帧目标为约 2 FPS；同一视频内使用 dHash 汉明距离 ≤2 且 32×18 灰度平均差 ≤1.5 的规则去掉相邻近重复帧。共保留 315 张候选图，去掉 44 张重复帧，没有坏解码或极端黑帧被写入数据集。

teacher 使用：

`D:\ai-work\longpet-realworld-person-v1\teacher_models\yolov8x.pt`

普通清晰帧保留最高置信度的 person 框。23 张边缘、近距离、暗光、背身、低置信度或 teacher 漏检帧经人工审核后纳入，其中 10 张使用覆盖到可见人体范围的保守人工框；所有框均裁剪到图像边界并转换成单类别 YOLO 格式。最终没有未处理的 `no_detection` 或 `low_confidence` 候选。

### 视频级 split

同一视频的帧只进入一个 split，未把相邻帧随机拆分。新增 test 约占 16%，包含空场、远距离白光和远距离绿光三类未用于训练的视频。

| split | 来源视频数 | 图片 | positive | negative | person boxes |
|---|---:|---:|---:|---:|---:|
| train | 11 | 170 | 150 | 20 | 150 |
| val | 5 | 93 | 38 | 55 | 38 |
| test（V1.3 New Test） | 3 | 52 | 38 | 14 | 38 |
| 合计 | 19 | 315 | 226 | 89 | 226 |

train 视频：`1场景_1.mp4`、`2场景_1.mp4`、`3场景_1.mp4`、`4场景1人白正/白左/白右/白坐.mp4`、`4场景1人绿正/绿左/绿右/绿坐.mp4`。

val 视频：`1场景_2.mp4`、`2场景_2.mp4`、`3场景_2.mp4`、`4场景1人白后.mp4`、`4场景1人绿后.mp4`。

test 视频：`4场景.mp4`、`4场景1人白远.mp4`、`4场景1人绿远.mp4`。

## V1.3 微调

初始化 checkpoint：

`D:\ai-work\longpet-realworld-person-v1\fine-tune-v1\runs\tinyissimo-longpet-person-128\weights\best.pt`

训练输出：

`D:\ai-work\longpet-realworld-person-v2\fine-tune-v2\runs\tinyissimo-longpet-person-128-v2`

候选 checkpoint：

`D:\ai-work\longpet-realworld-person-v2\fine-tune-v2\runs\tinyissimo-longpet-person-128-v2\weights\best.pt`

训练保持 TinyissimoYOLO-v1-small、`nc=1`、128×128、FP32，使用 40 epochs、batch 32、`lr0=0.0005`、patience 10，从 V1.2 best.pt 继续微调。V1.3 test 没有参与 early stopping 或参数选择。

复现命令：

```powershell
& 'D:\code_qt\longpet_main\longpet\.venv-vision\Scripts\python.exe' `
  'D:\code_qt\longpet_main\longpet\scripts\vision\prepare_longpet_person_v2_dataset.py' `
  --raw-dir 'D:\ai-work\longpet-realworld-person-v2\raw' `
  --output 'D:\ai-work\longpet-realworld-person-v2\dataset' `
  --teacher 'D:\ai-work\longpet-realworld-person-v1\teacher_models\yolov8x.pt' `
  --upstream 'C:\ai-work\TinyissimoYOLO' `
  --sample-fps 2 --teacher-imgsz 1280 --teacher-conf 0.10 --device 0

& 'D:\code_qt\longpet_main\longpet\.venv-vision\Scripts\python.exe' `
  'D:\code_qt\longpet_main\longpet\scripts\vision\train_export_tinyissimo.py' `
  --upstream 'C:\ai-work\TinyissimoYOLO' `
  --data 'D:\ai-work\longpet-realworld-person-v2\dataset\data.yaml' `
  --output-dir 'D:\ai-work\longpet-realworld-person-v2\fine-tune-v2' `
  --model-output 'D:\ai-work\longpet-realworld-person-v2\models\tinyissimo-person-128-longpet-v2.onnx' `
  --weights 'D:\ai-work\longpet-realworld-person-v1\fine-tune-v1\runs\tinyissimo-longpet-person-128\weights\best.pt' `
  --epochs 40 --batch 32 --workers 4 --device 0 --imgsz 128 `
  --lr0 0.0005 --patience 10 --name tinyissimo-longpet-person-128-v2
```

## Frozen V1.2 Test 与 New V1.3 Test

Frozen V1.2 Test 是原有的 85 张图片及标签，路径为：

`D:\ai-work\longpet-realworld-person-v1\dataset\images\test`

它没有 negative，且没有被改动、重新标注或重新划分。New V1.3 Test 是本轮的 52 张新图，其中 38 张 positive、14 张 confirmed-empty negative。

### PyTorch / ONNX split metrics

下表的两列均来自同一个 `verify_tinyissimo_onnx.py` 运行；ONNX 结果不是单独重新调阈值得到的。完整 parity JSON 在 `D:\ai-work\longpet-realworld-person-v2\metrics`。

| 模型 | 测试集 | P | R | mAP50 | mAP50-95 |
|---|---|---:|---:|---:|---:|
| V1.2 PT/ONNX | Frozen V1.2 Test | 0.99997 | 0.96471 | 0.98564 | 0.84877 |
| V1.3 PT/ONNX | Frozen V1.2 Test | 1.00000 | 0.95020 | 0.97837 | 0.80863 |
| V1.2 PT/ONNX | New V1.3 Test | 1.00000 | 0.46887 | 0.70624 | 0.29888 |
| V1.3 PT/ONNX | New V1.3 Test | 0.89608 | 0.78947 | 0.89592 | 0.61359 |

### 产品式逐帧指标（score ≥ 0.25，IoU ≥ 0.50）

| 模型 | 测试集 | positive frames | detection rate | negative frames | negative FPR | mean best IoU |
|---|---|---:|---:|---:|---:|---:|
| V1.2 | Frozen V1.2 Test | 85 | 96.47% | 0 | N/A | 0.8810 |
| V1.3 | Frozen V1.2 Test | 85 | 88.24% | 0 | N/A | 0.8005 |
| V1.2 | New V1.3 Test | 38 | 47.37% | 14 | 28.57% | 0.3723 |
| V1.3 | New V1.3 Test | 38 | 71.05% | 14 | 0% | 0.5943 |

V1.3 对新场景的检测率和空场误报均明显改善，但冻结集回退，因而本轮不把 V1.3 设为默认模型。

### 每个来源视频

`mean confidence` 是该视频所有阈值内预测框的平均分数；没有预测时记为 `—`。`mean IoU` 是每张 positive 图的最佳预测框与 GT 框 IoU 的平均。

| 模型 | 来源视频 | frames | detected | detection rate | mean confidence | mean IoU |
|---|---|---:|---:|---:|---:|---:|
| V1.2 | 1场景1人绿远.mp4 | 6 | 6 | 100% | 0.7731 | 0.8346 |
| V1.2 | 2场景1人绿左.mp4 | 3 | 0 | 0% | — | 0.0000 |
| V1.2 | 3场景1人坐白.mp4 | 19 | 19 | 100% | 0.8538 | 0.8948 |
| V1.2 | 3场景1人绿暗.mp4 | 11 | 11 | 100% | 0.8973 | 0.9358 |
| V1.2 | 3场景1人黑后.mp4 | 17 | 17 | 100% | 0.8672 | 0.9339 |
| V1.2 | 3场景1人黑正.mp4 | 29 | 29 | 100% | 0.8621 | 0.9210 |
| V1.3 | 1场景1人绿远.mp4 | 6 | 5 | 83.33% | 0.7409 | 0.7204 |
| V1.3 | 2场景1人绿左.mp4 | 3 | 0 | 0% | — | 0.0000 |
| V1.3 | 3场景1人坐白.mp4 | 19 | 19 | 100% | 0.8050 | 0.8741 |
| V1.3 | 3场景1人绿暗.mp4 | 11 | 5 | 45.45% | 0.5062 | 0.4188 |
| V1.3 | 3场景1人黑后.mp4 | 17 | 17 | 100% | 0.8199 | 0.9213 |
| V1.3 | 3场景1人黑正.mp4 | 29 | 29 | 100% | 0.8480 | 0.9257 |
| V1.2 | 4场景.mp4（空） | 14 | — | — | 0.3111 | — |
| V1.2 | 4场景1人白远.mp4 | 20 | 12 | 60.00% | 0.5472 | 0.4543 |
| V1.2 | 4场景1人绿远.mp4 | 18 | 6 | 33.33% | 0.5396 | 0.2811 |
| V1.3 | 4场景.mp4（空） | 14 | — | — | — | — |
| V1.3 | 4场景1人白远.mp4 | 20 | 16 | 80.00% | 0.5077 | 0.6678 |
| V1.3 | 4场景1人绿远.mp4 | 18 | 11 | 61.11% | 0.7179 | 0.5127 |

`2场景1人绿左.mp4` 在 V1.2 和 V1.3 均为 0/3，仍需单独补充更接近原始困难构图的标注/回放样本。

## 可视化和统计输出

完整逐帧输出位于：

- `D:\ai-work\longpet-realworld-person-v2\predictions\v1.2\frozen-test`
- `D:\ai-work\longpet-realworld-person-v2\predictions\v1.2\new-test`
- `D:\ai-work\longpet-realworld-person-v2\predictions\v1.3\frozen-test`
- `D:\ai-work\longpet-realworld-person-v2\predictions\v1.3\new-test`

每个目录包含带 GT/预测框的图片、`summary.json`、`per_frame.csv`、`per_video.csv` 和 `review_contact_sheet.jpg`。审核总览集中展示漏检、低置信度、错误框和空场景误报。

## ONNX 验证

候选 ONNX：

`D:\ai-work\longpet-realworld-person-v2\models\tinyissimo-person-128-longpet-v2.onnx`

- input：`[1, 3, 128, 128]`，`tensor(float)`
- output：`[1, 5, 16]`，`tensor(float)`
- opset：17
- `onnx.checker`：passed
- ONNX Runtime：`CPUExecutionProvider` load passed
- V1.3 frozen parity：`max_abs_diff=0.00020218`，`mean_abs_diff=0.00000856`，passed at `atol=5e-4`
- V1.3 new parity：`max_abs_diff=0.00042725`，`mean_abs_diff=0.00001037`，passed at `atol=5e-4`
- SHA-256：`7d9331f874999366bed73f337c297204ba1c71f4ab792fab525a76dac2fbe373`

候选 ONNX 没有替换 `third_party` baseline、deploy 默认模型或 V1.2 ONNX。

## 仓库改动

新增/修改的仓库文件：

- `scripts/vision/prepare_longpet_person_v2_dataset.py`：V1.3 视频级 split、约 2 FPS 抽帧、去重、空场景确认、teacher 标注和 manifest/report。
- `scripts/vision/evaluate_longpet_test.py`：增加 per-video `frames`、`detected_frames`、`mean_confidence`，并修正有 negative 时的 FPR 说明。
- `scripts/vision/verify_tinyissimo_onnx.py`：输出 JSON 前自动创建父目录。
- `docs/LongPet-Vision-V1.3-Dataset-Expansion-Report.md`：本报告。

视频、图片、labels、teacher、runs、checkpoint、predictions 和 V1.3 ONNX 都保留在 `D:\ai-work` 工作区，不应提交 Git。

## 用户后续自行执行的板端候选命令

本轮没有执行 SSH、SCP、`192.168.137.32`、`longpet.service` 或 `LongPetVisionBench`。确认 V1.3 是否值得部署前，由用户自行执行：

```powershell
scp 'D:\ai-work\longpet-realworld-person-v2\models\tinyissimo-person-128-longpet-v2.onnx' `
  root@192.168.137.32:/tmp/tinyissimo-person-128-longpet-v2.onnx
ssh root@192.168.137.32
```

登录板端后只做临时 benchmark：

```bash
install -d -o longpet -g longpet -m 0750 /home/longpet/models
install -o longpet -g longpet -m 0640 \
  /tmp/tinyissimo-person-128-longpet-v2.onnx \
  /home/longpet/models/tinyissimo-person-128-longpet-v2.onnx

LONGPET_VISION_CONFIDENCE_THRESHOLD=0.25 \
/home/longpet/LongPetVisionBench \
  --detector tinyissimo \
  --model /home/longpet/models/tinyissimo-person-128-longpet-v2.onnx \
  --camera /dev/video0 \
  --duration 60 \
  --warmup 10 \
  --interval-ms 300 \
  --nms-threshold 0.45 \
  --threads 1
```

该候选模型不应在用户确认前替换正式服务模型；当前 V1.2 仍是冻结旧场景表现更好的主模型。
