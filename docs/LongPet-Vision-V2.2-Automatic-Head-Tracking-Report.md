# LongPet Vision V2.2 — Automatic Head Tracking

日期：2026-09-12  
状态：代码与自动化构建完成；ESP 烧录和整机动作结论待用户实测

## 1. 本轮边界

V2.2 只把 Vision V2.0 的人物位置送到头部 Servo：

```text
CameraCaptureAdapter
  -> VisionService (Tinyissimo + Sparse LK)
  -> TargetObservation
  -> AutomaticHeadTrackingService
  -> MotionService
  -> MotionPort / EspSerialAdapter
  -> TARGET dx dy area
  -> ESP HEAD_ONLY
  -> head Servo
```

没有实现、也没有打开自动底盘旋转、前后移动、横移、`FOLLOW chassis` 或基于 area 的距离跟随。
`HEAD_ONLY` 仍由 MCU 执行层硬性阻止四轮自动动作。

## 2. 分层与职责

### Vision 层

`VisionService` 继续只产生 `TargetObservation`，不知道 UART、ESP 或控制模式。摄像头统一旋转后，
observation 中的 `sourceSize`、bbox、归一化中心和大小处于同一个坐标系。

### 自动跟头 Service

新增 `AutomaticHeadTrackingService`，负责：

- 用户启用状态；
- Vision/Motion/VideoCall 生命周期门控；
- observation 新鲜度和 frame sequence 去重；
- bbox 到协议 `TARGET` 的几何换算；
- 自动 `HEAD_ONLY` 的进入、退出和状态显示；
- target lost 与超时处理；
- 对普通目标日志限频。

它不直接访问串口，也不实现第二套 Servo PID。

### Motion 层

`MotionService` 是唯一控制权仲裁者：

```text
fault / STOP
  > MANUAL remote control
  > AUTO HEAD_ONLY
  > SAFE
```

它通过 `MotionPort::sendTarget()` 发给 `EspSerialAdapter`。协议序列化集中在
`EspMotionProtocol::targetCommand()`。

### FamilyLink 与 UI

板端增加：

```text
GET /api/v1/automatic-head-tracking
PUT /api/v1/automatic-head-tracking
body: {"enabled": true|false}
```

响应包含 `enabled`、`active`、`state`、Vision 状态、frame sequence、target age、`dx/dy/area`、
说明文字和更新时间。家属端仍按 Adapter -> Service -> IPC -> preload -> React 分层访问接口。

AI 视野页增加“自动跟随头部”开关和简要状态；调试信息中可查看 Auto Head 状态、Motion 模式、
目标偏差和年龄。默认关闭。

## 3. TARGET 计算

优先使用 observation 已有的归一化中心和尺寸，并乘以本帧实际 `sourceSize`：

```text
center_x = normalized_center_x * source_width
center_y = normalized_center_y * source_height
width    = normalized_width    * source_width
height   = normalized_height   * source_height

dx   = round(center_x - source_width  / 2)
dy   = round(center_y - source_height / 2)
area = round(width * height)
```

归一化数据不可用时才回退到像素 bbox，并先裁剪到源画面。无效尺寸、空 bbox 或超出 Motion
Protocol V2 范围的数据被拒绝并转为 lost，不会下发畸形命令。

例：640x480 画面，人物中心 `(0.25, 0.5)`、大小 `(0.2, 0.4)`：

```text
TARGET -160 0 24576
```

因此人物在画面左侧得到负 `dx`，右侧得到正 `dx`。`area` 只作为 bbox 像素面积传输，本轮
绝不用于底盘运动。

## 4. 新鲜度与慢 Detector

自动服务只消费满足下列条件的 observation：

- `present=true`；
- `fresh=true`；
- 状态为 `DETECTED/TRACKING/CORRECTED/REACQUIRED`；
- 由 capture timestamp 和上游 age 共同计算的年龄不超过阈值；
- `frameSequence` 尚未消费过。

默认 `maximumTargetAgeMs=500`。收到有效目标后启动一次性 `targetExpiryMs=500` 计时器；期间若
没有新的 observation，只发送一次：

