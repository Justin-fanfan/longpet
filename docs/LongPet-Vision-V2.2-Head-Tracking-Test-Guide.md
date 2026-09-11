# LongPet Vision V2.2 自动跟头实机测试指南

本指南由用户执行。代码实现过程没有 SSH/SCP 到板子、没有启动板端程序、没有烧录 ESP。

## 1. 测试前准备

1. 分别备份当前 LongPet 可执行文件、systemd 配置和 ESP 固件。
2. 编译并部署启用 Vision 的 LoongArch Release `LongPet`。
3. 用 Arduino ESP32 Core 2.0.14 编译并烧录本轮 `longpet-motion-mcu/xiao_che`。
4. 确认摄像头统一旋转配置仍为当前实机值，不能只在家属端 CSS 中旋转画面。
5. 让四轮悬空，或断开电机驱动主电源但保留 MCU/Servo 供电，完成 A、B、D、F、G 后再落地。
6. 保持独立硬件急停可用。软件 STOP 不能替代硬件急停。

LongPet systemd 建议配置：

```ini
Environment="LONGPET_VISION_ENABLED=1"
Environment="LONGPET_VISION_TRACKING_ENABLED=1"
Environment="LONGPET_MOTION_ENABLED=1"
Environment="LONGPET_MOTION_DEVICE=/dev/ttyS2"
Environment="LONGPET_AUTO_HEAD_ENABLED=0"
Environment="LONGPET_AUTO_HEAD_MAX_TARGET_AGE_MS=500"
Environment="LONGPET_AUTO_HEAD_TARGET_EXPIRY_MS=500"
```

先保留自动跟头默认关闭，从家属端逐次开启。

## 2. 观察日志

```bash
journalctl -u longpet.service -b -f -o cat
```

需要看到的关键字：

```text
AUTO_HEAD enabled
AUTO_HEAD HEAD_ONLY entered
AUTO_HEAD target dx=... dy=... area=...
AUTO_HEAD target lost
AUTO_HEAD stale target dropped
AUTO_HEAD manual override
AUTO_HEAD resume after manual
AUTO_HEAD video-call suspend
AUTO_HEAD video-call resume
```

普通 target 日志被限频，不能用“每个视觉帧是否都有日志”判断是否工作。

## 3. A — MCU 左右语义

自动跟头保持关闭，通过现有串口调试链路逐条执行：

```text
MODE MANUAL
HEAD CENTER
HEAD LEFT 20
HEAD CENTER
HEAD RIGHT 20
HEAD CENTER
```

验收：

- `HEAD LEFT` 真实向设备自身左侧；
- `HEAD RIGHT` 真实向设备自身右侧；
- `HEAD CENTER` 回到正常中心；
- 每次只发小步，确认方向前不要连续发送到软限位。

若仍相反，不要交换家属端按钮。把实机现象和 `STATUS` 的 `servo=` 变化记录下来，再调整 MCU
唯一配置 `kServoPhysicalLeftPulseSign`。

## 4. B — HEAD_ONLY 基础闭环

1. 在家属端连接真实 LongPet。
2. 打开“AI 视野”，确认画面和人物框正常。
3. 打开“自动跟随头部”。
4. 查看状态应依次为 `SEARCHING`、`TRACKING`，Motion 模式为 `HEAD_ONLY`。
5. 人站画面中央 5 秒：头应基本不动，允许 deadband 内的小误差。
6. 人缓慢走到画面左侧：头应真实向左跟。
7. 回中央，再走到画面右侧：头应真实向右跟。

全程观察四轮：任何由 Vision 引起的轮子动作都判定为严重失败，立即硬件急停。

## 5. C — 连续左右走动

人物在可见范围内左右来回移动 5 轮：

- 头部持续跟随且方向一致；
- 中央附近不过度左右抖动；
- 不突然跳向上一位置；
- Servo 不撞机械限位；
- 家属端 dx 符号随人物左右正确变化。

