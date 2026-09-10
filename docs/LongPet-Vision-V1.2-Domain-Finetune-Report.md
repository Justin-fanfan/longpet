# LongPet Vision V1.2：TinyissimoYOLO LongPet domain fine-tune

> 状态：`tinyissimo-person-128-longpet-v1.onnx` 是 V1.2 候选模型，待用户板端验收。该文件尚未
> 放入 `third_party/tinyissimo-yolo/models`，本轮没有覆盖 V1.1 runtime baseline。
>
> 路径说明：本文中的 `C:\ai-work`、`D:\ai-work\longpet-realworld-person-v1` 和用户目录是
> 本次历史实验的实际复现路径，不是程序强制路径。新实验推荐使用
> `D:\ai-work\longpet-vision`，边界与目录结构见
> [仓库结构与维护说明](Repository-Structure-and-Maintenance.md)。

本轮在 PC 上完成 LongPet 实拍数据复审、数据集整理、COCO person-only checkpoint 的保守 fine-tune、静态 FP32 ONNX 导出，以及同一 test split 的 before/after 对比。没有连接、上传或操作 2K300 板端，也没有修改 `longpet.service`。

## 数据集复审

- 原始压缩包：`C:\Users\18214\Downloads\Camera Roll.zip`
- 原始素材目录：`D:\ai-work\longpet-realworld-person-v1\raw\Camera Roll`
- 有效视频：36 段，约 332.7 秒，均为 1920×1080、约 30 FPS。
- `WIN_20260909_22_10_53_Pro.mp4` 保留在 raw，但因 0.91 秒近黑片段排除出数据集。
- standalone 图片：发现 1 张 `1场景.jpg`。它不是无人图，左侧有局部人物，YOLOv8x 置信度约 0.858，且与视频抽帧不重复，因此作为 train 正样本保留。
- 确认无人并写入 negative 的 standalone 图片：0 张。没有把 teacher 漏检自动当成 negative。
- 抽帧目标 2 FPS；视频级 split；候选 560 张，其中视频近重复帧 122 张，review 候选 21 张，最终写入 539 张。

| split | 视频数 | 图片 | positive | negative | person boxes |
|---|---:|---:|---:|---:|---:|
| train | 24 | 366 | 366 | 0 | 366 |
| val | 6 | 88 | 88 | 0 | 88 |
| test | 6 | 85 | 85 | 0 | 85 |

test 视频明确为：`1场景1人绿远.mp4`、`2场景1人绿左.mp4`、`3场景1人坐白.mp4`、`3场景1人绿暗.mp4`、`3场景1人黑后.mp4`、`3场景1人黑正.mp4`。完整 train/val/test mapping 记录在数据集的 `quality_report.md` 和 `manifest.json`。

数据集文件：

- `D:\ai-work\longpet-realworld-person-v1\dataset\data.yaml`
- `D:\ai-work\longpet-realworld-person-v1\dataset\manifest.json`
- `D:\ai-work\longpet-realworld-person-v1\dataset\quality_report.md`

## test split 修复

`verify_tinyissimo_onnx.py` 新增 `--split val|test|train`，并让 PyTorch 与 ONNX 都使用 `batch=1、rect=False` 的固定 128×128 输入。这样 `--images`、P/R/mAP 和 raw parity 使用同一个 split；此前 PyTorch `YOLO.val()` 默认矩形验证、ONNX 强制方形验证造成的指标差异也被消除。

## COCO baseline 与 fine-tune 对比

两组结果都来自同一个 85 张、85 个 person instance 的 LongPet test split，指标使用最终静态 128×128 ONNX 的运行路径；PyTorch 数字与 ONNX 数字逐项一致。

| 指标 | COCO-full baseline | LongPet fine-tuned | 变化 |
|---|---:|---:|---:|
| Precision | 1.0000 | 1.0000 | ≈0 |
| Recall | 0.8813 | 0.9647 | +0.0834 |
| mAP50 | 0.9777 | 0.9856 | +0.0079 |
| mAP50-95 | 0.7963 | 0.8488 | +0.0525 |

