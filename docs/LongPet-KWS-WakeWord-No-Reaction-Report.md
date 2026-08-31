# LongPet KWS 唤醒词无反应 —— 排查与修复报告

日期：2026-08-31  
目标平台：LongPet / LoongArch64 / Qt 6  
板端主机：`LS-GD`（10.240.178.51），服务以 `longpet` 用户运行  
配置文件：`/etc/longpet/ai.ini`（`[kws]` 段）
关键字：KWS / arecord / 唤醒词 / 关键词得分 / `bufsize=0`

## 1. 结论

说完“小龙小龙”无任何反应，**根因**是仓内 vendored 的上游采集类
`third_party/longpet-kws/src/longpet_kws/cli.py` 的 `ArecordCapture` 用
`subprocess.Popen(..., bufsize=0)` 启动 `arecord`，导致 `p.stdout` 是**无缓冲 FileIO**，
`read(block_bytes)` 只做一次 `os.read`，管道尚未攒满就返回**部分帧**；随后 `_read()`
里 `if len(raw) < block_bytes: return` 把这个部分读误判为 EOF，**采集器只送出第一个
100 ms 音频块就退出**。结果 KWS `ready` 但几乎采不到音频，关键词得分恒为 0，唤醒词永不触发。

修复：去掉 `bufsize=0`（改用默认缓冲流，`read(n)` 会阻塞到凑满 `n` 字节或 EOF）。

已在**板端实机验证**：部署服务重启后日志出现
`KWS keyword=小龙小龙 score=0.2994` → `Voice interaction session=1 event=start` →
`vad_speech_detected`。说“小龙小龙”现在能正常唤醒 AI。

## 2. 问题现象

板端部署的 KWS（`capture_backend=arecord` + `alsa_device=plughw:CARD=Device,DEV=0`）：

- KWS 进程正常 `ready`，但说“小龙小龙”从未触发关键词事件；
- **ASR（语音交互）完全正常**（GStreamer 采集），已能完成 ASR→LLM→TTS→播放；
- 麦克风被 `arecord` 占用：`arecord -D …` 直接采集报 “Device or resource busy”；
- KWS 日志只有 `ready`，此后**没有任何 keyword / WAKE / COMMAND 事件**。

“ASR 正常、KWS 没反应、且麦一直 busy”这三个信号组合，天然指向 KWS 自己的采集与
ASR 的采集是**两条**不同的路径——果然，问题只出在 KWS 的 `arecord` 子进程读取侧。

## 3. 排查过程

### 3.1 先排除 / 确认麦克风与采集通路

| 检查 | 结果 |
|---|---|
| 直接 `arecord -D … -d 5 >file` | 抓到 **480000 字节**（=5 秒整），stderr 为空 —— **arecord 本身正常** |
| `amixer -c 1 sget 'Mic'` | 实控名是 `'Mic'`（非 `'Mic Capture Volume'`）；Capture 20/35=+8 dB，上例 35=+23 dB |
| 拉满 +23 dB 后测 | 说话时 peak=**0 dBFS（削顶）**，波形被切坏，KWS 崩掉 |
| 增益调回 +14 dB 再测 | 说话时 peak≈-10 dBFS，不削顶 |

结论：不是麦克风坏了，也不是 arecord 坏，是**消费端的读取逻辑**在丢数据。

### 3.2 定位到 python 读取侧

写了一段与 `ArecordCapture` 逐字节等价的最小复现脚本，结果非常明确：

```text
block 1 bytes=9600 at 0.142s
SHORT_READ len=2400 at 0.14s, poll=None
TOTAL blocks=1 in 0.14s
```

即：第 1 块成功读满 `block_bytes`（9600 = 100 ms），第 2 次 `read(9600)` 只拿到
**2400 字节**，`_read()` 立刻认为“不到一整块=EOF”，于是整个采集线程返回，只送出 1 个块。

对照：`arecord … -t raw - | wc -c` 在 3 秒写满 **384000 字节**——说明 arecord 写到 pipe
没问题，问题在 python 这边 `Popen` 的缓冲设置。

### 3.3 根因定位（`bufsize=0`）

`subprocess.Popen(..., bufsize=0)` 使子进程 stdout 成为**无缓冲 `FileIO`**：
`read(block_bytes)` 调用底层 `os.read`，**只返回当前管道里已经写进来的字节数**，不保证凑满
`block_bytes`。于是只要 `arecord` 写入节奏稍有抖动，就会出现一次性返回 `2400`（部分）字节。
而 `_read()`：

