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
`requirements-training.txt`. Create an isolated Python 3.11 environment; do not
install it into the board image or LongPet runtime environment.

The upstream files identify the fork as AGPL-3.0, but the pinned repository has
no top-level `LICENSE` file. This directory contains no copied fork source or
upstream weights. `models/` contains only the LongPet-trained derivative and its
provenance note. Confirm the upstream licensing terms before distributing that
artifact beyond this project/team.

Input-size note: v1-small has stride 32. Upstream rounds a requested 112 image
size to 128. LongPet uses 128×128 as the correctness baseline and will not label
that graph as 112×112. A forced 112 graph has only a 3×3 final feature map and is
not accepted by the production adapter.

## Reproduction outline

All paths below should point outside the LongPet repository:

```powershell
python scripts/vision/setup_tinyissimo_upstream.py `
  --checkout C:\ai-work\TinyissimoYOLO

python scripts/vision/prepare_coco_person_dataset.py `
  --cache C:\ai-work\coco-cache `
  --output C:\ai-work\coco-person `
  --train-positive 2000 --train-negative 500 `
  --val-positive 500 --val-negative 125

python scripts/vision/train_export_tinyissimo.py `
  --upstream C:\ai-work\TinyissimoYOLO `
  --data C:\ai-work\coco-person\coco-person.yaml `
  --output-dir C:\ai-work\training `
  --model-output C:\ai-work\tinyissimo-yolo-v1-small-person-128.onnx `
  --imgsz 128 --epochs 100 --batch 64 --device cpu

python scripts/vision/verify_tinyissimo_onnx.py `
  --upstream C:\ai-work\TinyissimoYOLO `
  --checkpoint C:\ai-work\training\runs\tinyissimo-v1-small-person-128\weights\best.pt `
  --onnx C:\ai-work\tinyissimo-yolo-v1-small-person-128.onnx `
  --images C:\ai-work\coco-person\images\val `
  --data C:\ai-work\coco-person\coco-person.yaml
```

The dataset manifest records annotation SHA-256 values, sampling limits, seed
and every selected COCO image ID. The export script rejects dynamic or
misaligned input shapes, runs `onnx.checker`, loads the result in ONNX Runtime,
and embeds LongPet provenance metadata. A successful exporter message alone is
not treated as validation.
