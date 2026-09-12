# LongPet Vision V2.3 — Head-Body Person Following Report

日期：2026-09-12

状态：**Software implementation complete / MCU/UART/FOLLOW 基础实机测试已由用户确认通过 / 完整人物跟随硬件验收待完成**。

本报告同时记录软件实现、构建与用户提供的第一轮实机结果。Codex 没有 SSH、SCP、启动板端程序、下发运动命令或烧录 ESP；下文“实机已验证”均指用户报告，不是 Codex 独立复测。Vision V2.2 自动跟头此前已由用户确认通过，作为本轮基线。基础协议通过不等于自动头身/距离闭环整体通过。

## 1. 本轮结论

V2.3 在 V2.2 `TARGET` 自动跟头之上增加了保守的人物跟随闭环：

```text
TargetObservation
  -> TARGET（头部继续跟随人物）
  -> 等待同一目标稳定
  -> 读取 MCU STATUS 的物理 head_offset
  -> 头偏超过滞回并持续一段时间：只原地旋转机身
  -> 头部稳定回中：停止旋转
  -> bbox normalized height == FAR：只低速前进
  -> GOOD / NEAR：停车
```

头身对齐优先于距离跟随，同一时刻只会发旋转、前进或停车中的一个动作。V2.3 不包含自动后退、左右平移、弧线跟随、复杂轨迹规划或速度闭环标定。

原计划的“头身协调”和“距离人物跟随”在本轮合并，是因为距离前进必须以机身已经朝向人物为前置条件。两者若由两个独立控制器分别抢占底盘，会产生竞争状态和不一致的停车边界；统一状态机仍把 `ALIGNING` 与 `APPROACHING` 分成清晰阶段，并保持对齐优先。

## 2. 架构与职责

### LongPet

```text
Application
  -> VisionService
       -> TargetObservation
  -> AutomaticHeadTrackingService（兼容保留 V2.2 类名）
       -> HEAD_ONLY / PERSON_FOLLOW 高层策略
       -> 距离滞回、头身对齐、状态机、抢占规则
  -> MotionService
       -> MANUAL / HEAD_ONLY / FOLLOW 所有权
       -> 模式切换和命令合法性
  -> MotionPort
       -> EspSerialAdapter
       -> Motion Protocol V2.3 extension
```

`AutomaticHeadTrackingService` 保留原名以避免无关重构，但现在是统一自动视觉运动策略服务。UI、FamilyLink Controller 和 Adapter 都不直接接触 UART 或电机。

V2.2 的 `TargetObservation -> TARGET -> Servo` 路径、方向修正和 HEAD_ONLY 行为均直接复用；V2.3 没有重新实现头部控制，只在选择 PERSON_FOLLOW 时增加底盘决策。HEAD_ONLY 回归仍由原接口和自动测试覆盖。

### ESP32-S3 Motion MCU

MCU 只负责：

- 执行 `TARGET` 对头部的有限修正；
- 在 `FOLLOW` 模式执行 `FOLLOW_MOVE FORWARD/ROTATE_LEFT/ROTATE_RIGHT/STOP`；
- 独立 500 ms FOLLOW lease；
- 模式切换、目标过期、链路超时、fault 的低层停车；
- 在 STATUS 中报告物理 `head_offset`。

MCU 不读取 bbox、不判断 FAR/GOOD/NEAR、不决定何时对齐机身，因此感知策略没有下沉到固件。

### Family Desktop

AI 视野页通过 FamilyLink REST 选择互斥模式：

- `DISABLED`：关闭；
- `HEAD_ONLY`：V2.2 仅头部；
- `PERSON_FOLLOW`：V2.3 头身与距离跟随。

家属端只表达模式意图和显示遥测，不发送自动底盘方向。

## 3. V2.3 状态机

| 状态 | 含义 | 底盘行为 |
|---|---|---|
| `DISABLED` | 人物跟随未启用 | STOP |
| `ACQUIRING` | 等待同一人物目标持续稳定 | STOP |
| `ALIGNING` | 头偏进入阈值消抖、正在旋转或回中驻留 | ROTATE 或 STOP |
| `APPROACHING` | 头部已对齐且距离为 FAR | FORWARD |
| `HOLDING` | 距离 GOOD/NEAR，或等待新鲜 Motion STATUS | STOP |
| `LOST` | target lost/stale/expiry | STOP + `TARGET 0 0 0` |

重要转换：

