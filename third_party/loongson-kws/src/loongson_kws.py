#!/usr/bin/env python3

import argparse
import ctypes
import json
import multiprocessing as mp
import os
import queue
import select
import signal
import subprocess
import sys
import time
import wave
from collections import deque
from datetime import datetime
from pathlib import Path

try:
    import numpy as np
except ModuleNotFoundError as error:
    print(
        json.dumps(
            {
                "event": "runtime_status",
                "state": "error",
                "detail": f"关键词识别依赖缺失：{error}",
                "recoverable": False,
            },
            ensure_ascii=False,
        ),
        flush=True,
    )
    raise SystemExit(2)


PROJECT_ROOT = Path(__file__).resolve().parents[1]
MODEL_DIR = (
    PROJECT_ROOT
    / "models"
    / "sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01"
)
KEYWORDS_FILE = PROJECT_ROOT / "config" / "keywords.txt"

MODEL_SAMPLE_RATE = 16_000
DEFAULT_DEVICE_ID = 0

# 100 ms is short enough for VAD and is accepted reliably by ES8388.
FRAME_SECONDS = 0.10

CALIBRATION_SECONDS = 1.5
PRE_ROLL_SECONDS = 0.45
END_SILENCE_SECONDS = 0.65
MIN_UTTERANCE_SECONDS = 0.35
MAX_UTTERANCE_SECONDS = 5.5

KEYWORD_SIGNALS = {
    "你好": {"signal": "GREETING", "code": 1},
    "救命": {"signal": "EMERGENCY", "code": 2},
    "停止": {"signal": "STOP", "code": 3},
    "知道了": {"signal": "ACKNOWLEDGE", "code": 4},
    "好的": {"signal": "ACKNOWLEDGE", "code": 4},
    "完成了": {"signal": "COMPLETE", "code": 5},
    "吃过了": {"signal": "COMPLETE", "code": 5},
}

SHUTDOWN_REQUESTED = False


def log(message):
    print(message, file=sys.stderr, flush=True)


def emit_signal(keyword, source="microphone"):
    keyword = keyword.strip()
    signal = KEYWORD_SIGNALS.get(
        keyword, {"signal": "KEYWORD_DETECTED", "code": 0}
    )
    event = {
        "event": "keyword_detected",
        "keyword": keyword,
        **signal,
        "source": source,
        "timestamp": datetime.now().astimezone().isoformat(timespec="milliseconds"),
    }
    print(json.dumps(event, ensure_ascii=False), flush=True)
    return event


def emit_runtime_status(state, detail="", recoverable=True):
    event = {
        "event": "runtime_status",
        "state": state,
        "detail": detail,
        "recoverable": recoverable,
        "timestamp": datetime.now().astimezone().isoformat(timespec="milliseconds"),
    }
    print(json.dumps(event, ensure_ascii=False), flush=True)
    return event


def request_shutdown(signum, frame):
    del signum, frame
    global SHUTDOWN_REQUESTED
    SHUTDOWN_REQUESTED = True


def arm_parent_death_signal():
    """Terminate descendants if their direct parent disappears (Linux)."""
    if not sys.platform.startswith("linux"):
        return
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        libc.prctl(1, signal.SIGTERM)
        if os.getppid() == 1:
            os.kill(os.getpid(), signal.SIGTERM)
    except (AttributeError, OSError):
        pass


def model_path(component, int8=True):
    suffix = ".int8.onnx" if int8 else ".onnx"
    return MODEL_DIR / f"{component}-epoch-12-avg-2-chunk-16-left-64{suffix}"


