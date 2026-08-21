# LongPet V0.2 视觉模块优化、接入与上板报告

> 日期：2026-08-20  
> 工程：`D:\code_qt\longpet`  
> 分支：`codex/v02-reminder-kws`  
> 开发板：Loongson 2K0300，`192.168.137.45`，工作目录 `/root/mytest/qt`

## 1. 结论摘要

本轮完成了视觉模块的分层架构、轻量运行时、挥手交互、状态展示、测试、交叉构建和真实开发板部署。最终版本已经在开发板上运行，并与现有 KWS、Network、ALSA 音量、Backlight、Power、Reminder 等模块同时常驻。

本轮最重要的产品判断是：

- **挥手识别已作为真实功能接入。** 识别到挥手后，在没有 Emergency 或 ReminderAlert 抢占屏幕时回到 Home，并显示“看到你挥手啦，我在这里”。
- **跌倒检测没有伪装成可用功能。** 研究模型在板上过慢，纯运动几何方案在独立正常活动样本上误报明显，因此当前只保留 `fall_candidate` 实验能力，默认关闭，并且不会触发紧急页。
- 已预留正式 `fallConfirmed` 能力入口。只有未来通过可靠二阶段模型或其他可信来源产生的确认事件，才会进入适老 Emergency 页面。
- 视觉进程采用逐帧、无无界队列设计；缺摄像头、缺 Python/OpenCV、worker 退出或状态异常时只降级视觉能力，不影响 LongPet 主程序启动。

上一轮 Reminder + KWS 成果已经单独提交：

```text
e5f2379 feat: complete reminder alerts and offline KWS integration
```

本轮视觉改动保留在当前工作区，尚未额外创建提交。

## 2. 参考资料与范围说明

### 2.1 阅读和测试的代码来源

