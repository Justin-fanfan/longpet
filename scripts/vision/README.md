# Vision scripts

这些脚本连接“仓库内可复现逻辑”和“仓库外训练工作区”，不会让 C++ runtime 依赖数据集或
训练环境。

| 脚本 | 职责 |
|---|---|
| `prepare_coco_person_dataset.py` | 从 COCO 2017 构造 person-only 正/负样本数据集；可下载或通过 `--coco-root` 使用本地 COCO。 |
| `prepare_longpet_person_dataset.py` | 从参数指定的 LongPet 实拍视频/图片抽帧、teacher 辅助标注、去重、拆分并生成质量报告。 |
| `train_export_tinyissimo.py` | 从结构或 `--weights` checkpoint 训练/fine-tune Tinyissimo，保存 `best.pt` 并导出静态 ONNX。 |
| `verify_tinyissimo_onnx.py` | 比较 PyTorch/ONNX raw 输出，并在指定 split 上验证 P/R/mAP。 |
| `evaluate_longpet_test.py` | 对 LongPet test 集生成按帧、按视频和按场景统计及预测图。 |
| `setup_tinyissimo_upstream.py` | 准备固定上游 checkout，严格应用仓库内最小 patch。 |
| `prepare-fastestdet-model.sh` | 显式下载并校验 Vision V1 FastestDet 历史 baseline。 |
| `../run-vision-benchmark.sh` | 在已具备模型、摄像头和 benchmark binary 的板端运行通用基准。 |

推荐把所有传入路径放在 `D:\ai-work\longpet-vision\` 下；`C:\ai-work`、旧 `D:\ai-work` 路径只
出现在历史实验报告时代表当时事实，不是脚本默认值。脚本不会自动把候选模型复制到
`third_party/tinyissimo-yolo/models/`，也不会修改部署配置。