产品式逐帧统计以 `score >= 0.25` 且最佳框 IoU >= 0.50 计为成功：

| 来源视频 | 帧数 | baseline | fine-tuned | mean IoU baseline → fine-tuned |
|---|---:|---:|---:|---:|
| 1场景1人绿远.mp4 | 6 | 83.3% | 100.0% | 0.724 → 0.854 |
| 2场景1人绿左.mp4 | 3 | 0.0% | 0.0% | 0.000 → 0.000 |
| 3场景1人坐白.mp4 | 19 | 100.0% | 100.0% | 0.879 → 0.896 |
| 3场景1人绿暗.mp4 | 11 | 90.9% | 100.0% | 0.824 → 0.935 |
| 3场景1人黑后.mp4 | 17 | 52.9% | 100.0% | 0.476 → 0.931 |
| 3场景1人黑正.mp4 | 29 | 100.0% | 100.0% | 0.921 → 0.921 |

整体正样本帧检测率由 84.7% 提升到 96.5%。`2场景1人绿左.mp4` 的 3 张测试帧仍全部低于 0.25，属于当前明确未解决的困难场景，标签没有为迎合模型而修改。

test 没有确认的无人图片，因此 negative false-positive rate 为 **N/A**，不能从本轮数据推断误报率。下一轮应单独采集同一摄像头视角的无人场景并放入 val/test。

## fine-tune 设置

使用完整 COCO person-only checkpoint 初始化，没有随机重置：

- TinyissimoYOLO-v1-small，`nc=1`，class 0=`person`
- 输入 `128×128`，静态 FP32 ONNX，opset 17
- SGD，`lr0=0.001`，batch 32，最多 30 epochs，`patience=8`
- 最佳验证 epoch 为 21，随后 early stopping
- test 未参与训练、early stopping 或超参数选择

最终文件：

- best checkpoint：`D:\ai-work\longpet-realworld-person-v1\fine-tune-v1\runs\tinyissimo-longpet-person-128\weights\best.pt`
- ONNX：`D:\ai-work\longpet-realworld-person-v1\models\tinyissimo-person-128-longpet-v1.onnx`
- ONNX input：`[1,3,128,128]`
- ONNX output：`[1,5,16]`
- ONNX SHA-256：`cb3defedb3ac01006d4caa5312c21d4a674e5a808e885067f2bc67e8f822f89c`

## ONNX parity

| 模型 | test images | max_abs_diff | mean_abs_diff | candidate 数一致 | passed |
|---|---:|---:|---:|---|---|
| COCO baseline | 85 | 0.00025177 | 0.00001221 | 是 | 是 |
| fine-tuned | 85 | 0.00036621 | 0.00000982 | 是 | 是 |

误差均低于 `atol=5e-4`，每张图 `score >= 0.25` 的 candidate 数一致。

## 可视化与验证产物

- baseline 预测：`D:\ai-work\longpet-realworld-person-v1\predictions\baseline\test`
- fine-tuned 预测：`D:\ai-work\longpet-realworld-person-v1\predictions\finetuned\test`
- baseline 逐视频统计：`D:\ai-work\longpet-realworld-person-v1\predictions\baseline\summary.json`
- fine-tuned 逐视频统计：`D:\ai-work\longpet-realworld-person-v1\predictions\finetuned\summary.json`
- baseline parity/metrics：`D:\ai-work\longpet-realworld-person-v1\baseline-test.verify.json`
- fine-tuned parity/metrics：`D:\ai-work\longpet-realworld-person-v1\finetuned-test.verify.json`

## 复现命令

### fine-tune 与导出

