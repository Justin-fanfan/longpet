# LongPet Family Remote Control V1 实施报告

日期：2026-09-11  
状态：软件实现与 Windows 本地自动化测试完成；按本轮要求未执行 LoongArch 交叉编译、部署或车辆动作测试。

## 1. 本轮结论

Family Remote Control V1 已把家属端实时画面、独立运动控制通道和板端 Motion Service 接入现有分层。家属端可在同一个“AI 视野”页面切换“AI 视野 / 远程操控”，画面不中断；远程模式提供底盘前进、后退、原地左转、原地右转、STOP，以及头部左转、回中、右转。

运动命令没有进入 UI、VisionService 或 FamilyLink HTTP Adapter。板端调用链为：

```text
Family Desktop React UI
  -> Renderer MotionControlAdapter
  -> 独立 WebSocket :8790 /motion-control/v1
  -> FamilyMotionControlAdapter
  -> MotionService
  -> MotionPort
  -> EspSerialAdapter
  -> /dev/ttyS2 @ 115200 8N1
  -> Motion MCU
```

HTTP `:8787` 只负责 FamilyLink Bearer 鉴权并签发 30 秒有效的临时运动会话；JPEG/视觉元数据仍走 `:8789`，运动控制走独立的 `:8790`，不会排在 JPEG 帧之后。

本轮没有暴露 MCU 的 `SHIFT` 指令。该方向当前硬件文档标为未可靠验证，不适合作为面向家属的正式操作。

## 2. 架构与职责

### LongPet

- `Application`：读取环境变量，装配并启动串口 Adapter、控制 WebSocket Adapter 和 `MotionService`。
- `FamilyLinkController`：提供创建临时运动会话的 HTTP 入口，不处理 UART 指令。
- `FamilyLinkService`：把 FamilyLink 能力与 `MotionService` 连接起来。
- `MotionService`：拥有 MANUAL 生命周期、底盘刷新、远端租约、MCU 在线判断、故障与停车策略。
- `FamilyMotionControlAdapter`：验证临时会话，解析带版本号的二进制控制帧，只向 Service 发出强类型动作。
- `EspSerialAdapter`：管理 POSIX 串口、断线重连、行缓冲和 MCU 状态回报。
- `EspMotionProtocol`：集中生成和解析 MCU ASCII 协议。

`VisionService`、页面和 Widget 均不接触串口。以后人物跟随需要驱动车体时，也应调用 `MotionService` 提供的业务接口，而不是从 Vision 层发送 UART 文本。

### Family Desktop

- React 页面只表达用户意图和显示状态。
- `motion-control-adapter.js` 负责 WebSocket、LPMF 控制帧、按住刷新、松开停止和窗口级安全事件。
- preload 只开放 `startMotionControl()` 白名单 IPC。
- Electron 主进程通过现有 `FamilyLinkService -> HttpFamilyLinkAdapter` 请求会话，Bearer Token 不进入 Renderer。

## 3. 会话与协议

### 3.1 创建会话

```http
POST /api/v1/motion-control/sessions
Authorization: Bearer <FamilyLink Token>
```

成功返回 HTTP 201：

```json
{
  "sessionId": "temporary-session-id",
  "sessionToken": "temporary-random-token",
  "port": 8790,
  "protocolVersion": 1,
  "mediaFrameVersion": 1,
  "refreshIntervalMs": 150,
  "leaseTimeoutMs": 350,
  "defaultSpeed": 20,
  "headStepUs": 20,
  "expiresAt": "2026-09-11T12:00:30.000Z"
}
```

同一时间只允许一个待连接或已连接的控制会话。已有会话时返回 HTTP 409 `MOTION_CONTROL_BUSY`；功能未启动时返回 HTTP 503 `MOTION_CONTROL_UNAVAILABLE`。临时 token 不写入日志或磁盘。

家属端根据已配置的 FamilyLink URL 推导控制地址，只替换协议和端口：

```text
http://<device>:8787 -> ws://<device>:8790/motion-control/v1
https://<device>     -> wss://<device>:8790/motion-control/v1
```

代码中没有固定当前开发板 IP。

### 3.2 WebSocket 控制消息

消息复用现有 LPMF 二进制帧头，`streamType=control`；payload 是 UTF-8 JSON。建立连接后第一帧必须在 6 秒内完成鉴权：

```json
{
  "type": "authenticate",
  "protocol_version": 1,
  "session_id": "temporary-session-id",
  "token": "temporary-random-token"
}
```

底盘与头部指令：

```json
{ "type": "chassis", "direction": "FORWARD", "speed": 20 }
{ "type": "chassis", "direction": "BACKWARD", "speed": 20 }
{ "type": "chassis", "direction": "ROTATE_LEFT", "speed": 20 }
{ "type": "chassis", "direction": "ROTATE_RIGHT", "speed": 20 }
{ "type": "stop" }
{ "type": "head", "action": "LEFT", "step_us": 20 }
{ "type": "head", "action": "CENTER" }
{ "type": "head", "action": "RIGHT", "step_us": 20 }
{ "type": "release" }
```