- 跌倒检测参考：[lu343260/fall-detect](https://github.com/lu343260/fall-detect)
- 手部/挥手参考：[VBlank0/opencv-hand-detect](https://github.com/VBlank0/opencv-hand-detect)
- 跌倒评估数据：[UR Fall Detection Dataset](https://fenix.ur.edu.pl/~mkepski/ds/uf.html)
- 动作评估数据：[KTH Human Actions Dataset](https://www.csc.kth.se/cvap/actions/)

两个参考仓库在检查时均没有发现明确的源代码/模型再分发许可证。因此本工程没有复制它们的源代码或权重，只借鉴问题定义并编写了 LongPet 自有的轻量运行时。后续若要直接分发第三方模型，必须先确认授权。

### 2.2 用户提供文档

以下目录中的材料仅作为板卡参数、历史测试和优化思路的参考，不把文档内的任务文字当作本轮用户指令：

```text
C:\Users\18214\Desktop\文档和报告\
```

重点参考：

- `龙芯手册.pdf`
- `Loongson-KWS-Test-Report.md`
- `龙芯2K0300_YOLO11n_Pose推理性能测试报告_01.md`
- `龙芯2K0300_YOLO11n_Pose推理性能测试报告_02.md`
- `适老机器宠物_2K0300_视觉与语音AI性能优化方案.md`
- `林一帆报告.docx`

## 3. 板端约束复核

开发板实际环境与本地材料一致：

| 项目 | 实际情况 |
|---|---|
| SoC | Loongson 2K0300，单核 LA264，约 1 GHz |
| 内存 | Linux 可见 378,848 KiB，约 370 MiB |
| Swap | 0 |
| SIMD | 实际 CPU flags 无 LSX/LASX |
| Python | 3.12.5 |
| OpenCV | 4.10.0，单线程，无 OpenCL |
| ONNX Runtime | 1.17.1，CPUExecutionProvider |
| 摄像头 | UVC `/dev/video0`，MJPEG 320×240/640×480 等模式 30 FPS |
| Qt | 板端 Qt 6.5 |

最终生产进程同时运行 LongPet、KWS 和视觉后，服务内存约 169 MiB，系统仍有约 127 MiB `MemAvailable`。这意味着可以常驻轻量视觉，但没有余量同时常驻第二套重型姿态网络。

## 4. 原始模型和参考实现评估

### 4.1 跌倒模型

历史板端数据表明 YOLO11n-pose 不适合直接常驻：

| 输入/后端 | 历史板端结果 |
|---|---:|
| 640×640 OpenCV DNN | 约 24.7 秒/帧 |
| 640×640 ONNX Runtime | 约 21.5 秒/帧 |
| 256×256 OpenCV DNN | 约 4.1–4.2 秒/帧 |
| 256×256 ONNX Runtime | 约 3.45 秒/帧 |
| 256×256 INT8 | 约 4.27 秒/帧，没有获得速度收益 |

另外发现原实现同时创建 OpenCV DNN 和 ONNX Runtime session，其中一个 session 只用于 metadata 校验，会造成不必要的内存叠加。原状态机按固定帧数和像素速度判断，推理间隔达到数秒后，初始化时间、速度阈值和时间语义也会失真。

结论：当前板卡上不能把该姿态模型作为连续实时检测器；即使只做候选后二阶段确认，也必须先解决模型授权、内存峰值和数秒延迟问题。

### 4.2 原挥手实现

原参考实现包含 palm、landmark 和 YOLO hand pose 等网络。主要问题是运动门控位置过晚：画面中只要持续有运动，就会连续执行重网络；摄像头噪声、人体走动都会导致高负载。同时“当前手框与运动框相交”并不能证明发生了挥手，容易将普通手部活动误判为挥手。

本轮改为“低成本前景组件 + 有限时间轨迹”：

1. 320×240 MOG2 前景分割；
2. 只保留画面上部、小面积、连续空间位置的候选；
3. 2.4 秒有界轨迹；
4. 验证横向幅度、累计路径、方向翻转次数、垂直路径比例、最大单步和持续时间；
5. 事件层再做 3.5 秒冷却。

## 5. 数据集实验与能力边界

### 5.1 跌倒候选实验

UR 视频为 640×240 的深度可视化 + RGB 拼接画面。初始实验错误地对整张拼接图缩放，后续已修正为只裁右侧 RGB；真实开发板摄像头本身提供普通 RGB，不需要此裁剪。

为了避免只在单一子集上得到好看的数字，实验采用分段开发/留出：

| 阶段 | 跌倒命中 | 正常活动误报 | 说明 |
|---|---:|---:|---|
| UR 01–10 调参集 | 10/10 | 0/10 | 明显存在覆盖不足/过拟合风险 |
| UR 11–20 独立留出，连续相机协议 | 8/10 | 3/10 | 坐床、低照蹲起、弯腰出现误报 |
| UR 21–30 第二留出，连续相机协议 | 7/10 | 0/10 | 漏检 21–23 |
| ADL 31–40 追加负样本 | — | 6/10 | 多种正常活动与跌倒几何不可分 |

“连续相机协议”会在数据集视频结束后重复最后一帧一小段时间，因为真实摄像头不会在人物落地时立即停止。原始短视频协议会使等待确认窗口来不及完成；即使使用更合理的连续协议，追加 ADL 的 6/10 误报仍然不能接受。

因此当前产品只允许：

```text
MOG2/轮廓状态机 -> fall_candidate（实验、默认关闭）
可靠二阶段确认/其他可信来源 -> fall_confirmed -> EmergencyPage
```

当前 worker 永远不会把自身的候选冒充为 `fall_confirmed`。

### 5.2 挥手实验

KTH person15 使用 1 个 handwaving 正样本和 boxing、handclapping、jogging、running、walking 共 5 个负样本进行采样率对比：

| 处理频率 | handwaving 事件数 | 负样本触发 |
|---:|---:|---:|
| 5 FPS | 2 | 0/5 |
| 6 FPS | 3 | 1/5（boxing） |
| 8 FPS | 2 | 0/5 |

该结果只说明当前轨迹规则在这组小样本上的行为，不是准确率声明。5 FPS 与 8 FPS 行为一致且资源更低，因此板端默认采用 5 FPS；仍可使用 `LONGPET_VISION_FPS=8` 做现场实验。

## 6. 最终架构

```text
Application（唯一组合根、负责创建/启动/销毁）
    |
    +-- VisionAdapter（platform）
    |      |
    |      +-- QProcess -> vision_worker.py -> OpenCV /dev/video0
    |      +-- 解析 JSON status / detection，失败时降级
    |
    +-- VisionService（service）
    |      +-- 数据校验、时间戳补全、按事件类型冷却
    |      +-- wave / fallCandidate / fallConfirmed 信号
    |
    +-- AppController
    |      +-- wave -> Home + Toast
    |      +-- fallConfirmed -> EmergencyPage
    |      +-- ReminderAlert / Emergency 抢占优先级保护
    |
    +-- SystemService -> SettingsPage“本地感知”状态
```

Page、Widget、MainWindow 均不直接依赖 OpenCV、摄像头或 Python 进程。视觉硬件/运行时细节停留在 platform 层，业务节流和语义停留在 Service 层，页面抢占停留在 AppController。

## 7. 运行时实现

### 7.1 默认参数

| 参数 | 默认值 |
|---|---:|
| 分辨率 | 320×240 |
| 处理频率 | 5 FPS |
| 视觉启动延迟 | 30 秒 |
| worker nice | 5 |
| 挥手 | 开启 |
| 跌倒候选 | 关闭 |
| 内存主动退出阈值 | `MemAvailable < 32 MiB` |
| 心跳 | 5 秒 |

视觉延迟 30 秒启动是有意设计：板端 KWS 模型冷启动约需 25 秒，错峰可以避免两个 Python/OpenCV/ONNX 运行时同时制造内存和 CPU 峰值。

### 7.2 恒定内存和摄像头优化

- worker 一次只保留当前帧、背景模型和短轨迹，不建立帧列表，不使用无界队列或额外线程。
- UVC 摄像头实际按 30 FPS 输出 MJPEG；未到目标处理时刻时只调用 `grab()` 前进缓冲，只有目标帧才 `read/retrieve` 并解码。
- 捕获格式先设置 MJPG，再设置宽高、FPS 和 buffer size。
- SIGTERM/SIGINT 会退出主循环；正常超时停止报告 `stopped`，不会误报为摄像头损坏。
- 每个 heartbeat 上报实际 FPS、检测处理时间、摄像头号和摘要。
- `/proc/meminfo` 可用时监控 `MemAvailable`，低于安全阈值主动退出。
- C++ 父进程为 Linux 子进程设置 nice 和 `PDEATHSIG`；LongPet 退出后 worker 不会成为孤儿。

### 7.3 可配置环境变量

```text
LONGPET_VISION_ENABLED
LONGPET_VISION_ROOT
LONGPET_VISION_WORKER
LONGPET_VISION_PYTHON
LONGPET_VISION_CAMERA
LONGPET_VISION_WIDTH
LONGPET_VISION_HEIGHT
LONGPET_VISION_FPS
LONGPET_VISION_WAVE_ENABLED
LONGPET_VISION_FALL_ENABLED
LONGPET_VISION_STARTUP_TIMEOUT_MS
LONGPET_VISION_NICE
```

Windows/普通 CI 默认不启动真实视觉进程；Linux 默认启用，但运行时、Python 或摄像头不可用时正常降级。

## 8. 产品交互

### 8.1 挥手

`wave` 事件经过 Service 冷却后由 AppController 处理：

- 普通页面/Companion：进入 Home，显示“看到你挥手啦，我在这里”；
- ReminderAlert 正在显示：不抢占提醒；
- Emergency 正在显示：不抢占紧急页；
- 3.5 秒内重复事件由 VisionService 抑制。

### 8.2 紧急页

EmergencyPage 新增动态 detail：

- KWS “救命”：`听到紧急求助，请确认是否需要联系家人`；
- 未来可信 `fallConfirmed`：`视觉监护检测到可能跌倒`；
- 重复紧急事件可更新当前 detail；
- 关闭紧急页后继续遵循现有 ReminderAlert 恢复逻辑。

注意：当前本地视觉 worker 不产生 `fallConfirmed`，所以不会因实验候选误触发 Emergency。

### 8.3 设置页

“语音关键词”行重构为“本地感知”，显示两行摘要：

```text
语音：监听中/已就绪/未启动
视觉：5.0 FPS/监护中/已就绪/未启动
```

详细 KWS、最近关键词和视觉摘要保留在 tooltip 中，SystemService 仍是 UI 的唯一系统状态来源。

## 9. 修改文件

### 9.1 新增

- `src/model/VisionModels.h`
- `src/platform/VisionAdapter.h`
- `src/platform/VisionAdapter.cpp`
- `src/services/VisionService.h`
- `src/services/VisionService.cpp`
- `third_party/longpet-vision/src/motion_detector.py`
- `third_party/longpet-vision/src/vision_worker.py`
- `third_party/longpet-vision/requirements.txt`
- `third_party/longpet-vision/README.md`
- `docs/LongPet-V0.2-Vision-Integration-Board-Report.md`

### 9.2 修改

- `CMakeLists.txt`
- `src/app/Application.h`
- `src/app/Application.cpp`
- `src/app/AppController.h`
- `src/app/AppController.cpp`
- `src/mainwindow.h`
- `src/mainwindow.cpp`
- `src/model/SettingsModels.h`
- `src/services/SystemService.h`
- `src/services/SystemService.cpp`
- `src/pages/SettingsPage.cpp`
- `src/pages/EmergencyPage.h`
- `src/pages/EmergencyPage.cpp`
- `tests/V02Test.cpp`

## 10. 构建与测试

### 10.1 Windows Release

环境：Qt 6.11.2 MSVC 2022（本地开发环境），Release，`/W4 /permissive-`。

结果：

```text
LongPet.exe              构建通过
LongPetV02Tests.exe      构建通过
CTest                    1/1 passed
QtTest                   21 passed, 0 failed, 1 skipped
```

跳过项是需要 `LONGPET_TEST_CAPTURE_DIR` 的页面抓图测试；现有 ReminderAlert 1024×600 渲染测试仍正常执行并通过。

新增测试覆盖：

- Adapter disabled 降级；
- worker detection/status JSON 解析；
- 非法置信度拒绝；
- VisionService 挥手冷却；
- `fallCandidate` 不等同于 `fallConfirmed`；
- 只有确认事件进入 Emergency；
- 紧急页恢复原页面；
- 挥手进入 Home；
- Settings 同时显示语音与视觉状态。

### 10.2 Python 运行时

```text
py_compile              通过
恒定内存 synthetic      fall_candidate 与 wave 均产生，passed=true
```

### 10.3 LoongArch Release

使用现有 WSL Ubuntu-24.04 脚本：

```text
/mnt/d/code_qt/longpet/scripts/build-loongarch.sh
```

结果：

```text
ELF 64-bit LSB executable, LoongArch
interpreter /lib64/ld-linux-loongarch-lp64d.so.1
Release 链接成功
```

## 11. 板端实测

所有新增基准在最后阶段均采用逐帧生成/读取，临时实验使用 96 MiB cgroup 上限和超时保护。

### 11.1 性能演进

| 测试 | 结果 | 说明 |
|---|---:|---|
| 空白帧微基准 320×240 | 35.77 ms/帧 | 板端真实结果，峰值 RSS 约 54 MiB |
| 初始摄像头 8 FPS | 73–79 ms/处理帧 | 每个 30 FPS 输入帧都做 MJPEG 解码 |
| `grab()` 优化后 8 FPS | 35.78–43.84 ms/处理帧 | 稳定约 8 FPS |
| 最终摄像头 5 FPS | 36.78–44.18 ms/处理帧 | 稳定 4.95–5.05 FPS |
| 5 FPS 临时单元 CPU | 7.974 s / 30 s | 约 26.6% 单核 |
| 5 FPS 临时单元内存峰值 | 44.6 MiB | cgroup 统计 |
| 合成大幅运动帧 | 约 394–400 ms/帧 | 复杂前景最坏情况明显更慢 |

“2.5 ms/帧”是 Windows 主机实验值，不是开发板结果，不能用于板端性能结论。

### 11.2 最终生产稳态

最终版本运行约 3 分钟后的快照：

| 项目 | 结果 |
|---|---:|
| `longpet.service` | active/running |
| `NRestarts` | 0 |
| LongPet RSS | 54,560 KiB |
| KWS 主进程 RSS | 79,504 KiB |
| KWS 解码子进程 RSS | 32,800 KiB |
| 视觉 worker RSS | 54,080 KiB |
| 视觉 worker 平均 CPU | 21.4% |
| service MemoryCurrent | 177,487,872 bytes |
| service MemoryPeak | 178,552,832 bytes |
| 系统 MemAvailable | 129,856 KiB |
| 摄像头占用 | `/dev/video0` -> vision worker |

当前生产命令确认包含：

```text
vision_worker.py --camera 0 --width 320 --height 240 --fps 5 --wave-enabled
```

板端部署路径：

```text
/root/mytest/qt/LongPet
/root/mytest/qt/vision/
```

最终二进制 SHA-256：

```text
cfd67905d59f67ba1af3dd4b2dddbc1beae6ecc2d9ee4aca22b068a271f62a47
```

可回滚备份：

```text
/root/mytest/qt/LongPet.pre-vision-20260820-2133
/root/mytest/qt/LongPet.vision-8fps
/root/mytest/qt/vision/src/vision_worker.py.pre-grab
```

## 12. OOM 事故说明与整改

早期临时 synthetic 脚本曾创建约 4,000 张 320×240×3 空白帧列表，仅像素数据就约 921.6 MiB；开发板只有约 370 MiB 可见内存且无 swap，因此触发 OOM 并导致 SSH 中断。串口信息确认了该原因。

这是测试脚本设计错误，和 LongPet 正式视觉实现无关，但必须记录。整改如下：

- 删除预生成帧列表，改为单帧复用或 generator；
- 板端实验统一使用 96 MiB 临时 cgroup；
- timeout 同时设置强制 kill 后备；
- worker 自测循环支持停止信号；
- 正式运行时不保留历史帧，只保留短轨迹坐标；
- 增加 `MemAvailable` 安全阈值；
- 报告严格区分 Windows 与板端数据。

SSH 恢复后复核 LongPet 一直为 active，`NRestarts=0`；最终部署和后续实验未再出现 OOM。

## 13. 已真实实现与预留能力

### 13.1 已真实实现

- OpenCV + UVC 摄像头真实采集；
- 恒定内存的 320×240 运动/轨迹处理；
- 挥手事件 JSON 协议；
- Adapter 生命周期、解析和降级；
- Service 校验与冷却；
- AppController 页面优先级；
- 挥手回 Home + Toast；
- Settings 视觉状态/FPS；
- KWS 与视觉错峰启动；
- Windows 构建/CTest；
- LoongArch Release 交叉构建；
- 板端长期常驻的初步稳定性验证。

### 13.2 仅预留或实验

- `fall_candidate`：存在代码，但默认关闭，只能用于后续数据采集/二阶段触发；
- `fallConfirmed`：C++ Service/Controller 接口已存在，当前 worker 不产生；
- 深度相机、姿态模型、场景床区、多人跟踪：未实现；
- 摄像头视频上传/存储：未实现，也不应在未确认隐私策略前实现；
- Family App 视觉告警传输：未实现；
- 视觉 worker 自动重试：当前退出后降级，随 LongPet 下次启动恢复。

## 14. 仍需人工验证

以下项目需要真实人在摄像头前配合，无法通过 SSH 完全替代：

1. 在 0.8 m、1.5 m、2.5 m 距离分别挥手，检查 Home 跳转和 Toast。
2. 正面/侧面、左右手、坐姿/站姿、白天/夜间各采集至少 20 次正样本。
3. 对拍手、擦脸、伸懒腰、拿杯子、走过镜头等负样本各测试至少 20 次。
4. ReminderAlert 显示期间挥手，确认提醒页不被抢占。
5. Emergency 显示期间挥手，确认紧急页不被抢占。
6. 连续运行 24 小时，记录 CPU 温度、MemAvailable、KWS 漏检率、视觉进程和服务重启次数。
7. 确认摄像头安装角度不会长期包含电视画面、窗帘摆动或高频背光变化。
8. 断开/重新插入摄像头，确认当前版本进入降级；当前不会自动重试，需重启 LongPet。

## 15. 需要敲定的事项

### 15.1 跌倒功能验收路线

建议保持当前默认关闭。若要继续，应先确定：

- 是否允许固定房间/固定机位，并配置床、沙发等排除区域；
- 是否可以采集本机位、本家庭环境下的跌倒与 ADL 数据；
- 是否接受“候选后数秒确认”而不是实时确认；
- 产品验收要求，例如召回率、每小时误报次数、确认延迟；
- 第三方模型权重的商用/再分发许可。

在这些问题确定前，不建议打开 `LONGPET_VISION_FALL_ENABLED=1`。

### 15.2 挥手后的产品动作

当前动作是回到 Home 并显示 Toast。可以后续选择：

- 仅唤醒/回 Home（当前实现，最稳妥）；
- 同时播放“我在呢”提示音；
- 进入对话并启动一次语音监听；
- 只在 Companion 页面响应，不影响其他普通页面。

### 15.3 隐私策略

当前完全本地处理，不保存、不上传图像。若未来要保存跌倒截图或发送家属端，需要先明确用户授权、保留时长、加密、删除策略和家属端权限。

### 15.4 系统级内存保护

临时实验已经使用 96 MiB cgroup，但正式 `longpet.service` 尚未设置 `MemoryMax`。建议先完成 24 小时稳定性测试，再决定是否给整个服务设置约 280–300 MiB 上限；过早限制可能把 KWS 冷启动峰值误杀。

## 16. 建议后续顺序

1. 完成现场挥手正/负样本测试和 24 小时共存测试；
2. 根据现场数据微调轨迹阈值，而不是继续使用单个公开视频过拟合；
3. 给 VisionAdapter 增加摄像头热插拔后的有限次数退避重试；
4. 明确跌倒验收指标、场景和授权；
5. 将轻量运动候选作为稀疏触发，评估单一、延迟加载的二阶段模型；
6. 二阶段达到验收线后，才允许输出 `fallConfirmed` 并接入家属告警。

