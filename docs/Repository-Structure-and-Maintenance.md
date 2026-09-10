# LongPet 仓库结构与维护说明

本文是仓库目录职责、第一方/第三方边界、训练工作区和维护检查的当前说明。版本实验数字仍以各
Vision/KWS 报告为准；历史报告中的机器路径和旧 IP 是当时事实，不是当前程序要求。

## 1. 主要目录树

```text
longpet/
├─ src/                         # LongPet 正式 C++ runtime
│  ├─ app/                     # 组合根与业务流程
│  ├─ model/                   # 跨层数据模型
│  ├─ data/                    # SQLite、Repository 与配置读取
│  ├─ services/                # 业务服务和 Port
│  ├─ platform/                # OS、硬件、网络、AI、Vision Adapter
│  ├─ pages/                   # Qt 页面
│  └─ widgets/                 # 可复用 UI 组件
├─ components/
│  └─ longpet-kws/             # 第一方 Python KWS 组件与必要模型
├─ resources/                  # QRC、样式、图标和声音
├─ scripts/
│  ├─ vision/                  # 数据、训练、验证和模型准备脚本
│  ├─ build-loongarch.sh       # 交叉构建
│  ├─ run-board.sh             # 板端启动
│  └─ run-vision-benchmark.sh  # 板端 Vision benchmark 辅助
├─ third_party/
│  └─ tinyissimo-yolo/         # 固定上游、patch、依赖、provenance、V1.1 模型
├─ deploy/                     # systemd、环境变量和配置样例
├─ docs/                       # 设计、实验、部署和维护文档
├─ tests/                      # C++ 自动化测试
├─ tools/                      # 随 C++ 工程构建的辅助 executable
├─ cmake/                      # CMake toolchain
├─ CMakeLists.txt
└─ README.md
```

`build*`、`.venv*`、IDE 状态、数据集和训练产物可能存在于本地工作树，但不是仓库结构的一部分，
必须由 `.gitignore` 排除。

## 2. 目录职责和边界

| 目录 | 应保存 | 不应保存 |
|---|---|---|
| `src/` | 正式 C++ 产品代码 | 数据集、训练代码、第三方 fork |
| `components/` | LongPet 直接维护且可相对独立运行的组件 | 仅固定上游信息的依赖 |
| `scripts/` | 可参数化的构建、训练、验证、benchmark、数据准备工具 | 本机用户名、固定数据盘、训练输出 |
| `third_party/` | 固定上游版本、必要 patch、依赖、许可证/provenance、小型正式模型 | 完整 fork、COCO、runs、venv、teacher/checkpoint |
| `deploy/` | service、环境变量、权限和配置样例 | 凭据、实际设备私有配置、模型缓存 |
| `docs/` | 当前说明和保留事实的历史报告 | 无说明的临时日志、预测图 |

## 3. 为什么 KWS 是 component

`components/longpet-kws` 有自己的 `run.py`、`src/longpet_kws` package、依赖和 runtime 资产，
但代码已被 LongPet 针对 2K300 采集、VAD、进程隔离和稳定性直接维护。它需要和 LongPet 一起
演进、测试和部署，所以属于第一方 component，而不是只读的第三方镜像。

组件的公开起点提交、FSMN 模型与词表哈希、以及当前缺失的上游许可证信息记录在
`components/longpet-kws/README.md`。板端安装目录仍使用
`/home/longpet/longpet-kws/upstream` 以兼容已有配置；仓库路径和板端路径不要求同名。

快速定位：

- 入口：`components/longpet-kws/run.py`；
- 状态机、模型加载和采集：`components/longpet-kws/src/longpet_kws/cli.py`；
- FBank/VAD：同 package 下的 `fbank.py`、`vad.py`；
- 模型与词表：`components/longpet-kws/assets/fsmn/`；
- Qt/Python JSON-lines bridge：`deploy/kws/longpet_kws_bridge.py`；
- 应用侧进程 Adapter：`src/platform/KwsProcessAdapter.cpp`。

在仓库根运行帮助：

```powershell
python components/longpet-kws/run.py --help
```

默认模型、词表由 `__file__` 推导，不依赖启动时的工作目录。

## 4. 为什么 Tinyissimo 保持 third_party

`third_party/tinyissimo-yolo` 不包含完整 TinyissimoYOLO/Ultralytics fork。它只保存：

- `UPSTREAM_COMMIT` 固定上游 revision；
- LongPet 必要的最小 patch；
- CPU/CUDA 训练依赖快照；
- 上游许可证风险和模型 provenance；
- 已选择的 V1.1 小型正式 FP32 ONNX baseline。

训练时由 `setup_tinyissimo_upstream.py` 在仓库外准备固定 checkout。LongPet C++ runtime 只读取
最终 ONNX，不依赖 Python、PyTorch 或 Ultralytics。V1.2 候选模型尚未进入该目录，因此不得把
它描述为当前默认模型。

## 5. `scripts/vision` 快速定位

| 脚本 | 用途 |
|---|---|
| `prepare_coco_person_dataset.py` | 从 COCO 2017 构造 person-only 数据集；支持下载或本地 COCO。 |
| `prepare_longpet_person_dataset.py` | 从参数指定的实拍视频/图片生成 domain 数据集和质量报告。 |
| `train_export_tinyissimo.py` | 训练或从 `--weights` fine-tune，并导出/检查静态 ONNX。 |
| `verify_tinyissimo_onnx.py` | PyTorch/ONNX parity 和指定 split 的 P/R/mAP。 |
| `evaluate_longpet_test.py` | LongPet test 按帧、视频和场景评估。 |
| `setup_tinyissimo_upstream.py` | 固定 checkout 并应用精确 patch。 |
| `prepare-fastestdet-model.sh` | 准备 V1 FastestDet 历史 baseline。 |
| `run-vision-benchmark.sh` | 板端 detector/camera benchmark 辅助。 |