```powershell
& 'D:\code_qt\longpet_main\longpet\.venv-vision\Scripts\python.exe' `
  'D:\code_qt\longpet_main\longpet\scripts\vision\train_export_tinyissimo.py' `
  --upstream 'C:\ai-work\TinyissimoYOLO' `
  --data 'D:\ai-work\longpet-realworld-person-v1\dataset\data.yaml' `
  --weights 'C:\ai-work\tinyissimo-training\runs\tinyissimo-person-128-full\weights\best.pt' `
  --output-dir 'D:\ai-work\longpet-realworld-person-v1\fine-tune-v1' `
  --model-output 'D:\ai-work\longpet-realworld-person-v1\models\tinyissimo-person-128-longpet-v1.onnx' `
  --epochs 30 --batch 32 --workers 4 --device 0 --imgsz 128 `
  --lr0 0.001 --patience 8 --name tinyissimo-longpet-person-128
```

### baseline test 验证

```powershell
& 'D:\code_qt\longpet_main\longpet\.venv-vision\Scripts\python.exe' `
  'D:\code_qt\longpet_main\longpet\scripts\vision\verify_tinyissimo_onnx.py' `
  --upstream 'C:\ai-work\TinyissimoYOLO' `
  --checkpoint 'C:\ai-work\tinyissimo-training\runs\tinyissimo-person-128-full\weights\best.pt' `
  --onnx 'C:\ai-work\tinyissimo-person-128-full.onnx' `
  --images 'D:\ai-work\longpet-realworld-person-v1\dataset\images\test' `
  --data 'D:\ai-work\longpet-realworld-person-v1\dataset\data.yaml' `
  --split test --limit 1000 --atol 5e-4 `
  --output 'D:\ai-work\longpet-realworld-person-v1\baseline-test.verify.json'
```

### fine-tuned test 验证

```powershell
& 'D:\code_qt\longpet_main\longpet\.venv-vision\Scripts\python.exe' `
  'D:\code_qt\longpet_main\longpet\scripts\vision\verify_tinyissimo_onnx.py' `
  --upstream 'C:\ai-work\TinyissimoYOLO' `
  --checkpoint 'D:\ai-work\longpet-realworld-person-v1\fine-tune-v1\runs\tinyissimo-longpet-person-128\weights\best.pt' `
  --onnx 'D:\ai-work\longpet-realworld-person-v1\models\tinyissimo-person-128-longpet-v1.onnx' `
  --images 'D:\ai-work\longpet-realworld-person-v1\dataset\images\test' `
  --data 'D:\ai-work\longpet-realworld-person-v1\dataset\data.yaml' `
  --split test --limit 1000 --atol 5e-4 `
  --output 'D:\ai-work\longpet-realworld-person-v1\finetuned-test.verify.json'
```

## 用户自行执行的板端命令

本轮没有执行以下命令。确认模型文件后，由用户在 PC 上执行：

```powershell
scp 'D:\ai-work\longpet-realworld-person-v1\models\tinyissimo-person-128-longpet-v1.onnx' `
  root@192.168.137.32:/tmp/tinyissimo-person-128-longpet-v1.onnx
ssh root@192.168.137.32
```

登录板端后：

```bash
install -d -o longpet -g longpet -m 0750 /home/longpet/models
install -o longpet -g longpet -m 0640 \
  /tmp/tinyissimo-person-128-longpet-v1.onnx \
  /home/longpet/models/tinyissimo-person-128-longpet-v1.onnx

# 仅做用户主动执行的摄像头 benchmark；不会自动修改或重启 longpet.service
LONGPET_VISION_CONFIDENCE_THRESHOLD=0.25 \
/home/longpet/LongPetVisionBench \
  --detector tinyissimo \
  --model /home/longpet/models/tinyissimo-person-128-longpet-v1.onnx \
  --camera /dev/video0 \
  --duration 60 \
  --warmup 10 \
  --interval-ms 300 \
  --nms-threshold 0.45 \
  --threads 1
```

如果要让正式服务读取新模型，先由用户确认测试结果，再自行修改 `LONGPET_VISION_MODEL_PATH`；本轮没有替换正式模型或启动服务。

## 当前产品方向

后续主线是本地人物检测、人物在场与方向感知、低频 detector 配合 lightweight tracker、
人物跟随和主动交互，再探索轻量手势识别。固定摄像头视角明显偏上，跌倒检测保留为历史探索
事实，但不再作为当前 Vision roadmap 的下一版本必做功能。
