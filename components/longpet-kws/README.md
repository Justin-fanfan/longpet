# 中文关键词唤醒

`components/longpet-kws` 是 LongPet 团队直接维护的第一方组件，可独立运行，也可由
`deploy/kws/longpet_kws_bridge.py` 启动。它不是第三方源码镜像；当前实现以公开的
`loongpet_kws` 版本为起点，并包含 LongPet 的板端采集和稳定性修正。

当前方案是 WeKWS FSMN-CTC：约 75.6 万参数，单个 ONNX 文件约 3.07 MB（权重已内嵌）。运行时只依赖 NumPy、ONNX Runtime 和 sounddevice。

```text
待机 --“小龙小龙”--> 等待一条指令（最多 10 秒） --“你好 / 陪我说话 / 救命”--> 执行后立即回到待机
```

## 本地运行

先进入本组件目录：

```powershell
Set-Location components/longpet-kws
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe run.py
```

开发板已根据当前 USB 麦克风实测设置默认参数：

```text
capture_backend = sounddevice
device = 2
capture_rate = 48000
kws_rate = 16000
vad_threshold = -60 dBFS
command_timeout = 10 s
```

因此板端正常启动只需：

```bash
python3 run.py
```

如果 USB 插拔后 PortAudio 设备编号变化，可用 `--list-devices` 重新查看，再用 `--device` 临时覆盖默认值。

输出 `WAKE: 小龙小龙` 后才进入 active 模式；随后第一个有效指令会输出 `COMMAND: ...`，并立即回到待机。每次唤醒只能执行一条指令。

查看和选择麦克风：

```powershell
.\.venv\Scripts\python.exe run.py --list-devices
.\.venv\Scripts\python.exe run.py --device 1
```

如果 ALSA 提示 `Invalid sample rate`，说明硬件不接受 16 kHz 直接采集。指定硬件常用的 48 kHz 采集，程序会用 NumPy 转成 FSMN 需要的 16 kHz：

```bash
python3 run.py --device 2 --input-samplerate 48000
```

在 LoongArch64 单核板上，`sounddevice` 采集默认运行在独立 Python 进程中，并使用 `RawInputStream/S16_LE` 阻塞读取。PortAudio 的 `blocksize=0`，由 ALSA 选择硬件原生 period；采集进程每次读取 100 ms PCM。重采样、VAD 和 ONNX 均在主进程执行。队列最多保留 4 个音频块（约 400 ms），满时丢弃最旧数据。

`arecord` 仅作为 ALSA/PortAudio 故障排查时的备用后端：

```bash
python3 run.py \
  --capture-backend arecord \
  --alsa-device plughw:1,0 \
  --input-samplerate 48000
```

`--alsa-device` 应以开发板实际检测到的 Card ID 为准。

默认独立阈值为：小龙小龙 `0.15`、你好 `0.10`、陪我说话 `0.05`、救命 `0.05`。可分别调整：

```powershell
.\.venv\Scripts\python.exe run.py `
  --wake-threshold 0.15 `
  --nihao-threshold 0.10 `
  --peiwoshuohua-threshold 0.05 `
  --jiuming-threshold 0.05
```

某个词误触发多就只提高它的阈值；漏检多则降低。`--threshold 0.08` 仍可一次把四个阈值全部覆盖为 `0.08`。

## VAD

默认启用纯 NumPy 自适应能量 VAD：门限为 `-60 dBFS`，保留 300 ms 语音前缀，连续静音 500 ms 后结束一句并重置 FSMN 流式缓存。查看 VAD 开始/结束日志：

```powershell
.\.venv\Scripts\python.exe run.py --vad-debug
```

如果说话时始终没有 `VAD: speech start`，先查看麦克风实际输入电平：

```bash
python3 run.py --device 2 --input-samplerate 48000 --audio-debug --vad-debug
```

`AUDIO` 每秒输出一次 RMS/Peak dBFS。说话时 RMS 应明显高于安静时；如果始终接近 `-120 dBFS`，表示录音是全零或输入被静音。

安静说话时不容易开启 VAD，可将绝对门限从默认 `-60 dBFS` 降低：

```powershell
.\.venv\Scripts\python.exe run.py --vad-threshold-db -65
```

环境噪声容易开启 VAD，可提高至 `-55 dBFS`，或增大 `--vad-noise-ratio`。对照无 VAD 效果可使用 `--no-vad`。

## 查看关键词分数

调整阈值时，可持续输出当前这句话中四个关键词的最高分：

```powershell
.\.venv\Scripts\python.exe run.py --show-scores --vad-debug --threshold 1
```

未完整拼出的关键词显示为 `0.0`，VAD 判定语音结束后四个分数清零。该选项仅用于调试，部署时建议关闭以减少终端 I/O。

## 目录结构

```text
components/longpet-kws/
├─ run.py                   # 麦克风运行入口
├─ src/longpet_kws/
│  ├─ cli.py               # 检测器、状态机与录音/ALSA采集
│  ├─ fbank.py             # 纯 NumPy Kaldi FBank
│  └─ vad.py               # 自适应能量 VAD
├─ assets/fsmn/            # ONNX 模型和中文词表
├─ requirements.txt
└─ README.md
```

`run.py` 和 `src/longpet_kws/cli.py` 都基于自身 `__file__` 定位组件根目录，所以从仓库根或组件
目录启动时，默认模型与词表都指向同一份 `assets/fsmn`，不依赖当前工作目录。

## 来源、版本与许可证状态

- 起点仓库：<https://github.com/ycxuan0517/loongpet_kws>
- 固定起点提交：`d349994161b7a2f43e30078a605033b1e2facc25`（另见 `UPSTREAM_COMMIT`）
- `fsmn_ctc.onnx`：3,065,258 bytes；SHA-256
  `6febd9f7f15c47caed88d434d810651e34215334c66961b9ba66251fa04d98c4`
- `tokens.txt`：SHA-256
  `41b2c566f0d16ed6a0913d7770520c16556a4bcca206e8012676f847be4b0980`
- 上述起点仓库在该提交没有提供顶层 `LICENSE`，也没有记录声学模型更上游的精确版本与许可文件。

因此这些模型文件是为保持现有 LongPet KWS 可复现性而保留的必要 runtime 资产，但不能据此
推断其具备对外再分发授权。公开发布前必须补齐 WeKWS/模型原始来源和许可证审查；在此之前，
不要把“可运行”写成“已完成许可证确认”。