```python
def _read(self):
    while True:
        raw = self.process.stdout.read(self.block_bytes)
        if len(raw) < self.block_bytes:   # 把“部分读”当成 EOF
            return
        _put_latest(self.audio_queue, raw)
```

**把抖动产生的部分读误判成流结束**，采集器第一轮后即 `return`。KWS 因此只“采过一瞬”，
后续时间全部空转 —— 关键词得分恒 0，唤醒词自然没反应。

> 注意：`arecord` 子进程本身仍在运行（poll=None），所以这个子进程一直占着 USB 麦克风，
> 造成“设备 busy”的假象；而 python 侧已经不再吞任何数据。这解释了为何 ASR 正常、麦却 busy。

## 4. 修复方案

只改一处：去掉 `bufsize=0`。

```python
# 修复前
self.process = subprocess.Popen(
    ["arecord", "-q", "-D", self.device, "-f", "S16_LE",
     "-r", str(self.sample_rate), "-c", "1", "-t", "raw", "-"],
    stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL,
    bufsize=0,
)

# 修复后
self.process = subprocess.Popen(
    ["arecord", "-q", "-D", self.device, "-f", "S16_LE",
     "-r", str(self.sample_rate), "-c", "1", "-t", "raw", "-"],
    stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL,
    # 不能用 0：无缓冲 stdout 会让 read(n) 只做一次 os.read，在管道未满时
    # 返回部分帧，_read() 误判为 EOF，采集器只出一个块就停。用默认缓冲流，
    # read(n) 会阻塞到凑满 n 字节或 EOF。
)
```

默认缓冲流（`bufsize=-1`）下 `read(9600)` 会在内部循环调用 `os.read`，直到**凑满 9600 字节**
或遇到真正的 EOF，因此不再出现部分读。

**修改落点**（仓库与板端必须同步）：

- 仓库：`third_party/longpet-kws/src/longpet_kws/cli.py`
- 板端：`/home/longpet/longpet-kws/upstream/src/longpet_kws/cli.py`
  （板端 bridge `longpet_kws_bridge.py` 通过 `sys.path.insert(0, kws_root/"src")` 从该文件
  `from longpet_kws.cli import ArecordCapture`，因此只需改这一处。）

> 该采集 bug 只影响 `capture_backend=arecord` 分支；`sounddevice` 分支走
> `sounddevice_capture_worker`，不受影响。

## 5. 验证结果

### 5.1 采集连续性（修复前后对照）

| 场景 | 分数行数（约） | 说明 |
|---|---|---|
| 修复前，5/60 秒 | **1 行** | 只出了一个块，之后空转 |
| 修复后，5 秒 | **49 行** | ~10 块/秒，连续 |
| 修复后，20 秒 | **199 行** | 连续采集 |
| 修复后，25 秒 | **249 行** | 连续采集 |

### 5.2 唤醒词得分（`run.py` 直接测试，+14 dB，无 VAD）

```json
{"keyword": "小龙小龙", "score": 0.6565}
{"keyword": "小龙小龙", "score": 0.2854}
{"keyword": "小龙小龙", "score": 0.3112}
```

- 最高 **0.6565**，远超默认阈值 0.15 → **触发**；
- 峰值 -10.5 dBFS，**未削顶**（修复前 +23 dB 处是 0 dBFS 削顶）；
- 同时 `你好 / 陪我说话 / 救命` 得分均为 **0.0**，只有说唤醒词时才触发，很干净。

### 5.3 部署服务实机（重启后）

```text
23:28:58  KWS keyword=小龙小龙 score=0.2994        ← 部署 KWS 检出唤醒词（>0.15）
23:28:59  Voice interaction session=1 event=start  ← AI 语音交互启动
23:28:59  Voice interaction session=1 event=vad_speech_detected
```

此前同一服务只有 `KWS ready` 后再无 keyword 事件；修复后唤醒链路完整。

## 6. 涉及文件

### 6.1 本次核心修改

- `third_party/longpet-kws/src/longpet_kws/cli.py` —— `ArecordCapture.start()` 去掉 `bufsize=0`（唯一必改项）。

### 6.2 同批次相关（协助定位 / 同仓库未提交）

- `src/platform/KwsProcessAdapter.cpp` —— 启动时打印 `KWS resolved config:` 诊断日志，
  便于核对 `alsa_device` / `capture_backend` 实际解析值（本次排查关键依据之一）。