服务端状态包含 `uart_available`、`mcu_online`、`fault`、`remote_control_active`、`mode`、`motion`、`stop_reason`、`servo_us`、`imu_available` 和更新时间。只有 `mcu_online=true`、`fault=false`、`mode=MANUAL` 时，家属端才解锁动作按钮。

## 4. UART 协议与状态链路

当前板卡实测串口节点是 `/dev/ttyS2`，参数为 115200、8 数据位、无校验、1 停止位、无软硬件流控。`EspMotionProtocol` 发送：

```text
STOP
MODE SAFE
MODE MANUAL
MOVE FORWARD 20
MOVE BACKWARD 20
MOVE ROTATE_LEFT 20
MOVE ROTATE_RIGHT 20
HEAD LEFT 20
HEAD CENTER
HEAD RIGHT 20
STATUS
```

接收并解析 MCU 当前格式：

```text
[STATUS] mode=MANUAL motion=STOPPED stop=REMOTE_STOP fault=0 target=0 servo=1570 imu=1
```

任何不符合已知枚举、速度 `1..100` 或头部步长 `1..100 us` 的网络指令均在到达串口前拒绝。

## 5. 安全策略

底盘采用三级失效保护：

```text
浏览器按住刷新：150 ms
        ↓ 中断
LongPet 远端租约：350 ms -> STOP
        ↓ LongPet/UART 整体失效
Motion MCU watchdog：500 ms -> STOP
```

LongPet 把刷新周期限制为 100–200 ms，把远端租约限制在“刷新周期 + 50 ms”到 450 ms 内，确保默认早于 MCU watchdog。

以下事件会请求立即停车并清空按住状态：

- 底盘按钮 `pointerup` / `pointercancel`；
- W/S/A/D 对应 `keyup`；
- 点击 STOP 或按 Space；
- 按 Esc 退出远控；
- Electron 窗口失去焦点；
- 页面隐藏、最小化或关闭；
- 从远程操控切换回 AI 视野；
- 切换设备连接；
- WebSocket 断开；
- UART 断开、MCU 超时、MCU fault 或 MCU 离开 MANUAL；
- LongPet Motion Service 停止。

控制会话建立时先 `STOP -> MODE MANUAL -> STATUS`。退出时执行 `STOP -> MODE SAFE`。串口重新出现时也先发送 `STOP -> MODE SAFE -> STATUS`，避免延续旧状态。

头部和底盘保持独立：松开 J/L 或头部按钮只停止继续发送头部步进，舵机保持当前位置，不发送底盘 STOP；K/“回中”发送一次 `HEAD CENTER`。

## 6. 家属端交互

| 操作 | 键盘 | 行为 |
|---|---|---|
| 前进 | W | 按住移动，松开停车 |
| 后退 | S | 按住移动，松开停车 |
| 原地左转 | A | 按住移动，松开停车 |
| 原地右转 | D | 按住移动，松开停车 |
| 停车 | Space | 立即 STOP |
| 头左 | J | 按住连续步进，松开保持 |
| 头回中 | K | 单次回中 |
| 头右 | L | 按住连续步进，松开保持 |
| 停车并退出 | Esc | STOP、释放控制会话 |

远程操控和 AI 视野共用一条视觉监控连接。切换右侧面板不会重新打开摄像头；“显示 AI 人物框”只控制 Renderer Canvas 是否绘制框，不停止板端 Vision 推理。

## 7. 配置与部署

`deploy/longpet.service` 新增：

```ini
SupplementaryGroups=video input audio tty dialout
Environment="LONGPET_MOTION_ENABLED=1"
Environment="LONGPET_MOTION_DEVICE=/dev/ttyS2"
Environment="LONGPET_MOTION_CONTROL_PORT=8790"
Environment="LONGPET_MOTION_REFRESH_MS=150"
Environment="LONGPET_MOTION_REMOTE_LEASE_MS=350"
Environment="LONGPET_MOTION_DEFAULT_SPEED=20"
Environment="LONGPET_MOTION_HEAD_STEP_US=20"
```

`/dev/ttyS2` 是当前 2K300 镜像上的实测枚举，不应在更换镜像或硬件后盲目沿用。部署前应核对：

```bash
ls -l /dev/ttyS2
cat /proc/tty/driver/serial
systemctl show longpet.service -p SupplementaryGroups
```

程序应通过 `dialout` 组访问串口，不建议给串口设备设置 `0777`。

按本轮要求，本次没有上传二进制或替换板端 service。后续部署时使用仓库中的 service 文件覆盖配置，再执行：

```bash
systemctl daemon-reload
systemctl restart longpet.service
journalctl -u longpet.service -b -f
```