```text
TARGET 0 0 0
```

然后等待新帧。不会为了维持 10 Hz 而重复旧 dx，所以 Tinyissimo correction 阻塞时，旧方向不会
让 Servo 一直转到限位。MotionService 的状态轮询/PING 与 target freshness 相互独立，不能伪造新目标。

## 5. MANUAL 抢占

当家属端运动 WebSocket 请求进入 MANUAL：

1. MotionService 撤销 AUTO HEAD 所有权；
2. 停车并切换 `MODE MANUAL`；
3. 自动服务进入 `MANUAL_OVERRIDE`，停止消费视觉目标；
4. J/K/L 和底盘命令只由人工会话控制。

人工会话退出后先回 SAFE。若用户的自动跟头开关仍为开启，自动服务重新申请 `HEAD_ONLY`，但不会
复用抢占前的 bbox；它等待下一条新 observation 后才继续。

## 6. VideoCall 与摄像头生命周期

视频通话 active 时：

- 自动服务结束 `HEAD_ONLY` 并清 target；
- 状态显示 `VIDEO_CALL_SUSPENDED`；
- Vision 原有暂停和摄像头共享规则保持不变；
- 不再消费最后一个 bbox。

通话结束、Vision 真正恢复后，若开关仍开启则重新进入 `HEAD_ONLY` 的 searching 状态，并等待新
observation。通话前目标不会被重放。

## 7. Servo 左右方向修正

后续实机反馈确认旧固件把协议 LEFT/RIGHT 的物理动作做反。V2.2 在 MCU 层修正，而不是在家属端
交换 J/L 或按钮：

```text
MotionConfig::kServoPhysicalLeftPulseSign = +1
physical LEFT  -> pulse increase
physical RIGHT -> pulse decrease
```

`head_direction.h` 是唯一方向映射：

- `HEAD LEFT/RIGHT` 使用物理方向 helper；
- `TARGET dx < 0` 使用同一个 physical-left helper；
- `TARGET dx > 0` 使用同一个 physical-right helper。

以后改变舵机或安装方向时只修改这一项配置，协议和 UI 仍保持真实 LEFT/RIGHT 语义。代码测试已经
验证两条命令路径符号一致；物理动作必须在用户重烧固件后确认。

## 8. 状态

对外状态包括：

| 状态 | 含义 |
|---|---|
| `DISABLED` | 用户未开启 |
| `WAITING_FOR_VISION` | Vision 未启用、不可用或暂停 |
| `WAITING_FOR_MOTION` | UART/MCU 未就绪或无法进入 HEAD_ONLY |
| `SEARCHING` | 已持有 HEAD_ONLY，等待新人物目标 |
| `TRACKING` | 最近一条新鲜目标已发送 |
| `MANUAL_OVERRIDE` | 人工遥控正在抢占 |
| `VIDEO_CALL_SUSPENDED` | 通话期间暂停 |
| `FAULT` | Motion MCU 报告 fault |

目标日志按默认 2 秒限频；enabled/disabled、HEAD_ONLY、lost、stale、manual override、video-call
suspend/resume 等关键状态变化单独记录。

## 9. 配置

```ini
Environment="LONGPET_MOTION_ENABLED=1"
Environment="LONGPET_MOTION_DEVICE=/dev/ttyS2"
Environment="LONGPET_AUTO_HEAD_ENABLED=0"
Environment="LONGPET_AUTO_HEAD_MAX_TARGET_AGE_MS=500"
Environment="LONGPET_AUTO_HEAD_TARGET_EXPIRY_MS=500"
```

`LONGPET_AUTO_HEAD_ENABLED=0` 是安全默认值。家属端动态开关在当前进程内生效；重启后仍按 systemd
环境变量决定初始值。若 Vision 或 Motion 未就绪，开启请求保留用户意图并进入 waiting 状态，不会
误报正在 TRACKING。

## 10. 自动测试与构建

覆盖内容：