- `src/data/AiConfigRepository.cpp` —— `alsa_device` 改为 env 继承
  `LONGPET_KWS_ALSA_DEVICE → LONGPET_AI_CAPTURE_DEVICE → ini`，解决板端此前误报
  “KWS 使用 arecord 时必须配置 alsa_device” 的问题。
- `deploy/longpet-ai.ini.example`、`deploy/longpet-ai-mixed.ini.example`、
  `deploy/配置说明.md` —— 说明 env 继承顺序与采集设备配置。
- `third_party/longpet-kws/**` —— 首次 vendored 进仓库的上游源码与模型
  （含本次修复的 `cli.py`）。此目录此前尚未提交。

### 6.3 板端同步位置

- `/home/longpet/longpet-kws/upstream/src/longpet_kws/cli.py`（已打补丁，备份 `cli.py.bak`）
- `/home/longpet/longpet-kws/longpet_kws_bridge.py`（上层调用方，无需改）

## 7. 附带发现与建议

1. **麦克风增益**：实控 ALSA 控制名是 `'Mic'`（卡 1），不是 `'Mic Capture Volume'`。
   数值映射近似 `dB = value - 12`（0→-12 dB，20→+8 dB，35→+23 dB）。
   **+23 dB 会削顶**（说话时 peak≈0 dBFS），+14 dB 时说话峰值约 -10 dBFS、最稳。
   本次已把板端增益暂调到 **+14 dB（value 26）**。
   - 注意：该增益是**硬件 mixer 状态**，不在 ai.ini / 服务里持久化；板子重启或 USB 麦重新枚举
     后可能回到更安静的默认值。若需固化，可考虑在部署时额外设置（如 udev / systemd 启动脚本），
     当前版本未内置。

2. **阈值**：默认 `wake_threshold=0.15`。实测触发得分 0.29（部署）~ 0.66（近讲）。若远场偏弱，
   可先调增益，仍偏弱再考虑适度下调阈值（如 0.10），但要权衡误唤醒率。

3. **上游同步**：`ArecordCapture` 的 `bufsize=0` 是 vendored 上游 `loongpet_kws` 自带
   的写法。修复只在 LongPet 的 `third_party/**/cli.py` 内；若将来升级上游，需再次确认该处
   （建议向上游反馈：`bufsize=0` + 按 `block_bytes` 读取的组合在管道未满时有丢帧风险）。

## 8. 复现 / 回归步骤

### 8.1 复现修复前 bug

板端（停止 `longpet.service`，释放麦克风后）：

```bash
cd /home/longpet/longpet-kws/upstream
python3 -u run.py --no-vad --audio-debug --show-scores \
  --capture-backend arecord --alsa-device plughw:CARD=Device,DEV=0 \
  --input-samplerate 48000 --duration 8
```

修复前：`--duration 8` 只打出 **1 条** `AUDIO` 和 **1 条** `{"scores": {...}}`。

### 8.2 验证修复后

同样的命令，修复后应打出 **~8 条 `AUDIO` 和 ~80 条 `scores`**（~10 块/秒）。

### 8.3 触发唤醒词

```bash
setsid bash -c 'python3 -u run.py --no-vad --audio-debug --show-scores \
  --capture-backend arecord --alsa-device plughw:CARD=Device,DEV=0 \
  --input-samplerate 48000 --duration 25 > /tmp/kws_scores.log 2>&1' </dev/null &
```

期间念“小龙小龙”，随后：

```bash
grep -E '"(keyword|WAKE|COMMAND)"' /tmp/kws_scores.log
grep -E 'AUDIO' /tmp/kws_scores.log | tail   # 看 peak 是否随语音 -30→-10 dBFS
```

预期出现 `{"keyword": "小龙小龙", "score": 0.6+}` 或 `WAKE: 小龙小龙`。

### 8.4 部署回归

```bash
systemctl restart longpet.service
journalctl -fu longpet.service   # 看 KWS ready → 说“小龙小龙” → keyword → session=start
```

## 9. 待办 / 未覆盖

- 该项目所有改动（`cli.py`、`AiConfigRepository.cpp`、诊断日志、ini 示例、`third_party/longpet-kws/**`）
  **尚未 `git commit`**。
- 尚未在原上游仓库复刻/验证该修复；本报告只保证 LongPet 方案内一致。
- 未做长时间误唤醒率、远场识别率的板端统计。
