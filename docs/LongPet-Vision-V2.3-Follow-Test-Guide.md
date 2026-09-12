# LongPet Vision V2.3 Follow Test Guide

日期：2026-09-12

Codex 没有替用户烧录或操作实机。用户已报告 MCU/UART/FOLLOW 基础实机测试通过，并完成第一轮 bbox 距离采样；本文把这些结果标为“用户已验证”，不冒充 Codex 独立复测。头身自动对齐、落地前进、完整连续跟随及相关抢占场景仍待回填。后续测试地点必须空旷、远离台阶和易碎物，首次测试抬起轮组，现场保留电机硬断电手段。

## 1. 部署物

完整 V2.3 验证需要以下版本配套；基础 MCU/UART/FOLLOW 已由用户按当前固件测试通过：

1. WSL 交叉构建得到的 LongPet；
2. `D:\code\longpet-motion-mcu\xiao_che` V2.3 ESP 固件。

家属端从 `D:\code\family-desktop` 启动。主 service 不必为本次标定直接改写；第一轮距离参数使用 `deploy/longpet-follow.conf.example`，安装目标为 `/etc/systemd/system/longpet.service.d/follow.conf`，操作和核对方法见 `deploy/配置说明.md`。

## 2. 启动前检查

```sh
systemctl stop longpet.service
stty -F /dev/ttyS2 115200 raw -echo
printf 'STATUS\n' > /dev/ttyS2
timeout 2 cat /dev/ttyS2
```

必须看到 `head_offset=`。若没有，说明 ESP 仍是旧固件，PERSON_FOLLOW 会被 LongPet 拒绝。

再启动服务并看日志：

```sh
systemctl start longpet.service
journalctl -u longpet.service -f
```

## 3. 第一阶段：悬空轮组协议测试（已通过，可作回归）

用户已报告以下基础项目实机通过：MANUAL/FOLLOW/HEAD_ONLY 模式与隔离、watchdog、`TARGET_LOST` 停车、非法命令拒绝，以及 FOLLOW 运动租约不被 `TARGET` 或 `PING` 续期。`head_offset` 的实测为 CENTER = 0、LEFT 最大约 -700 us、RIGHT 最大约 +700 us。下面的步骤保留为更换固件或硬件后的回归方法，不表示自动人物跟随整体通过。

停止 LongPet，独占串口。依次验证：

```text
MODE SAFE
FOLLOW_MOVE FORWARD 10       -> 拒绝，轮不动
MODE FOLLOW
TARGET -100 0 50000          -> 只动头，不动轮
FOLLOW_MOVE ROTATE_LEFT 10   -> 左转
FOLLOW_MOVE STOP             -> 立即停车
FOLLOW_MOVE BACKWARD 10      -> 拒绝
FOLLOW_MOVE SHIFT_LEFT 10    -> 拒绝
```

### 独立 lease

1. `MODE FOLLOW`；
2. 只发一次 `FOLLOW_MOVE FORWARD 10`；
3. 持续发送 `PING`、`STATUS` 和非零 `TARGET`；
4. 预期仍在约 500 ms 后 `FOLLOW_COMMAND_TIMEOUT` 停车。

再以 7 Hz 刷新 FOLLOW_MOVE，应持续动作；停止刷新后约 500 ms 停车。

### 模式和链路

- FOLLOW 运动中切 SAFE、HEAD_ONLY、MANUAL：立即停车；
- FOLLOW 运动中拔 UART：最迟约 500 ms 停车；
- 仅 MCU 裸串口重连时：仍可报告原模式，但必须保持 STOP，绝不能恢复旧动作；
- LongPet 控制链重连时：自动跟随保持关闭，由 LongPet 明确收敛到 SAFE，必须由用户重新启用；
- `TARGET 0 0 0`：立即停车并报告 TARGET_LOST。

## 4. 第二阶段：家属端模式

1. 打开 AI 视野；
2. 确认三种模式互斥；
3. 选择“仅头部”，确认行为与 V2.2 一致且底盘不动；
4. 选择“人物跟随”，确认调试区出现 `ACQUIRING`；
5. 选择“关闭”，确认 STATUS 回到 SAFE 且底盘 STOP；
6. 再次启用人物跟随后切到“远程操控”，确认人物跟随立即关闭；
7. 退出远程操控，确认不会自动恢复；
8. 启用人物跟随后发起视频通话，确认关闭且通话后不恢复。

## 5. 第三阶段：头身对齐（仍建议悬空）

将人物置于画面左侧：

- 头部先物理向左跟随；
- `headDirection=LEFT`、`headOffsetUs<0`；
- 持续超过 enter dwell 后底盘只 `ROTATE_LEFT`；
- 期间不得出现 FORWARD。

右侧同理。人物快速从左移到右时，预期先 STOP 并重新消抖，不立即反转。回中后应先停车等待 exit dwell，再允许距离动作。