def check_required_files(keywords_file, int8=True):
    required = [
        model_path("encoder", int8),
        model_path("decoder", int8),
        model_path("joiner", int8),
        MODEL_DIR / "tokens.txt",
        keywords_file,
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError("缺少文件：\n" + "\n".join(missing))


def keyword_names(path):
    names = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if "@" in line:
            names.append(line.rsplit("@", 1)[1].strip())
    return names


def create_kws(keywords_file, int8, threshold, score):
    import sherpa_onnx

    check_required_files(keywords_file, int8)
    model_name = "INT8" if int8 else "FP32"
    log(f"正在加载 {model_name} KWS 模型……")
    started = time.monotonic()
    kws = sherpa_onnx.KeywordSpotter(
        encoder=str(model_path("encoder", int8)),
        decoder=str(model_path("decoder", int8)),
        joiner=str(model_path("joiner", int8)),
        tokens=str(MODEL_DIR / "tokens.txt"),
        keywords_file=str(keywords_file),
        num_threads=1,
        sample_rate=MODEL_SAMPLE_RATE,
        keywords_score=score,
        keywords_threshold=threshold,
    )
    log(f"KWS 模型加载完成，耗时 {time.monotonic() - started:.2f} 秒。")
    return kws


def configure_es8388():
    settings = [
        ("Capture Digital", "192,192"),
        ("Mic PGA", "8,8"),
        ("Left PGA Mux", "Line 1"),
        ("Right PGA Mux", "Line 1"),
    ]
    log("正在配置 ES8388 麦克风增益……")
    for control, value in settings:
        result = subprocess.run(
            ["amixer", "-c", "0", "sset", control, value],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or f"退出码 {result.returncode}"
            log(f"警告：无法设置 {control}: {detail}")


def safe_status(status_queue, message):
    try:
        status_queue.put_nowait(message)
    except queue.Full:
        pass


def put_latest(target_queue, value):
    try:
        target_queue.put_nowait(value)
        return False
    except queue.Full:
        try:
            target_queue.get_nowait()
        except queue.Empty:
            pass
        try:
            target_queue.put_nowait(value)
        except queue.Full:
            return True
        return True


def start_arecord(audio_device, sample_rate, channels):
    command = [
        "arecord",
        "-M",
        "-D",
        audio_device,
        "-t",
        "raw",
        "-f",
        "S16_LE",
        "-c",
        str(channels),
        "-r",
        str(sample_rate),
        "-q",
    ]
    recorder = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0,
        preexec_fn=arm_parent_death_signal if sys.platform.startswith("linux") else None,
    )
    if recorder.stdout is None:
        recorder.terminate()
        raise RuntimeError("无法打开 arecord 输出管道")
    return recorder


def stop_arecord(recorder):
    if recorder.poll() is not None:
        return
    recorder.terminate()
    try:
        recorder.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        recorder.kill()
        recorder.wait()


def read_arecord_frame(recorder, frame_bytes, timeout=1.5):
    descriptor = recorder.stdout.fileno()
    data = bytearray()
    deadline = time.monotonic() + timeout
    while len(data) < frame_bytes:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        readable, _, _ = select.select([descriptor], [], [], remaining)
        if not readable:
            return None
        chunk = os.read(descriptor, frame_bytes - len(data))
        if not chunk:
            return b""
        data.extend(chunk)
    return bytes(data)


def capture_worker(
    audio_device, sample_rate, channels, microphone_channel,
    utterance_queue, status_queue, stop_event, debug
):
    arm_parent_death_signal()
    # The recorder must remain schedulable while ONNX saturates the only CPU core.
    try:
        os.nice(-5)
    except (AttributeError, OSError):
        pass

    calibration_frames = max(1, round(CALIBRATION_SECONDS / FRAME_SECONDS))
    pre_roll_frames = max(1, round(PRE_ROLL_SECONDS / FRAME_SECONDS))
    end_silence_frames = max(1, round(END_SILENCE_SECONDS / FRAME_SECONDS))
    min_frames = max(1, round(MIN_UTTERANCE_SECONDS / FRAME_SECONDS))
    max_frames = max(1, round(MAX_UTTERANCE_SECONDS / FRAME_SECONDS))

    calibration = []
    pre_roll = deque(maxlen=pre_roll_frames)
    noise_floor = 0.005
    in_speech = False
    utterance = []
    silence_frames = 0
    onset_frames = 0
    sent = 0
    dropped_utterances = 0
    last_level = time.monotonic()
    frame_count = 0
    restarts = 0
    recorder = None
    speech_started_at = 0.0

    try:
        recorder = start_arecord(audio_device, sample_rate, channels)
        safe_status(status_queue, ("stream_started", audio_device))
        capture_frames = int(sample_rate * FRAME_SECONDS)
        frame_bytes = capture_frames * channels * 2

        while not stop_event.is_set():
                raw = read_arecord_frame(recorder, frame_bytes)
                if raw is None or raw == b"":
                    exit_code = recorder.poll()
                    safe_status(
                        status_queue,
                        ("recorder_restart", restarts + 1, exit_code),
                    )
                    stop_arecord(recorder)
                    if stop_event.is_set():
                        break
                    time.sleep(0.2)
                    recorder = start_arecord(audio_device, sample_rate, channels)
                    restarts += 1
                    if restarts > 5:
                        raise RuntimeError("arecord 连续 5 次停止输出")
                    continue

                pcm = np.frombuffer(raw, dtype="<i2").reshape(-1, channels)
                samples = np.asarray(
                    pcm[:, microphone_channel], dtype=np.float32
                ) / 32768.0
                # ES8388 may carry a small DC offset; KWS and VAD need AC speech.
                samples -= float(np.mean(samples))
                samples = np.ascontiguousarray(samples, dtype=np.float32)
                frame_count += 1

                rms = float(np.sqrt(np.mean(samples * samples)))
                peak = float(np.max(np.abs(samples)))

                if len(calibration) < calibration_frames:
                    calibration.append(rms)
                    pre_roll.append(samples)
                    if len(calibration) == calibration_frames:
                        ordered = sorted(calibration)
                        quiet_count = max(1, int(len(ordered) * 0.7))
                        noise_floor = max(0.0005, float(np.median(ordered[:quiet_count])))
                        safe_status(status_queue, ("calibrated", noise_floor))
                    continue

                start_rms = max(0.0030, noise_floor * 2.5)
                start_peak = max(0.0180, noise_floor * 8.0)
                keep_rms = max(0.0020, noise_floor * 1.5)
                keep_peak = max(0.0120, noise_floor * 5.0)
                is_start = rms >= start_rms or peak >= start_peak
                is_active = rms >= keep_rms or peak >= keep_peak

                if not in_speech:
                    pre_roll.append(samples)
                    if is_start:
                        onset_frames += 1
                    else:
                        onset_frames = 0
                        # Adapt only while the signal still resembles background.
                        if rms < start_rms:
                            noise_floor = 0.995 * noise_floor + 0.005 * rms

                    if onset_frames >= 2:
                        in_speech = True
                        speech_started_at = time.monotonic()
                        utterance = list(pre_roll)
                        silence_frames = 0
                        onset_frames = 0
                        safe_status(status_queue, ("speech_started", rms, peak))
                else:
                    utterance.append(samples)
                    if is_active:
                        silence_frames = 0
                    else:
                        silence_frames += 1

                    should_finish = (
                        silence_frames >= end_silence_frames
                        or len(utterance) >= max_frames
                    )
                    if should_finish:
                        if len(utterance) >= min_frames:
                            complete = np.ascontiguousarray(
                                np.concatenate(utterance), dtype=np.float32
                            )
                            packet = (
                                complete,
                                speech_started_at,
                                time.monotonic(),
                                dropped_utterances,
                            )
                            if put_latest(utterance_queue, packet):
                                dropped_utterances += 1
                            sent += 1
                            safe_status(
                                status_queue,
                                (
                                    "utterance",
                                    sent,
                                    len(complete) / sample_rate,
                                    float(np.max(np.abs(complete))),
                                    dropped_utterances,
                                ),
                            )
                        in_speech = False
                        utterance = []
                        silence_frames = 0
                        pre_roll.clear()

                now = time.monotonic()
                if now - last_level >= 0.2:
                    last_level = now
                    safe_status(
                        status_queue,
                        (
                            "level",
                            rms,
                            peak,
                            noise_floor,
                            start_rms,
                            frame_count,
                            restarts,
                            dropped_utterances,
                        ),
                    )
    except BaseException as exc:
        safe_status(
            status_queue,
            ("capture_error", repr(exc), isinstance(exc, FileNotFoundError)),
        )
        stop_event.set()
    finally:
        if recorder is not None:
            stop_arecord(recorder)
        safe_status(
            status_queue,
            (
                "capture_stopped",
                frame_count,
                restarts,
                0,
            ),
        )


def load_wave(path):
    with wave.open(str(path), "rb") as source:
        rate = source.getframerate()
        channels = source.getnchannels()
        width = source.getsampwidth()
        frames = source.readframes(source.getnframes())
    if width != 2:
        raise ValueError(f"仅支持 16-bit PCM WAV，当前 sampwidth={width}")
    samples = np.frombuffer(frames, dtype="<i2")
    if channels > 1:
        samples = samples.reshape(-1, channels)[:, 0]
    return rate, np.asarray(samples, dtype=np.float32) / 32768.0


def prepare_microphone_samples(samples):
    samples = np.asarray(samples, dtype=np.float32).copy()
    samples -= float(np.mean(samples))
    peak = float(np.max(np.abs(samples))) if len(samples) else 0.0
    # Bring quiet ES8388 speech closer to the training-wave amplitude without clipping.
    gain = min(8.0, 0.45 / max(peak, 1e-6))
    gain = max(1.0, gain)
    samples *= gain
    np.clip(samples, -1.0, 1.0, out=samples)
    return np.ascontiguousarray(samples), gain, peak


def decode_samples(kws, sample_rate, samples, add_silence=True):
    stream = kws.create_stream()
    samples = np.ascontiguousarray(samples, dtype=np.float32)
    if add_silence:
        trailing = np.zeros(int(sample_rate * 0.8), dtype=np.float32)
        samples = np.concatenate((samples, trailing))
    started = time.monotonic()
    stream.accept_waveform(sample_rate, samples)
    stream.input_finished()
    result = ""
    decodes = 0
    while kws.is_ready(stream):
        kws.decode_stream(stream)
        decodes += 1
        current = kws.get_result(stream).strip()
        if current:
            result = current
            break
    elapsed = time.monotonic() - started
    return result, decodes, elapsed, len(samples) / sample_rate


def drain_status(status_queue, debug=False):
    while True:
        try:
            message = status_queue.get_nowait()
        except queue.Empty:
            return
        kind = message[0]
        if kind == "stream_started":
            log(f"ES8388 独立采集已启动：arecord {message[1]}")
        elif kind == "calibrated":
            log(f"环境底噪校准完成：RMS={message[1]:.5f}，现在可以说关键词。")
            emit_runtime_status("listening", "离线关键词 · 正在监听")
        elif kind == "speech_started" and debug:
            log(f"[VAD] 检测到语音：rms={message[1]:.5f} peak={message[2]:.5f}")
        elif kind == "utterance":
            log(
                f"[VAD] 语句 #{message[1]}：{message[2]:.2f}s "
                f"peak={message[3]:.4f} 丢弃旧语句={message[4]}"
            )
        elif kind == "level":
            print(
                json.dumps(
                    {
                        "event": "audio_level",
                        "rms": round(message[1], 6),
                        "peak": round(message[2], 6),
                        "noise_floor": round(message[3], 6),
                        "dropped_utterances": message[7],
                    },
                    ensure_ascii=False,
                    separators=(",", ":"),
                ),
                flush=True,
            )
            if debug:
                log(
                    f"[AUDIO] rms={message[1]:.5f} peak={message[2]:.5f} "
                    f"noise={message[3]:.5f} trigger={message[4]:.5f}"
                )
        elif kind == "audio_status":
            log(f"[AUDIO] PortAudio 状态：{message[1]}")
        elif kind == "recorder_restart":
            log(
                f"[AUDIO] arecord 无新数据，正在自恢复："
                f"第 {message[1]} 次，退出码={message[2]}"
            )
        elif kind == "capture_error":
            if len(message) > 2 and message[2]:
                raise FileNotFoundError(f"录音依赖缺失：{message[1]}")
            raise RuntimeError(f"录音进程异常：{message[1]}")
        elif kind == "capture_stopped" and debug:
            log(
                f"[AUDIO] 采集停止：callbacks={message[1]} "
                f"dropped={message[2]} status={message[3]}"
            )


def run_wave(args, wav_path, keywords_file, source):
    kws = create_kws(keywords_file, not args.fp32, args.threshold, args.score)
    rate, samples = load_wave(wav_path)
    result, decodes, elapsed, duration = decode_samples(kws, rate, samples)
    log(
        f"WAV 解码完成：audio={duration:.2f}s decodes={decodes} "
        f"elapsed={elapsed:.2f}s RTF={elapsed / duration:.2f} result={result!r}"
    )
    if result:
        emit_signal(result, source=source)
        return 0
    log("未检测到关键词。")
    return 2


def listen(args):
    configure_es8388()
    kws = create_kws(
        args.keywords, not args.fp32, args.threshold, args.score
    )

    context = mp.get_context("spawn")
    utterance_queue = context.Queue(maxsize=3)
    status_queue = context.Queue(maxsize=64)
    stop_event = context.Event()
    recorder = context.Process(
        target=capture_worker,
        args=(
            args.alsa_device,
            args.sample_rate,
            args.channels,
            args.mic_channel,
            utterance_queue,
            status_queue,
            stop_event,
            args.debug,
        ),
        daemon=True,
        name="es8388-capture",
    )
    recorder.start()

    names = " / ".join(keyword_names(args.keywords))
    log("")
    log("========================================")
    log("关键词识别已启动")
    log(f"关键词：{names}")
    log(
        f"录音：arecord {args.alsa_device} / {args.sample_rate} Hz / "
        f"{args.channels} ch / channel {args.mic_channel}"
    )
    log("采集与推理已分离；推理期间麦克风仍持续工作。")
    log("按 Ctrl+C 退出。")
    log("========================================")

    started = time.monotonic()
    processed = 0
    try:
        while not stop_event.is_set() and not SHUTDOWN_REQUESTED:
            drain_status(status_queue, args.debug)
            if args.duration and time.monotonic() - started >= args.duration:
                log(f"已达到测试时长 {args.duration:.1f} 秒。")
                break
            if not recorder.is_alive():
                drain_status(status_queue, args.debug)
                raise RuntimeError(f"录音进程意外退出，exitcode={recorder.exitcode}")
            try:
                utterance, speech_started_at, captured_at, dropped_utterances = (
                    utterance_queue.get(timeout=0.2)
                )
            except queue.Empty:
                continue

            processed += 1
            prepared, gain, raw_peak = prepare_microphone_samples(utterance)
            log(
                f"[KWS] 开始处理语句 #{processed}："
                f"{len(prepared) / args.sample_rate:.2f}s "
                f"raw_peak={raw_peak:.4f} gain=x{gain:.2f}"
            )
            result, decodes, elapsed, duration = decode_samples(
                kws, args.sample_rate, prepared
            )
            log(
                f"[KWS] 完成语句 #{processed}：decodes={decodes} "
                f"elapsed={elapsed:.2f}s RTF={elapsed / duration:.2f} "
                f"result={result!r}"
            )
            if result:
                emit_signal(result)
            keyword_latency_ms = (
                (time.monotonic() - speech_started_at) * 1000.0 if result else 0.0
            )
            print(
                json.dumps(
                    {
                        "event": "decode_metrics",
                        "elapsed_ms": round(elapsed * 1000.0, 2),
                        "rtf": round(elapsed / max(duration, 1e-6), 3),
                        "keyword_latency_ms": round(keyword_latency_ms, 2),
                        "capture_to_decode_ms": round(
                            max(0.0, time.monotonic() - captured_at) * 1000.0, 2
                        ),
                        "dropped_utterances": dropped_utterances,
                    },
                    separators=(",", ":"),
                ),
                flush=True,
            )
            drain_status(status_queue, args.debug)
    finally:
        stop_event.set()
        recorder.join(timeout=3.0)
        if recorder.is_alive():
            recorder.terminate()
            recorder.join(timeout=2.0)
        utterance_queue.cancel_join_thread()
        status_queue.cancel_join_thread()
        log("关键词识别已停止。")


def list_devices():
    import sounddevice as sd

    print(sd.query_devices())


def parse_args():
    parser = argparse.ArgumentParser(
        description="Loongson 2K0300 + ES8388 关键词识别"
    )
    parser.add_argument("--device", type=int, default=DEFAULT_DEVICE_ID)
    parser.add_argument("--alsa-device", default="hw:0,0")
    parser.add_argument("--sample-rate", type=int, default=44_100)
    parser.add_argument("--channels", type=int, default=2)
    parser.add_argument("--mic-channel", type=int, default=0)
    parser.add_argument("--keywords", type=Path, default=KEYWORDS_FILE)
    parser.add_argument("--threshold", type=float, default=0.20)
    parser.add_argument("--score", type=float, default=1.5)
    parser.add_argument("--fp32", action="store_true")
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--duration", type=float, default=0.0)
    parser.add_argument("--wav", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--list-devices", action="store_true")
    return parser.parse_args()


def main():
    signal.signal(signal.SIGTERM, request_shutdown)
    signal.signal(signal.SIGINT, request_shutdown)
    args = parse_args()
    if not 8_000 <= args.sample_rate <= 192_000:
        raise SystemExit("--sample-rate must be between 8000 and 192000")
    if not 1 <= args.channels <= 8:
        raise SystemExit("--channels must be between 1 and 8")
    if not 0 <= args.mic_channel < args.channels:
        raise SystemExit("--mic-channel must select an existing channel")
    args.keywords = args.keywords.resolve()
    emit_runtime_status("starting", "关键词模型加载中")
    try:
        if args.list_devices:
            list_devices()
            return 0
        if args.self_test:
            test_keywords = MODEL_DIR / "test_wavs" / "test_keywords.txt"
            test_wav = MODEL_DIR / "test_wavs" / "6.wav"
            return run_wave(args, test_wav, test_keywords, "self_test")
        if args.wav:
            return run_wave(args, args.wav.resolve(), args.keywords, "wav")
        listen(args)
        emit_runtime_status("stopped", "关键词识别已停止")
        return 0
    except KeyboardInterrupt:
        log("\n收到 Ctrl+C，正在退出……")
        emit_runtime_status("stopped", "关键词识别已停止")
        return 130
    except Exception as exc:
        log(f"程序异常：{exc}")
        nonrecoverable = isinstance(exc, (FileNotFoundError, ModuleNotFoundError))
        emit_runtime_status(
            "error", f"关键词识别不可用：{exc}", recoverable=not nonrecoverable
        )
        return 1


if __name__ == "__main__":
    mp.freeze_support()
    raise SystemExit(main())