所有训练/数据路径必须由参数传入。脚本职责和常用参数的简表还见
`scripts/vision/README.md`。

## 6. 仓库与 `ai-work` 的边界

需要区分三种路径：

1. **历史实验实际路径**：报告可保留当时的 `C:\ai-work\...`、`D:\ai-work\...` 或用户目录，
   但必须标注为历史记录；
2. **推荐示例路径**：新文档统一使用 `D:\ai-work\longpet-vision\`，方便协作；
3. **程序必须路径**：训练脚本没有固定 Windows 必须路径，全部由参数传入；板端 runtime 路径
   由 deploy 配置/环境变量决定。

仓库 runtime、CMake 和正式脚本不得依赖仓库外现存的 `C:\ai-work` 或 `D:\ai-work` 文件。
整理仓库时也不得移动或删除这些目录中的数据。

推荐结构：

```text
D:\ai-work\longpet-vision\
├─ upstream\
│  └─ TinyissimoYOLO\
├─ datasets\
│  ├─ coco2017\
│  ├─ coco-person-full\
│  └─ longpet-person-v1\
├─ checkpoints\
│  ├─ coco-person\
│  └─ longpet-person-v1\
├─ models\
│  ├─ tinyissimo-person-128-coco.onnx
│  └─ tinyissimo-person-128-longpet-v1.onnx
├─ experiments\
│  └─ domain-finetune-v1\
└─ teacher_models\
   └─ yolov8x.pt
```

## 7. Git 提交策略

应该提交：

- 第一方源码、测试和参数化脚本；
- 当前设计、部署、实验和维护文档；
- 小型且必要的正式部署模型；
- 固定上游 revision、必要 patch、训练依赖；
- 模型来源、SHA-256、许可证/provenance；
- 不含凭据的配置样例。

不能提交：

- COCO 或其他大型数据集；
- 实拍视频、抽帧图片与自动标签；
- `runs/`、checkpoint、teacher 大模型和缓存；
- Python venv；
- 临时 prediction、测试输出和大量 benchmark 临时结果；
- API Key、Token、实际生产配置。

模型进入 `third_party/.../models` 前，应同时具备用途、输入输出、训练数据范围、上游 revision、
SHA-256、许可证状态和验收状态说明。

## 8. 模型训练与板端 runtime

```text
外部 ai-work
  数据集 + 固定上游 + checkpoint
        ↓ scripts/vision 训练、导出、验证
  候选静态 FP32 ONNX
        ↓ 人工检查 provenance / SHA-256 / PC 指标 / 板端验收
仓库 third_party/.../models（必要的小型正式资产）
        ↓ deploy 配置选择
板端 /home/longpet/models + C++ ONNX Runtime
```

导出成功不等于可部署：候选必须经过 graph/parity、数据集指标和板端性能/功能验收。训练脚本不会
自动覆盖仓库 baseline，deploy 示例也继续指向已接受的 V1.1 模型。

## 9. Vision 版本与产品方向

- **V1 / FastestDet**：352×352 历史 baseline；板端完整链路约数秒一帧，已证明不适合正式实时
  detector；
- **V1.1 / Tinyissimo**：128×128、person-only、FP32 ONNX；无 KWS 约 600～700 ms，KWS 并发
  约 1.1 s，完成接入与性能探索；
- **V1.2 / domain fine-tune**：完整 COCO person checkpoint 加 LongPet 实拍 fine-tune，并在
  正确 test split 评估。候选 `tinyissimo-person-128-longpet-v1.onnx` 等待用户板端验收。

V1.2 PC test 指标为 P `1.0000`、R `0.9647`、mAP50 `0.9856`、mAP50-95 `0.8488`；正样本
逐帧检测率 `84.7% → 96.5%`。`2场景1人绿左.mp4` 的 3 帧仍未超过 score 0.25。test 没有
确认的无人样本，所以 negative false-positive rate 是 `N/A`，不能声称已经验证无人误报率。

当前方向是本地人物检测、人物在场与方向感知、低频 detector + lightweight tracker、后续人物
跟随与主动交互，再探索轻量手势识别。固定摄像头视角偏上，跌倒检测保留为历史探索事实，但不再
是当前 roadmap 主线。

## 10. 部署与历史记录

当前 2K300 示例地址是 `192.168.137.32`，当前命令以 `deploy/配置说明.md` 为准。历史报告中的
`10.x.x.x` 是当时 DHCP/实验地址，不应被复制为当前部署命令，也不应为“统一数字”而篡改原始
benchmark 事实。

Windows PowerShell、WSL/Linux 和板端 shell 命令必须分别放在标明语言的代码块中。包含破坏性
安装、覆盖、service stop/start 的步骤需要操作者人工确认，仓库维护检查本身不连接开发板。

## 11. 维护检查清单

目录或路径调整后至少执行：

```powershell
$LegacyKwsPath = 'third_party/' + 'longpet-kws'
git grep -n -F $LegacyKwsPath
python -m compileall scripts/vision components/longpet-kws
python components/longpet-kws/run.py --help
python scripts/vision/prepare_coco_person_dataset.py --help
python scripts/vision/prepare_longpet_person_dataset.py --help
python scripts/vision/train_export_tinyissimo.py --help
python scripts/vision/verify_tinyissimo_onnx.py --help
python scripts/vision/evaluate_longpet_test.py --help
python scripts/vision/setup_tinyissimo_upstream.py --help
ctest --test-dir build --output-on-failure
```

`--help` 检查不下载模型、不启动训练。若当前环境缺少可选 ML/audio 依赖，应记录为环境限制，
不要为一次仓库整理擅自下载大型包或模型。
