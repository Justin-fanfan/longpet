# LongPet Vision V2.3 Follow Calibration Guide

日期：2026-09-12

本指南用于把保守默认值标定到当前摄像头、Servo 安装、场地和底盘。用户已完成 MCU/UART/FOLLOW 基础实机测试和第一轮 bbox 距离采样；本文记录的是用户提供的结果，Codex 未连接或操作开发板。完整头身协调、低速前进和连续人物跟随仍待测试。后续首次运动测试仍须抬起轮组或放在可靠台架上，并准备随时断开电机电源。软件 STOP 不能替代硬件急停。

## 1. 标定顺序

已有的基础结果与后续待测项如下；不要因为基础协议通过就直接打开完整自动跟随：

1. 已完成：MCU V2.3 STATUS、物理方向与 FOLLOW 安全租约；
2. 已完成：不同距离的 normalized bbox 数据采集；
3. 待测：头部回中噪声和对齐进入/退出阈值；
4. 待测：用首轮 FAR/GOOD/NEAR 滞回做悬空轮组测试；
5. 待测：标定旋转/前进速度；
6. 待测：空旷地面、低速、有人看护验证；
7. 最后才测试连续人物跟随。

## 2. 物理 head_offset

烧录 MCU 后，在 Servo 不动作时反复读取：

```text
STATUS
```

预期格式：

```text
[STATUS] ... servo=1570 head_offset=0 imu=1
```

依次用 MANUAL 让头部物理向左、回中、物理向右，记录：

| 姿态 | 已报告实测 head_offset |
|---|---|
| 物理左 | 最大约 -700 us |
| 回中 | 0 us |
| 物理右 | 最大约 +700 us |

用户已验证 CENTER/LEFT/RIGHT 的物理符号。若日后更换舵机或安装方向导致符号相反，不要继续测试 FOLLOW；应修正 `motion_config.h` 的 `kServoPhysicalLeftPulseSign`，HEAD、TARGET 和 STATUS 会一起使用同一映射，不要在 LongPet 或家属端交换左右。最大约 ±700 us 是行程结果，不是开始转动车身的阈值；当前 220/100 us 和 400/400 ms 驻留仍需完整跟随测试验证。

## 3. 对齐阈值

### 3.1 回中噪声

头部回中后读取至少 30 次 STATUS，记录绝对值最大值 `center_noise_max`。建议：

```text
align_exit_us >= center_noise_max + 20 us
align_enter_us >= align_exit_us + 80 us
```

默认是 exit 100 us、enter 220 us。进入阈值必须大于退出阈值。

### 3.2 机械角度映射

在安全台架上把头部大致转到 ±10°、±20°、±30°，记录 `head_offset`。选择希望机身开始追头的角度对应值作为 enter。退出值应让头部确实接近机身正前方，但不能小到受噪声影响。

### 3.3 驻留时间

- 左右轻微晃动就触发原地旋转：增大 `ALIGN_ENTER_DWELL_MS`；
- 人物明显移到侧面但响应太慢：减小 enter dwell，每次最多减 50 ms；
- 回中附近反复转停：增大 `ALIGN_EXIT_DWELL_MS` 或扩大 enter/exit 差；
- 回中后等待过久才前进：小幅减小 exit dwell。

建议保持 250～700 ms，不要为了“灵敏”直接设为 0。

## 4. 距离数据采集

保持摄像头高度、俯仰角和测试人物基本一致。在画面中央分别站到 0.5、0.8、1.0、1.2、1.5、2.0 m，家属端打开调试信息，记录稳定 5～10 秒的：

- normalized bbox height；
- normalized bbox width；
- area ratio；
- detector/tracker 状态；
- 是否全身、半身或被裁切。

用户第一轮实测如下。每格为 **normalized height / normalized width**；P10/P50/P90 是采样分位数，不是物理距离的精度保证：

| 距离 | P10 height / width | P50 height / width | P90 height / width |
|---:|---:|---:|---:|
| 0.5 m | 0.855 / 0.475 | 0.959 / 0.505 | 0.963 / 0.520 |
| 0.8 m | 0.890 / 0.348 | 0.906 / 0.368 | 0.920 / 0.430 |
| 1.0 m | 0.710 / 0.318 | 0.713 / 0.320 | 0.720 / 0.340 |
| 1.2 m | 0.572 / 0.250 | 0.574 / 0.255 | 0.599 / 0.260 |
| 1.5 m | 0.510 / 0.235 | 0.520 / 0.250 | 0.530 / 0.270 |
| 2.0 m | 0.330 / 0.220 | 0.338 / 0.224 | 0.346 / 0.240 |

0.5 m 的 P50/P90 高度已接近画面全高，而且其 P10 低于 0.8 m 的 P10；这说明近距离数据并非严格单调。是否发生上下裁切、姿态变化或检测框波动还需查看画面确认，不能仅凭这组数字判定原因。后续记录应注明是否全身可见；被裁切的样本不宜用来进一步推定米制距离。

