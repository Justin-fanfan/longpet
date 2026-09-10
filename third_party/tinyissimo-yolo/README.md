# TinyissimoYOLO integration boundary

LongPet does not vendor the complete TinyissimoYOLO/Ultralytics fork. Training
uses a pinned checkout of the official repository and keeps datasets, runs and
checkpoints outside this source tree.

- upstream: `https://github.com/ETH-PBL/TinyissimoYOLO.git`
- pinned revision: see `UPSTREAM_COMMIT`
- architecture: `ultralytics/cfg/models/tinyissimo/tinyissimo-v1-small.yaml`
- runtime artifact: static FP32 ONNX only; the C++ product has no Python or
  Ultralytics dependency

Run `scripts/vision/setup_tinyissimo_upstream.py` to clone and patch a training
checkout. The patch makes the v1 `reg_max=16` change required by upstream's
`a_train_export.py` and forces Torch's legacy ONNX exporter. Without the latter,
new Torch releases can emit a nominal opset-17 graph containing an unsupported
opset-18 `Split(num_outputs)` attribute which ONNX Runtime rejects.

The reproduced development environment is recorded in
`requirements-training.txt` for the CPU environment.
`requirements-training-cu300.txt` captures the reproduced CUDA 13.0 environment.
Create an isolated Python 3.11 environment; do not install either dependency set
into the board image or LongPet runtime environment.

The upstream files identify the fork as AGPL-3.0, but the pinned repository has
no top-level `LICENSE` file. This directory contains no copied fork source or
upstream weights. `models/` contains only the LongPet-trained derivative and its
provenance note. Confirm the upstream licensing terms before distributing that
artifact beyond this project/team.

Input-size note: v1-small has stride 32. Upstream rounds a requested 112 image
size to 128. LongPet uses 128×128 as the correctness baseline and will not label
that graph as 112×112. A forced 112 graph has only a 3×3 final feature map and is
not accepted by the production adapter.

## Runtime and candidate model status

- `models/tinyissimo-yolo-v1-small-person-128.onnx` is the checked-in Vision
  V1.1 baseline and remains the default model path in deployment examples.
- `tinyissimo-person-128-longpet-v1.onnx` is the Vision V1.2 candidate produced
  from the complete COCO person checkpoint plus LongPet domain fine-tune.
- The V1.2 candidate is not currently stored in `models/` and has not replaced
  the V1.1 runtime baseline. It must remain “candidate, pending user board
  acceptance” until its exact artifact is reviewed and accepted on the target.

## Reproduction outline

All paths below point to the recommended external workspace. This is an example
layout, not a program-required location:

```powershell
$VisionWork = 'D:\ai-work\longpet-vision'

python scripts/vision/setup_tinyissimo_upstream.py `
  --checkout "$VisionWork\upstream\TinyissimoYOLO"

python scripts/vision/prepare_coco_person_dataset.py `
  --coco-root "$VisionWork\datasets\coco2017" `
  --output "$VisionWork\datasets\coco-person-v1.1-subset" `
  --train-positive 2000 --train-negative 500 `
  --val-positive 500 --val-negative 125

python scripts/vision/train_export_tinyissimo.py `
  --upstream "$VisionWork\upstream\TinyissimoYOLO" `
  --data "$VisionWork\datasets\coco-person-v1.1-subset\coco-person.yaml" `
  --output-dir "$VisionWork\experiments\v1.1-coco-subset" `
  --model-output "$VisionWork\models\tinyissimo-yolo-v1-small-person-128.onnx" `
  --imgsz 128 --epochs 100 --batch 64 --device cpu

python scripts/vision/verify_tinyissimo_onnx.py `
  --upstream "$VisionWork\upstream\TinyissimoYOLO" `
  --checkpoint "$VisionWork\experiments\v1.1-coco-subset\runs\tinyissimo-v1-small-person-128\weights\best.pt" `
  --onnx "$VisionWork\models\tinyissimo-yolo-v1-small-person-128.onnx" `
  --images "$VisionWork\datasets\coco-person-v1.1-subset\images\val" `
  --data "$VisionWork\datasets\coco-person-v1.1-subset\coco-person.yaml"
```

The dataset manifest records annotation SHA-256 values, sampling limits, seed
and every selected COCO image ID. The export script rejects dynamic or
misaligned input shapes, runs `onnx.checker`, loads the result in ONNX Runtime,
and embeds LongPet provenance metadata. A successful exporter message alone is
not treated as validation.
