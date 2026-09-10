# KWS V3.1 → vision-v1.1 迁移与审计报告

日期：2026-09-11。范围：将 `kws` 最后一个节点的语音成果迁入现有 Vision 架构；不重做视觉算法、不合入训练产物、不部署开发板。

## 1. 仓库与交付状态

| 项目 | 本轮实际值 |
| --- | --- |
| 工作目录 | `D:\code_qt\longpet_main\longpet` |
| 当前分支 | `vision-v1.1`，未切换分支 |
| 迁移前及当前 HEAD | `869b872664e7dee98ec4598570345d89e76f50e1` |
| 来源 | [GitHub kws 分支](https://github.com/Justin-fanfan/longpet/tree/kws) |
| 锁定来源节点 | `208e1f7fdf09db6203dd78c1f7dcb9e2ceca5962`，`kws v3.1` |
| 两分支共同基线 | `42ca6a9a5f73b8d32bf4c148177e2556d64f8af8` |
| Git 提交 / 推送 / 远端合并 | 均未执行，等待确认 |

当前 `origin` 指向本地交付文件 `D:\LongPet-Vision-Final-Handoff\01_repository\longpet.git.bundle`，并不是 GitHub。
因此没有改写 origin，也没有直接 `pull`；使用明确 GitHub URL 抓取到本地 `github/kws` 引用后，按共同基线逐块迁移。
这是工作区集成，不是保留原提交拓扑的 merge/cherry-pick。源节点及来源在本文记录。

开始时发现用户已有未跟踪文件 `scripts/vision/run_realtime_camera.py`，未修改、未覆盖、未暂存。
迁移前后 SHA-256 均为 `79d989c71a1d6621e3da3ce21bc32e477b6e627fd299fa81d33037fe130cc2c2`。
旧 Codex worktree 的 Git 指针已失效，本轮没有修复或使用它进行开发。

## 2. 按当前架构保留了什么

不是把 `kws` 分支整棵目录覆盖到当前目录：

- 保留 `components/longpet-kws` 第一方组件布局、独立入口和已有来源/许可说明；不重建旧的 third_party KWS 目录。
- 保留 `VisionService`、`CameraCaptureAdapter`、Tinyissimo/FastestDet Adapter、工厂及后处理。
- 保留 `Application` 中同一 `CameraCaptureAdapter` 注入 Vision 与视频通话的组合关系。
- 保留 `LONGPET_ENABLE_VISION`、OpenCV/ONNX Runtime 构建接线、`LongPetVisionBench` 和原视觉测试入口。
- 保留 Vision V1.1/V1.2/V1.3 文档、训练脚本、正式 baseline 模型及当前候选模型验收约定。
- 保留当前部署说明里的组件路径、Vision 配置及地址示例，不用旧语音分支文档覆盖它们。
- 不迁入来源提交里的 `deploy/kws/__pycache__/longpet_kws_bridge.cpython-312.pyc`；沿用当前缓存忽略规则。

声学模型、tokens 和视觉模型均没有变更。

## 3. 已迁入能力及信号流

完整保留来源节点的以下能力：

1. 单个 FSMN-CTC 模型的 11 个词条，包含正式的唤醒、离线快捷词以及忽略的兼容词“你好”。
2. Online 唤醒进入原 V2 AI；Offline 走本地陪伴、时间、提醒、音量、首页和现有家属通话入口。
3. KWS 暂停启动、command_id 确认、异常重启、采集进程/队列回收；音频释放超时不会直接放行。
4. AI / 离线陪伴 / 通话共用音频协调器；取消等待实际音频取消确认，旧 session 不能继续播报。
5. 工具白名单、参数与日期校验、可取消的工具批次、同 ID 重复调用保护；已提交数据库操作不会伪装成被撤销。
6. 三套独立 Provider 配置、LAN 可用性开关、VAD/SSE/分句 TTS/历史和性能日志继续复用。
7. AppController 负责本地导航、离线播放页面和紧急页面；页面不直接调用网络、数据库或设备 API。

```text
Application（创建及连接对象）
  KwsProcessAdapter ↔ deploy/kws/longpet_kws_bridge.py
                         ↔ components/longpet-kws（同一个声学模型）
       ↓ keywordDetected
  VoiceCommandDispatcher ← VoiceCapabilityService
       ├─ 在线 → VoiceInteractionService → ASR / SSE LLM / TTS Provider
       │                                  └→ VoiceToolRegistry → ReminderService
       ├─ 离线播放 → LocalCompanionService → VoiceAudioPort
       └─ 语义信号 → AppController → UI

  AI / 离线播放 / 通话
       ↓ tryAcquire（预留，不等于已释放麦克风）
  MediaSessionCoordinator → KWS pause → 确认 → mediaReady → 开音频
       ↑ 音频取消/结束确认 → release → 冷却 → 恢复 KWS

  通话准备前 → callActivityChanged(true) → VisionService 暂停推理
       └─ VideoCallMediaAdapter 与 Vision 继续共享 CameraCaptureAdapter
  通话释放后 → callActivityChanged(false) → 恢复 Vision 的有效暂停状态
```

视觉的摄像头引用与音频 ownership 是不同资源：不能把 KWS/AI 音频锁替代成摄像头锁，
也不能因暂停 KWS 而重启第二套摄像头采集。Vision 仍可能与待机 KWS 同时运行；本轮不声称改善了二者争用 CPU 的性能。

## 4. 审计发现及处理

本次审计集中在来源差异和 KWS/AI/通话/Vision 的交界，不是整仓库安全审计。

| 发现 | 影响 | 处理 |
| --- | --- | --- |
| 来源测试仍指向旧 KWS 目录 | 当前第一方布局下 Python 词表测试失败 | 测试改用 `components/longpet-kws`；历史报告加迁移说明并更新可执行的仓库路径 |
| 来源含 `.pyc` 缓存 | 误带入机器和 Python 版本相关产物 | 排除，不迁入 |
| 通话活跃通知晚于 `media.prepare()` | 共享摄像头开始预热/供帧时，Vision 还没暂停，低性能板可能继续提交推理 | 通知提前到媒体准备前；失败、取消、挂断均平衡释放通知；仍尊重手动视觉暂停 |
| KWS ACK 仅核对默认可为 0 的 ID，未核对命令种类及 ready/stopping 状态 | 缺少 ID 的提前 paused 可能被当作释放确认；同 ID 错误类型可能扰乱握手 | 新增当前命令种类，要求 ready、非 stopping、正 ID、ID 与种类同时匹配；接受后清除等待类型，重复 ACK 不再放行 |
| 来源报告与组件 README 容易混淆“旧四词独立入口”和“产品 11 词 bridge” | 部署者运行错入口，或把历史测试当成本轮结果 | 组件 README 区分入口；历史报告显著标注日期、来源和新报告链接 |

源分支已有的音频释放、取消隔离、工具执行校验等修复已保留；没有为了迁移撤回它们。
新增 ACK 回归会注入“ready 前无 ID 的 paused”“同 ID 的错误类型 ACK”和“过期 ACK”，并连续执行 20 次快速 resume/pause。

Vision 新增交界回归覆盖：

- 10 次交替语音/视频通话；音频与提示音等待 KWS 确认；
- 预热前 Vision 已暂停，通话结束后只释放通话的摄像头引用；
- 预热失败、KWS 超时均恢复 Vision，迟到 ACK 不重启音频；
- 手动暂停不被挂断动作错误解除；恢复后可继续推理；
- 整组测试摄像头仅启动一次，最后一个消费者退出时才停止。

## 5. 新增/修改文件

| 层/用途 | 文件 |
| --- | --- |
| 组合根与页面流程 | `src/app/Application.cpp`、`AppController.{h,cpp}` |
| 配置及状态模型 | `src/data/AiConfigRepository.cpp`、`src/model/AiModels.{h,cpp}`、`SystemModels.h` |
| KWS 进程及协议 | `src/platform/KwsProcessAdapter.{h,cpp}`、`src/services/KwsPorts.h`、`deploy/kws/longpet_kws_bridge.py` |
| 音频协调及语音业务 | `MediaSessionCoordinator.{h,cpp}`、`VoiceInteractionService.{h,cpp}`、`LocalCompanionService.{h,cpp}`、`VoiceCommandDispatcher.{h,cpp}` |
| 通话交界 | `src/services/VideoCallService.{h,cpp}`；未替换当前共享摄像头的 `VideoCallMediaAdapter` |
| 网络及本地工具 | `NetworkStatusAdapter.{h,cpp}`、`SystemService.{h,cpp}`、`VoiceCapabilityService.cpp`、`VoiceToolRegistry.cpp` |
| Provider / 离线资源 | `OpenAiCompatibleProviders.{h,cpp}`、`OfflineAudioLibraryAdapter.cpp` |
| 构建及测试 | `CMakeLists.txt`、`tests/V02Test.cpp`、`tests/VisionV1Test.cpp`；新增 `tests/fake_kws_bridge.py`、`tests/test_kws_bridge.py` |
| 配置与文档 | 两份 `deploy/longpet-ai*.ini.example`、`deploy/配置说明.md`、组件 README；新增本报告及来源的历史接入报告，更新旧 V3 报告入口 |

以上未写完整路径的 C++ 服务文件均位于原有 `src/services/`；未新增旁路业务层。

## 6. 本轮验证

Windows 使用 Qt 6.11.0 / MinGW 13.1 / CMake / Ninja，新建忽略的 `build-kws-vision-release`。
测试构建显式使用 `LONGPET_ENABLE_VISION=OFF`：仍编译摄像头共享、VisionService、后处理和测试替身，
但不编译/链接真实 OpenCV/ONNX 推理分支。没有改变源码里的 Vision 默认开关或板端配置。

| 项目 | 本轮结果 |
| --- | --- |
| Release 配置 | 通过 |
| Windows Release 编译 | 首次及最终增量构建均通过 |
| CTest 三入口：V02 / KwsBridge / VisionV1 | 3 / 3 通过，共 32.82 秒；分别为 29.88 / 1.33 / 1.58 秒 |
| Python bridge | 7 项通过（本轮实际运行） |
| 路径检查 / `git diff --check` | 通过；活动代码、测试、部署说明无旧 KWS 目录引用 |
| 用户未跟踪脚本 | SHA-256 与开始时一致 |
| 真 ONNX 推理 / 声学唤醒 / 云 API / Linux ALSA | 本轮未执行；不借用 2026-09-07 的结果冒充本轮结果 |
| 交叉编译 / 上板 / 提交推送 | 未执行 |

复现（在仓库根 PowerShell 运行，Python 路径换成本机可导入 numpy 的解释器）：

```powershell
$env:PATH = 'D:\Qt\Tools\mingw1310_64\bin;D:\Qt\6.11.0\mingw_64\bin;D:\Qt\Tools\Ninja;' + $env:PATH
& D:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build-kws-vision-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DLONGPET_BUILD_TESTS=ON -DLONGPET_ENABLE_VISION=OFF `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.11.0/mingw_64 `
  -DPython3_EXECUTABLE="C:/path/to/python.exe"
& D:\Qt\Tools\CMake_64\bin\cmake.exe --build build-kws-vision-release --parallel 3
& D:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-kws-vision-release --output-on-failure
```

可选 Vulkan headers 缺失、Windows 未使用 Linux 音频辅助函数等提示不是本次新增功能依赖。
构建还保留 `AiChatMessage` 聚合成员默认初始化的编译警告；本轮未做无关的警告清理。
没有为本轮迁移新增重型运行时，未安装板端模块。

## 7. 配置与上板快速验收

完整操作以 `deploy/配置说明.md` 为准，下面是增量，不可覆盖已有三套 Provider 的 URL/Key。

```ini
[voice]
require_internet=true
availability_retry_ms=30000

[kws]
command_threshold=0.05
pause_timeout_ms=5000
resume_cooldown_ms=1200
```

只有 ASR/LLM/TTS 都在 LAN 时才考虑 `require_internet=false`。这允许本地网络可达，不代表自动探测或自动切换 Provider。
新环境变量为 `LONGPET_VOICE_REQUIRE_INTERNET` 和 `LONGPET_KWS_COMMAND_THRESHOLD`。

更新清单：

1. 使用当前 Vision 工程的正式 LoongArch 构建配置生成 `LongPet`；本轮 Windows `.exe` 不能上板。若要视觉推理，保留原 `LONGPET_ENABLE_VISION=ON` 及 SDK/OpenCV/ONNX 配置。
2. 主程序安装到 `/home/longpet/LongPet`，**同时更新** bridge 到 `/home/longpet/longpet-kws/longpet_kws_bridge.py`，不能只换程序。
3. 组件来源为 `components/longpet-kws`，板端仍可使用 `/home/longpet/longpet-kws/upstream`，不必改现有绝对路径；模型未改，同版无需重传。
4. 保留 `/etc/longpet/ai.ini` 的密钥和验证过的设备名；保留已有 Vision 配置。
5. 离线陪伴需要 `/home/longpet/offline-audio/` 下至少两条已授权音频，仓库没有新增正式陪伴录音。

人工备份并停止服务、成对替换后，再重启 `longpet.service`。本轮没有执行这些操作。
建议快测：

1. 正常唤醒 → 一轮 AI 回答 → KWS 恢复；重复 5～10 次。
2. Listening/Thinking/Speaking 分别点停止并立即重来，观察无旧回答、无持续 device busy。
3. 启用原 Vision 配置，家属来电/板端去电各测语音和视频，确认视觉在预热前暂停、通话结束后恢复；检查共享摄像头不重复打开。
4. 提示音过程中取消、通话失败后再唤醒，检查 KWS 能恢复。
5. 安全的本地条件下测 Offline 快捷词及本地播放；不要断开唯一管理链路。
6. 创建/查询/删除一条明确的测试提醒，勿用真实用药提醒做删除测试。

```bash
journalctl -u longpet.service -f -o cat
```

重点日志：`KWS ready/paused/resumed`、`Audio ownership ready`、`Voice metrics`、
`Vision frame=`、`Camera acquired/released`。分享日志时去除 Key 和其他隐私配置。

## 8. 尚未验证及保留限制

- 本次为迁移及交界审计，不保证整仓库不存在其他问题。
- KWS 声学准确率、远场/噪声误触、板端长稳及单核 KWS+Vision CPU 争用仍需实测。
- 仍为半双工：AI 录音/TTS、离线播放和通话时暂停 KWS，不保证此时能听见“停止/救命”；屏幕取消仍是可靠入口。
- 工具决策轮先缓冲文字再决定 TTS；并非每轮都能在 LLM 完成前出声。提醒删除是真实业务操作，不是演示。
- Vision 暂停停止提交后续推理，不强行中断已经在执行的一次模型推理；共享摄像头按原消费者引用生命周期运行。
- 本轮未处理旧媒体 Adapter 中已有的短时 `waitForStarted/waitForFinished`，不能把本报告理解为所有路径绝对无阻塞的性能验收。
- KWS 模型来源/再分发许可证的既有待确认项仍保留在组件 README；未新增模型或改变授权结论。

用户要求直接收尾报告后，没有继续扩大功能范围。所有改动留在当前分支工作区，等待用户检查、上板验收与提交确认。