## 5. 距离滞回设置

先定义希望保持的安全距离区间，例如 0.9～1.3 m：

- `FAR_ENTER_HEIGHT`：明显远于安全区时的较小 height；
- `FAR_EXIT_HEIGHT`：进入安全区后停止前进的 height，必须大于 FAR_ENTER；
- `NEAR_ENTER_HEIGHT`：明显过近时的较大 height；
- `NEAR_EXIT_HEIGHT`：离开过近区的 height，必须小于 NEAR_ENTER。

必须满足：

```text
far_enter < far_exit < near_exit < near_enter
```

若在边界反复前进/停车，增大 far_enter 与 far_exit 的间隔；不要只加速度。V2.3 NEAR 永远停车，不会后退。

根据上表，首次实机标定采用：

| 配置 | 第一轮值 | 选择依据 |
|---|---:|---|
| `LONGPET_FOLLOW_FAR_ENTER_HEIGHT` | 0.53 | 1.5 m 的 P90 为 0.530；约 1.5 m 及更远可进入 FAR |
| `LONGPET_FOLLOW_FAR_EXIT_HEIGHT` | 0.57 | 低于 1.2 m 的 P10 0.572；靠近到约 1.2 m 后退出 FAR |
| `LONGPET_FOLLOW_NEAR_EXIT_HEIGHT` | 0.78 | 高于 1.0 m 的 P90 0.720，保留从 NEAR 返回 GOOD 的间隔 |
| `LONGPET_FOLLOW_NEAR_ENTER_HEIGHT` | 0.84 | 低于 0.8 m 的 P10 0.890；靠近到约 0.8 m 时进入 NEAR |

因此主要 GOOD 区域约在 1.0～1.2 m，FAR 只在头身已对齐时低速前进，NEAR 只停车。`0.53 < 0.57 < 0.78 < 0.84` 满足两侧滞回；阈值没有跨人物、姿态、光照和安装场景的保证，完整人物跟随测试后仍可微调。

特别留意：1.2 m 的 P10 `0.572` 仅比 FAR 退出阈值 `0.57` 高 `0.002`；这是一个很小的实测余量。完整运动时若在约 1.2 m 附近仍偶发前进/停车切换，应先收集新画面和日志，再调整阈值或滞回，不能把本轮数字视为已验证的稳定控制点。

## 6. 速度标定

先悬空轮组：

```text
FOLLOW_MOVE ROTATE_LEFT 10
FOLLOW_MOVE ROTATE_RIGHT 10
FOLLOW_MOVE FORWARD 12
```

命令必须约 7 Hz 刷新，否则 500 ms lease 会停车。验证方向无误后才落地。

落地时从 rotate=8、forward=10 附近开始，小步增加。若底盘克服不了静摩擦，应查电池、电机 PID 和机械阻力；不要一次把速度大幅提高。首轮上限建议 rotate <= 15、forward <= 15。

## 7. 写入配置

不要直接改主 `longpet.service`。仓库提供独立的 `deploy/longpet-follow.conf.example`，用户可在核对现有板端配置后安装为 `/etc/systemd/system/longpet.service.d/follow.conf`。关键值为：

```text
LONGPET_FOLLOW_ALIGN_ENTER_US=220
LONGPET_FOLLOW_ALIGN_EXIT_US=100
LONGPET_FOLLOW_ALIGN_ENTER_DWELL_MS=400
LONGPET_FOLLOW_ALIGN_EXIT_DWELL_MS=400
LONGPET_FOLLOW_FAR_ENTER_HEIGHT=0.53
LONGPET_FOLLOW_FAR_EXIT_HEIGHT=0.57
LONGPET_FOLLOW_NEAR_EXIT_HEIGHT=0.78
LONGPET_FOLLOW_NEAR_ENTER_HEIGHT=0.84
LONGPET_FOLLOW_FORWARD_SPEED=12
LONGPET_FOLLOW_ROTATE_SPEED=10
```

模板还包含目标稳定时间、STATUS 最大年龄、最小动作观察窗口。安装方法及验证命令见 `deploy/配置说明.md`；修改后需 `systemctl daemon-reload` 和重启服务。每轮只改一组变量，并在测试记录中保存旧值、新值、现象与结论。drop-in 仅改变参数，不会让 PERSON_FOLLOW 开机自启。

## 8. 标定完成标准

- 人物在侧面时先转身，不同时前进；
- 人物回到机身前方后才判断距离；
- 安全距离附近不连续启停；
- 过近只停车；
- 短时目标噪声不启动轮组；
- 人物离开、摄像头卡住、串口断开或家属进入远控时可靠停车；
- 连续运行 10 分钟无左右振荡、无旧命令恢复。

未达到任一项时，保持 PERSON_FOLLOW 关闭并继续标定。