记录是否存在过慢、过冲或抖动，以及当时 `dx` 和 `servo=`。这些数据用于调整 MCU deadband、
divisor 和 maximum correction，不要同时在龙芯侧另加 PID。

## 6. D — 目标丢失和慢 Detector

1. 人物走出画面并保持 3 秒。
2. 头部应停止在当前位置，不能沿最后方向一直转到限位。
3. 家属端状态进入 `SEARCHING/LOST`，target area 变为 0。
4. 人物重新进入，Vision reacquire 后头部重新跟随。
5. 观察一次较慢 Tinyissimo detector correction；若 500 ms 内没有新 tracker observation，允许短暂
   target lost，但不能反复执行旧 dx。

日志中同一次失联不应无限刷 `AUTO_HEAD target lost`。

## 7. E — 快速开关和重启初值

1. 连续执行“开 -> 关 -> 开”，每次等待 UI 返回。
2. 关闭后不再发送正常 TARGET，MCU 回 SAFE。
3. 再开启后必须等待新 observation，不复用关闭前目标。
4. 配置 `LONGPET_AUTO_HEAD_ENABLED=0` 时重启，开关应为关闭。
5. 若改为 1 后重启，应按 Vision/Motion 实际状态显示 tracking 或 waiting，而不是无条件显示成功。

## 8. F — MANUAL override

1. 自动跟头开启且正在 TRACKING。
2. 切到家属端“远程操控”。
3. 状态应为 `MANUAL_OVERRIDE`，MCU 进入 `MANUAL`。
4. 使用 J/K/L 或按钮控制头部，确认没有自动目标与人工抢 Servo。
5. 小幅测试 W/A/S/D；松键、失焦和 STOP 仍按 Remote Control V1 安全停车。
6. 退出远控后，MCU 先回 SAFE；因开关仍开启，随后恢复 `HEAD_ONLY` 并等待一个新视觉帧。

验收重点：MANUAL 期间人工命令优先；退出后不出现 MANUAL/HEAD_ONLY 来回争抢。

## 9. G — VideoCall

1. 自动跟头开启并跟踪一个偏离中心的人物。
2. 发起视频通话。
3. Vision 暂停后头部不能继续沿最后 bbox 转动，状态为 `VIDEO_CALL_SUSPENDED`。
4. 确认原视频通话画面、声音和摄像头释放流程无回归。
5. 挂断后等待 Vision 恢复；状态应先 searching，再随新 observation tracking。

通话结束后绝不能立即重放通话前旧 target。

## 10. H — 故障与断线

在四轮安全悬空条件下：

- 暂时断开 Motion UART：状态应进入 `WAITING_FOR_MOTION`，不继续控制；
- 恢复 UART：MCU 响应新 STATUS 后可重新进入 HEAD_ONLY；
- Vision 暂停/不可用：进入 `WAITING_FOR_VISION`；
- MCU fault：进入 `FAULT`，不得继续 TARGET；
- FamilyLink 断线：页面给出可理解错误，板端安全状态不能依赖 UI 是否仍打开。

## 11. 建议记录表

| 项目 | 结果 | 现象/日志 |
|---|---|---|
| HEAD LEFT 真实向左 | 待测 | |
| HEAD RIGHT 真实向右 | 待测 | |
| TARGET dx<0 真实向左 | 待测 | |
| TARGET dx>0 真实向右 | 待测 | |
| 中央 deadband 稳定 | 待测 | |
| 左右往返 5 轮 | 待测 | |
| target lost 不持续转 | 待测 | |
| detector 阻塞不重复旧 dx | 待测 | |
| MANUAL override | 待测 | |
| MANUAL 退出恢复 | 待测 | |
| VideoCall suspend/resume | 待测 | |
| UART 断开/恢复 | 待测 | |
| HEAD_ONLY 四轮始终不动 | 待测 | |

把该表、LongPet journal 和 MCU `STATUS`/首个 fault 日志一起回传，才能形成 V2.2 的实机验收结论。