- 新目标先进入 `ACQUIRING`，默认稳定 600 ms 后才可能动轮；
- 物理头偏超过 220 us 并持续 400 ms，进入 `ALIGNING`；
- 物理头偏回到 100 us 内并持续 400 ms，才允许进入距离判断；
- 回中驻留期间先停车，不为了满足最小动作时长继续旋转；
- 目标丢失后进入 `LOST`，重新出现时重新走稳定确认，旧 DRIVE 不恢复。

## 4. 头身对齐

### 为什么不用图像 dx 直接驱动底盘

图像 dx 表示人物相对当前相机视野的位置。头部已向左追到人物时，人物可能已经回到画面中心，但机身仍未朝向人物。V2.3 因此使用 MCU 的实际头部位置：

```text
head_offset < 0 -> 头部在机身物理左侧 -> 底盘原地左转
head_offset > 0 -> 头部在机身物理右侧 -> 底盘原地右转
head_offset ~= 0 -> 机身大致对齐
```

物理语义由 MCU 的 `head_direction.h` 统一计算；LongPet 不解释 pulse 增减方向。STATUS 仍保留 `servo=<us>` 便于诊断，同时新增 `head_offset=<us>`。

用户本轮实测 `head_offset`：CENTER = 0，物理 LEFT 最大约 -700 us，物理 RIGHT 最大约 +700 us，符号与高层语义一致。最大行程不等于车身旋转阈值；220/100 us 与驻留时间仍需完整跟随试验检验。

### 滞回与驻留

进入阈值大于退出阈值，且进入/退出都需要驻留。这样可避免 Servo 回中附近的噪声造成底盘左右反复抖动。若偏移方向从左直接翻到右，策略先停车，再用新的进入驻留确认相反方向，不会在一帧里反向切换。

## 5. 距离策略

首版距离指标为人物框归一化高度：

```text
normalized_bbox_height = bbox_height / source_height
```

同时记录 normalized width 和 area ratio，但不用于首版驾驶。归一化高度比像素面积更容易跨 640×480、旋转后的画面尺寸和不同输出分辨率比较，但它仍不是物理距离，必须针对当前镜头、安装高度和目标人物标定。

第一轮实机 bbox 记录如下。每格为 normalized **height / width**，首版驾驶仍只用 height：

| 距离 | P10 height / width | P50 height / width | P90 height / width |
|---:|---:|---:|---:|
| 0.5 m | 0.855 / 0.475 | 0.959 / 0.505 | 0.963 / 0.520 |
| 0.8 m | 0.890 / 0.348 | 0.906 / 0.368 | 0.920 / 0.430 |
| 1.0 m | 0.710 / 0.318 | 0.713 / 0.320 | 0.720 / 0.340 |
| 1.2 m | 0.572 / 0.250 | 0.574 / 0.255 | 0.599 / 0.260 |
| 1.5 m | 0.510 / 0.235 | 0.520 / 0.250 | 0.530 / 0.270 |
| 2.0 m | 0.330 / 0.220 | 0.338 / 0.224 | 0.346 / 0.240 |

首次实机 drop-in 滞回：

| 分类 | 进入 | 退出/保持 |
|---|---:|---:|
| FAR | height <= 0.53 | 保持到 height >= 0.57 |
| GOOD | 两个区域之间 | 进入 FAR/NEAR 阈值后退出 |
| NEAR | height >= 0.84 | 保持到 height <= 0.78 |

`0.53` 对应 1.5 m 的 P90，便于约 1.5 m 及更远进入 FAR；`0.57` 略低于 1.2 m 的 P10 0.572，使靠近约 1.2 m 后退出 FAR。`0.84` 低于 0.8 m 的 P10 0.890，使约 0.8 m 进入 NEAR；`0.78` 高于 1.0 m 的 P90 0.720，保留退出 NEAR 的间隔。约 1.0～1.2 m 是主要 GOOD 区域。策略仍是 FAR 且已对齐才低速前进，GOOD/NEAR 停车、绝不自动后退。0.5 m 高度分布与 0.8 m 并非完全单调，需结合实际画面检查裁切或姿态影响，不能把 height 当成可靠的米制测距。这些都是首轮参数，完整人物跟随后允许微调。

## 6. UART 协议扩展与 lease

新增：

```text
FOLLOW_MOVE FORWARD <speed>
FOLLOW_MOVE ROTATE_LEFT <speed>
FOLLOW_MOVE ROTATE_RIGHT <speed>
FOLLOW_MOVE STOP
```

这些命令只在 `MODE FOLLOW` 合法。`BACKWARD`、`SHIFT_*` 和弧线动作会被拒绝。

