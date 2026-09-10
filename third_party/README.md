# Third-party integration boundaries

本目录只保存 LongPet 不直接维护的第三方依赖边界：固定上游版本、必要补丁、训练依赖、许可证或
provenance 说明，以及经过明确选择的小型正式 runtime 模型。

- `tinyissimo-yolo/`：固定 TinyissimoYOLO 上游提交、LongPet 最小补丁、可复现训练依赖和
  V1.1 person-only FP32 ONNX baseline。

不要把完整 TinyissimoYOLO/Ultralytics fork、数据集、训练 `runs`、checkpoint、teacher 模型或
本地 Python 环境提交到这里。LongPet 自维护的 Python KWS 位于 `components/longpet-kws/`。
