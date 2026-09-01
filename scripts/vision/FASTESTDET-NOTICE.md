# FastestDet model notice

Vision V1 uses the upstream FastestDet COCO model only when the operator
prepares it explicitly. The model binary is not stored in the LongPet
repository and is never downloaded during CMake configure or build.

- Upstream project: <https://github.com/dog-qiuqiu/FastestDet>
- Upstream model: `example/onnx-runtime/FastestDet.onnx`
- Upstream Git blob: `d60f81fc354db500df1c3e665445fdcb51d78120`
- Pinned SHA-256: `31cb14c017fce347cb4c846ec62fdf00d76bb3beecdf4be21e7116ac67feb880`
- Project license: BSD 3-Clause, copyright 2022 xuehao.ma

The upstream repository contains the model and license together. Preserve the
BSD copyright, conditions, and disclaimer when redistributing the model or a
binary package containing it. Review the upstream repository again before a
public product release in case its model distribution terms change.

The official ONNX Runtime example defines the actual preprocessing and output
decode used by LongPet V1:

- input: float32 NCHW `[1, 3, 352, 352]`, BGR bytes divided by 255;
- output: one float32 NCHW feature map;
- channels: objectness, four box values, then softmax class scores;
- score: `objectness^0.6 * best_class_score^0.4`;
- box: `tanh` center offsets and `sigmoid` normalized width/height;
- COCO `person` class index: `0`.

LongPet validates these graph properties when loading the model. It refuses an
incompatible ONNX file instead of interpreting it as a YOLOv5/v8/v11 tensor.