MCU 使用独立 `lastFollowMotionCommandMs` 和 `kFollowCommandTimeoutMs=500`。只有合法非 STOP `FOLLOW_MOVE` 刷新运动租约；PING、STATUS、TARGET 和 MANUAL MOVE 都不刷新。

LongPet 侧没有用定时器机械重发上一条动作来“保活”。每个新的 `frameSequence` 到达后，Follow Controller 都重新检查 present/fresh、目标年龄、模式、视频通话、Motion 状态、fault、头偏和距离，条件仍成立才发送下一条 `FOLLOW_MOVE`。视觉不再产生新证据时，LongPet 不刷新动作，MCU 的独立 500 ms lease 会作为最后一道停车边界。

## 7. 控制权和安全优先级

优先级：

```text
STOP / fault / disconnect
  > MANUAL remote control / video call
  > PERSON_FOLLOW
  > HEAD_ONLY
  > SAFE
```

实现行为：

- 开机默认不启用人物跟随，故意没有 `LONGPET_AUTO_FOLLOW_ENABLED`；
- MANUAL 建立前先预留控制权，再释放自动模式，关闭同步回调重入窗口；
- MANUAL 抢占 PERSON_FOLLOW 后模式变为 DISABLED，结束远控不会恢复；
- 视频通话开始会关闭 PERSON_FOLLOW，通话结束不会恢复；
- UART 断线、MCU offline/fault 或意外离开 FOLLOW 会关闭人物跟随；重连不会恢复；
- HEAD_ONLY 保留 V2.2 在普通 MANUAL/视频暂停结束后的恢复语义；
- target stale/lost/expiry 立即停车，但保留用户的 PERSON_FOLLOW 意图，重新检测后从 ACQUIRING 开始；
- FOLLOW 运动从不与 MANUAL MOVE 并发。

## 8. FamilyLink 与 UI

新增首选接口：

```text
GET /api/v1/automatic-tracking
PUT /api/v1/automatic-tracking
```

PUT 示例：

```json
{ "mode": "PERSON_FOLLOW" }
```

旧 `/api/v1/automatic-head-tracking` 保留，布尔 `enabled=true` 只表示 HEAD_ONLY。新快照增加：mode、followState、distanceClass、normalized bbox width/height/area、headDirection、headOffsetUs、chassisMotion、targetStableMs。

AI 视野页用三按钮互斥选择模式；调试网格显示感知、物理头偏、距离分类、跟随状态和底盘动作。切换到远程操控会在板端触发 MANUAL 抢占并永久关闭本轮人物跟随。

## 9. 配置

systemd 示例已加入：

```text
LONGPET_MOTION_STATUS_POLL_MS=250
LONGPET_FOLLOW_TARGET_STABLE_MS=600
LONGPET_FOLLOW_MOTION_STATUS_MAX_AGE_MS=750
LONGPET_FOLLOW_ALIGN_ENTER_US=220
LONGPET_FOLLOW_ALIGN_EXIT_US=100
LONGPET_FOLLOW_ALIGN_ENTER_DWELL_MS=400
LONGPET_FOLLOW_ALIGN_EXIT_DWELL_MS=400
LONGPET_FOLLOW_MIN_MOTION_MS=300
LONGPET_FOLLOW_FAR_ENTER_HEIGHT=0.28
LONGPET_FOLLOW_FAR_EXIT_HEIGHT=0.34
LONGPET_FOLLOW_NEAR_ENTER_HEIGHT=0.78
LONGPET_FOLLOW_NEAR_EXIT_HEIGHT=0.70
LONGPET_FOLLOW_FORWARD_SPEED=12
LONGPET_FOLLOW_ROTATE_SPEED=10
```

上面是主 service 的软件基线值；实测后的四个距离值不通过修改主 service 写入。新增 `deploy/longpet-follow.conf.example`，用户可独立安装为 `/etc/systemd/system/longpet.service.d/follow.conf`，覆盖 FAR `0.53/0.57`、NEAR 退出/进入 `0.78/0.84`，并保留头身阈值 `220/100 us`、驻留 `400/400 ms`、速度 `12/10` 等 V2.3 参数。详见 Calibration Guide 与 `deploy/配置说明.md`；drop-in 不启用开机自动行驶。

## 10. 主要修改文件

LongPet：

