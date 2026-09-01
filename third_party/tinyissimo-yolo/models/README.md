# LongPet-trained TinyissimoYOLO model

`tinyissimo-yolo-v1-small-person-128.onnx` is the Vision V1.1 static FP32
runtime artifact trained by the reproducible scripts in this repository.

- architecture: TinyissimoYOLO v1-small
- upstream commit: `19bea4bd1ea1e2c29a6ee6b14bd7494ce8c6ba25`
- classes: `0 = person`
- input/output: `[1,3,128,128]` -> `[1,5,16]`
- training: COCO 2017 subset, 2,000 positive + 500 negative train images,
  500 positive + 125 negative validation images, 100 epochs from scratch
- model bytes: `1,620,598`
- training parameter count: `402,057` (fused inference graph: `401,297`)
- SHA-256: `f647b4704a1856195a3c362c8134f0633c17db86ecfeb7de5d1fe31ec7f4c149`
- validation PyTorch: P `0.45824`, R `0.16553`, mAP50 `0.18497`,
  mAP50-95 `0.07927`
- validation ONNX Runtime: P `0.44609`, R `0.17604`, mAP50 `0.19205`,
  mAP50-95 `0.08114`

The model is adequate for the V1.1 latency experiment and detects clear,
nearby people, but the low recall means it is not a production safety model.
See `docs/LongPet-Vision-V1.1-TinyissimoYOLO-Report.md` before enabling it.

The pinned upstream source and exported model metadata identify the code as
AGPL-3.0. Complete a license review before distributing this trained derivative
beyond the project/team.