- 640x480、1024x600 的 bbox 换算；
- 左负右正 dx；
- fresh target 下发；
- stale/lost 只下发 lost；target expiry 只发送一次 lost，不重复旧 dx；
- frame sequence 去重；
- MANUAL 抢占及退出后恢复；
- VideoCall suspend/resume；
- 自动流程没有任何 `MOVE`；
- TARGET 协议范围和 STATUS target 字段；
- 家属端 GET/PUT、布尔校验、mock 默认关闭和 UI production build；
- MCU HEAD/TARGET 共用物理方向映射。

实际结果：

| 检查 | 结果 |
|---|---|
| Windows Release build (`LONGPET_BUILD_TESTS=ON`, host Vision stub) | 通过 |
| Windows CTest | 3/3 targets 通过，0 failed，25.19 s |
| Family Desktop `npm test` | 38/38 通过 |
| Family Desktop `npm run check` / Vite production build | 通过；仅有既存 lottie eval 与 bundle size 告警 |
| MCU `head_direction_test.cpp` host C++17 | 通过 |
| WSL Ubuntu 24.04 LoongArch Release (`LONGPET_ENABLE_VISION=ON`) | 通过 |
| LoongArch ELF | `LongPet` 与 `LongPetVisionBench` 均确认为 LoongArch LP64D |
| Arduino/ESP32-S3 全量编译 | 按用户 2026-09-12 指示跳过 |

LoongArch 产物位于 WSL：

```text
/home/justin/build/LongPet-my-loongarch64-release/LongPet
/home/justin/build/LongPet-my-loongarch64-release/LongPetVisionBench
```

交叉构建只有既存 `AiChatMessage` missing-field-initializers 告警，与本轮视觉/运动代码无关。没有 SSH、
SCP、板端执行、ESP 烧录或实机动作测试。

## 11. 修改文件

LongPet：

- 新增 `src/model/AutomaticHeadTrackingModels.{h,cpp}`；
- 新增 `src/services/AutomaticHeadTrackingService.{h,cpp}`；
- 修改 `MotionModels`、`MotionPorts`、`MotionService`、`EspMotionProtocol`、`EspSerialAdapter`；
- 修改 `Application`、`FamilyLinkService`、`FamilyLinkController`、`FamilyMotionControlAdapter`；
- 修改 `CMakeLists.txt`、`tests/MotionV1Test.cpp`、README 和 deploy 配置；
- 新增本报告与 `LongPet-Vision-V2.2-Head-Tracking-Test-Guide.md`。

Family Desktop：

- 修改 HTTP/mock Adapter、FamilyLink Service、IPC Controller 和 preload bridge；
- 修改 `VisionMonitorView.jsx` 与样式；
- 修改 FamilyLink API 文档；
- 扩展 service、HTTP Adapter 和 mock Adapter 测试。

Motion MCU：

- 新增 `xiao_che/head_direction.h` 和 `tests/head_direction_test.cpp`；
- 修改 `motion_config.h` 与 `xiao_che.ino`；
- 更新 README、`motion-protocol-v2.md`、`firmware-baseline.md` 和 `bench-test-report.md`。

## 12. 已知限制

- 只有水平单 Servo；`dy` 暂不执行。
- MCU 使用 deadband + 有界增量修正，不是 PID，速度和稳定性仍需实机调参。
- 500 ms 新鲜度/expiry 是安全首版值；Tinyissimo correction 期间可能短暂进入 lost，这是停止旧方向
  累积的设计结果。
- 开关尚未写入数据库；重启初值来自 systemd 环境。
- 多人场景沿用 Vision V2.0 当前主目标选择，不做身份识别。
- 未实现头身协调和自动底盘控制。

## 13. 下一阶段建议

先完成本轮测试指南中的方向、左右来回、目标丢失、MANUAL 和 VideoCall 验收，再根据实测记录
调节 MCU deadband、correction divisor 和每帧最大 correction。头部闭环稳定后，下一阶段只应增加
“头部接近左右软限位时请求底盘小角度回正”的上层协调意图；底盘必须有独立授权、限速、超时和避障
门控，不能让当前 `TARGET area` 直接进入 FOLLOW chassis。