并确认 8787、8789、8790 均只暴露在受信任局域网。当前是 HTTP/WS 比赛局域网方案，不应直接映射至公网。

## 8. 修改文件

LongPet 新增：

- `src/model/MotionModels.h/.cpp`
- `src/services/MotionPorts.h`
- `src/services/MotionService.h/.cpp`
- `src/platform/EspMotionProtocol.h/.cpp`
- `src/platform/EspSerialAdapter.h/.cpp`
- `src/platform/FamilyMotionControlAdapter.h/.cpp`
- `tests/MotionV1Test.cpp`
- 本报告

LongPet 修改：

- `CMakeLists.txt`
- `src/app/Application.h/.cpp`
- `src/app/FamilyLinkController.h/.cpp`
- `src/services/FamilyLinkService.h/.cpp`
- `deploy/longpet.service`
- `deploy/配置说明.md`

家属端新增与修改文件见 `D:\code\family-desktop\docs\FAMILY_REMOTE_CONTROL_V1.md`。

## 9. 已完成测试

### Windows Qt Release 与 CTest

最终代码已完成 Windows Qt Release 增量构建，使用 Qt/MinGW 运行时 PATH 执行 CTest，结果为：

```text
LongPet.V02       Passed  42.20 s
LongPet.VisionV1  Passed   9.75 s
LongPet.MotionV1  Passed   3.13 s
100% tests passed, 0 tests failed out of 3
```

`LongPet.MotionV1` 覆盖协议序列化/状态解析、MANUAL 生命周期、头部与底盘独立、MOVE 刷新与租约超时、断线/重连/能力不可用、WebSocket 鉴权、非法 SHIFT 拒绝和 STOP。
Windows 构建不会执行 `Q_OS_UNIX` 下的 termios 系统调用；零长度读取修复的最终验证仍以重新部署后的板端持续在线状态为准。

### 板端只读与安全检查

- SSH 可访问当前板卡；
- `/dev/ttyS2` 存在，属组为 `dialout`；
- ESP/Motion MCU 启动后，串口内核发送和接收计数都持续增加；
- `strace` 确认 MCU 正确返回完整的 `[STATUS] mode=SAFE motion=STOPPED stop=STOP_COMMAND fault=0 target=0 servo=1570 imu=1`；
- 实测发现并修复 `EspSerialAdapter` 的零长度读取误判：`VMIN=0/VTIME=0` 下，读完当前字节后 `read()==0` 表示暂时无更多数据，不是 EOF。旧代码因此每次收到 STATUS 后关闭串口并等待 2 秒重连，造成家属端长期显示 MCU 未连接；
- 未发送 `MOVE` 或 `HEAD`，未进行车轮或舵机动作测试。

因此已经确认“真实 MCU 双向串口和 STATUS 格式正常”，但不能据此宣称“底盘运动、头部运动及全部失效停车路径通过”。修复后的板端二进制仍需重新部署验证串口能持续保持打开。

## 10. 待进行的实机验收

按本轮要求不执行交叉编译。拿到板端构建后，建议先架空车轮并按以下顺序测试：

1. 确认 MCU 固件已运行、ESP 供电与 UART TX/RX/GND 接线正确；
2. 部署包含零长度读取修复的新二进制，观察 MCU 状态持续在线而不是每 2 秒闪断；
3. 进入远控，确认状态从 SAFE 进入 MANUAL；
4. 短按头部左/右/回中，验证松开后保持且底盘不动作；
5. 分别短按 W/S/A/D，验证松开立即 STOP；
6. 长按前进时拔掉网络，验证 350 ms LongPet 租约或 500 ms MCU watchdog 停车；
7. 测试 Space、Esc、切页、最小化和关闭应用均停车；
8. 制造 MCU fault/拔掉 UART，确认 UI 锁定且不能继续发送动作；
9. 连续进入退出远控，确认每次退出回到 SAFE；
10. 恢复并确认 `longpet.service` 正常运行。

## 11. 当前限制与下一步

- UART 双向通信和 MCU STATUS 已确认；修复后的长期稳定性以及真实动作仍待重新部署验收。
- 本轮没有交叉构建和实机性能数据，无法报告控制端到端延迟。
- 当前仅支持单家属控制；不含云中继、TLS/WSS、权限分级和审计持久化。
- 没有暴露平移 SHIFT，不影响本轮前后/旋转控制目标。
- 后续 Vision 自动跟随与家属远控需要明确的模式仲裁。建议由 `MotionService` 统一管理 SAFE / HEAD_ONLY / MANUAL / FOLLOW 所有权，进入任一模式前先 STOP，禁止 Vision 与远控同时写串口。

在 MCU 能稳定返回 STATUS 且本报告第 10 节通过前，不建议把远程底盘控制用于车轮落地、无人看护或跨公网场景。