- `src/services/AutomaticHeadTrackingService.*`：统一自动模式和 V2.3 策略状态机；
- `src/services/MotionService.*`、`MotionPorts.h`：FOLLOW 所有权和命令门控；
- `src/model/AutomaticHeadTrackingModels.*`、`MotionModels.*`：状态、距离、物理头偏遥测；
- `src/platform/EspMotionProtocol.*`、`EspSerialAdapter.*`：协议与 STATUS；
- `src/app/FamilyLinkController.*`、`src/services/FamilyLinkService.*`：REST；
- `src/app/Application.cpp`、`deploy/*`：配置；
- `tests/MotionV1Test.cpp`：状态机、安全抢占和协议测试。

Family Desktop：HTTP Adapter、Service、IPC/Preload、Mock、`VisionMonitorView.jsx`、样式和三组测试。

MCU：`xiao_che.ino`、`motion_config.h`、`head_direction.h`、新增 `follow_safety.h`、host 测试和协议/基线/台架文档。MCU 工作区还包含上一轮未提交的 V2.2 方向修正，本轮没有覆盖或丢弃它们。

## 11. 测试与构建结果

| 检查 | 结果 |
|---|---|
| Windows Qt 6 Release，Vision ON | PASS |
| LongPet CTest | 3/3 PASS（V02、VisionV1、MotionV1） |
| V2.3 头身/距离/抢占单元测试 | PASS |
| Family Desktop Node tests | 41/41 PASS |
| Family Desktop syntax check + Vite production build | PASS |
| MCU `head_direction` / `follow_safety` host C++ test | PASS |
| WSL Ubuntu 24.04 LoongArch Release，Vision ON | PASS，首次 91/91；最终源码增量 4/4 |
| 交叉产物 | LoongArch ELF，interpreter `/lib64/ld-linux-loongarch-lp64d.so.1` |
| ESP Arduino 全工程编译 | PASS：Arduino CLI 1.5.1 + ESP32 Core 2.0.14，`esp32:esp32:esp32s3` |
| ESP 编译资源占用 | Flash 318121 bytes（24%）；全局变量 20980 bytes（6%），剩余动态内存 306700 bytes |
| MCU/UART/FOLLOW 基础实机测试 | 用户报告通过：MANUAL/FOLLOW/HEAD_ONLY、watchdog、TARGET_LOST、模式隔离、非法命令拒绝 |
| FOLLOW 独立租约实机测试 | 用户报告通过：持续 TARGET 或 PING 都不能延长 FOLLOW 运动租约 |
| 第一轮 bbox 距离采样 | 用户提供 0.5～2.0 m 的 P10/P50/P90，见上表 |
| 完整人物跟随实机运动 | 尚未报告最终结果：头身对齐、距离停车/前进、抢占、连续性待测 |

Windows 测试还覆盖了：同帧去重、旧目标过期、头偏时不前进、回中后才接近、距离滞回、NEAR 不后退、MANUAL/视频/断线不恢复 FOLLOW，以及 FOLLOW 协议拒绝 BACKWARD。

ESP 编译使用临时便携工具链完成，没有修改或烧录用户设备。`Servo 1.3.0` 的库元数据给出架构兼容性提示，但源码成功完成编译与链接；该提示保留为后续工具链整理项，不冒充硬件验证结果。

## 12. 已知限制与后续

- bbox 高度受人物姿态、遮挡和检测框变化影响，不是真实米制距离；
- 当前只跟随 VisionService 选定的单一人物，不做人脸身份识别；
- 没有碰撞、悬崖、超声或激光测距传感器，正式落地必须保持低速并有人看护；
- 没有自动后退，因此过近时只停车；
- 没有弧线速度控制，动作是原地对齐与直行的离散状态机；
- ESP 全工程已用临时 Arduino CLI 环境完成编译和链接，但 Servo 1.3.0 的库元数据仍会给出架构兼容性警告；Codex 未烧录，用户已在实机完成基础协议验证；
- 基础 MCU/UART/FOLLOW 项已由用户确认通过，但头身闭环、落地距离控制与连续人物跟随仍 pending，需按 Test Guide 完成。

只有当已通过的基础协议结果与对齐滞回、距离阈值、失联停车和连续跟随的完整实测结果一并确认后，才能宣布 V2.3 hardware PASS。

实机参数采集与调整见 `LongPet-Vision-V2.3-Follow-Calibration-Guide.md`；分阶段验收矩阵见 `LongPet-Vision-V2.3-Follow-Test-Guide.md`。下一阶段不建议立即加入更复杂运动：物理方向、FOLLOW lease 和 bbox 样本已完成首轮验证，应继续回填头身对齐、低速连续跟随、视频/人工抢占与失联停车的完整结果；通过后再考虑加入障碍/悬崖传感器、连续 `vx/wz` 控制或身份目标选择。