## 6. 第四阶段：距离跟随（空旷地面、最低速）

用户第一轮实测的每格数据为 normalized bbox **height / width**：

| 距离 | P10 height / width | P50 height / width | P90 height / width |
|---:|---:|---:|---:|
| 0.5 m | 0.855 / 0.475 | 0.959 / 0.505 | 0.963 / 0.520 |
| 0.8 m | 0.890 / 0.348 | 0.906 / 0.368 | 0.920 / 0.430 |
| 1.0 m | 0.710 / 0.318 | 0.713 / 0.320 | 0.720 / 0.340 |
| 1.2 m | 0.572 / 0.250 | 0.574 / 0.255 | 0.599 / 0.260 |
| 1.5 m | 0.510 / 0.235 | 0.520 / 0.250 | 0.530 / 0.270 |
| 2.0 m | 0.330 / 0.220 | 0.338 / 0.224 | 0.346 / 0.240 |

首轮阈值 `FAR_ENTER=0.53`（1.5 m P90）、`FAR_EXIT=0.57`（略低于 1.2 m P10）、`NEAR_EXIT=0.78`（高于 1.0 m P90）、`NEAR_ENTER=0.84`（低于 0.8 m P10）。目标是约 1.5 m 及更远进入 FAR、靠近约 1.2 m 退出 FAR、约 1.0～1.2 m 保持 GOOD、约 0.8 m 进入 NEAR；仍需完整跟随验证后微调。机身与人物正对时：

| 场景 | 预期 |
|---|---|
| FAR | `APPROACHING` + FORWARD |
| FAR/GOOD 边界小幅晃动 | 滞回有效，不高频启停 |
| GOOD | `HOLDING` + STOP |
| NEAR | `HOLDING` + STOP，绝不后退 |
| 人物在侧面且 FAR | `ALIGNING` + ROTATE，绝不同时前进 |

## 7. 第五阶段：失效与连续性

逐项验证：

- target lost；
- target stale；
- 摄像头画面停止更新；
- MCU fault；
- UART 断线与恢复；
- LongPet 服务重启；
- 家属端网络断开；
- 快速切换关闭/仅头部/人物跟随；
- 连续启停 20 次；
- 连续跟随 10 分钟。

每项都应停车；断线、重启、MANUAL 和视频通话结束后不得自动恢复人物跟随。目标短暂丢失后若仍保持模式，重新出现必须从 ACQUIRING 开始，不能恢复旧 DRIVE。

## 8. 日志采集

LongPet：

```sh
journalctl -u longpet.service -b --no-pager | grep -E 'AUTO_TRACK|AUTO_FOLLOW|Motion'
```

关键日志示例：

```text
AUTO_FOLLOW config align_enter_us=... far_enter_h=...
AUTO_FOLLOW distance GOOD -> FAR bbox_height=...
AUTO_FOLLOW state ALIGNING head_offset_us=-... chassis=ROTATE_LEFT
AUTO_FOLLOW state APPROACHING ... chassis=FORWARD
AUTO_FOLLOW disabled: manual override
```

MCU 保存 `[MODE]`、`[FOLLOW]`、`[STOP]`、`[FAULT]`、`[STATUS]` 全部输出，尤其是首次 fault。

## 9. 结果记录

| 项目 | PASS/FAIL | 参数/日志 | 备注 |
|---|---|---|---|
| head_offset 左/中/右语义 | 用户已验证 | CENTER 0；LEFT 约 -700 us；RIGHT 约 +700 us | 行程最大值，不是对齐阈值 |
| MANUAL/FOLLOW/HEAD_ONLY 模式隔离 | 用户已验证 | | |
| FOLLOW wrong-mode / 非法命令拒绝 | 用户已验证 | | |
| watchdog / FOLLOW 独立 lease | 用户已验证 | | PING、TARGET 均不能续 FOLLOW 租约 |
| TARGET_LOST 停车 | 用户已验证 | | 不等于全部 Vision LOST 场景已验收 |
| 左/右头身对齐 | | | |
| 对齐期间无前进 | | | |
| FAR 前进 | | | |
| GOOD 停车 | | | |
| NEAR 停车且无后退 | | | |
| Vision target lost/stale | 待测 | | 与已通过的 MCU `TARGET_LOST` 协议测试区分 |
| MANUAL 抢占且不恢复 | | | |
| 视频通话抢占且不恢复 | | | |
| UART 断线/重连不恢复 | | | |
| 连续 20 次启停 | | | |
| 连续 10 分钟 | | | |

基础 MCU/UART/FOLLOW 已通过用户实机测试，距离数据已完成首轮采样；上表其余自动跟随项目回填前，整体 V2.3 状态仍为 **full person-follow hardware validation pending user test**。
