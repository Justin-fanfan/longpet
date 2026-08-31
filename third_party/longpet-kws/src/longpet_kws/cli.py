"""Streaming Chinese KWS using the pretrained WeKWS FSMN-CTC ONNX model."""

from __future__ import annotations

import argparse
import json
import math
import multiprocessing as mp
import queue
import subprocess
import sys
import threading
import time
from collections import defaultdict
from pathlib import Path

import numpy as np
import onnxruntime as ort
import sounddevice as sd

from .fbank import kaldi_fbank
from .vad import EnergyVad, VadChunk

SAMPLE_RATE = 16_000
DEFAULT_INPUT_DEVICE = 2
DEFAULT_INPUT_SAMPLE_RATE = 48_000
DEFAULT_VAD_THRESHOLD_DB = -60.0
PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MODEL = PROJECT_ROOT / "assets" / "fsmn" / "fsmn_ctc.onnx"
DEFAULT_TOKENS = PROJECT_ROOT / "assets" / "fsmn" / "tokens.txt"
KEYWORDS = ["小龙小龙", "你好", "陪我说话", "救命"]
DEFAULT_KEYWORD_THRESHOLDS = {
    "小龙小龙": 0.15,
    "你好": 0.10,
    "陪我说话": 0.05,
    "救命": 0.05,
}
KEYWORD_ALIASES = {
    "小龙小龙": ["小龙小龙"],
    "你好": ["你好"],
    "陪我说话": ["陪我说话"],
    # The general Chinese CTC model often emits homophones for the rare word
    # 救 while preserving the pronunciation. Map those paths to one KWS event.
    "救命": ["救命", "就命", "九命", "降命"],
}


def resample_block(audio: np.ndarray, input_rate: int, output_rate: int = SAMPLE_RATE) -> np.ndarray:
    """Linearly resample one microphone block to the model's 16-kHz rate."""
    samples = np.asarray(audio, dtype=np.float32)
    if input_rate == output_rate or len(samples) == 0:
        return samples
    output_count = max(1, round(len(samples) * output_rate / input_rate))
    source_positions = np.linspace(0.0, len(samples) - 1, output_count, dtype=np.float32)
    return np.interp(source_positions, np.arange(len(samples), dtype=np.float32), samples).astype(np.float32)


def _put_latest(target_queue, item) -> None:
    """Put without blocking, dropping one stale item when the queue is full."""
    try:
        target_queue.put_nowait(item)
    except queue.Full:
        try:
            target_queue.get_nowait()
        except queue.Empty:
            return
        try:
            target_queue.put_nowait(item)
        except queue.Full:
            pass


def sounddevice_capture_worker(
    audio_queue,
    status_queue,
    stop_event,
    device,
    sample_rate: int,
    blocksize: int,
) -> None:
    """Capture raw PCM with blocking reads, isolated from ONNX inference."""
    try:
        with sd.RawInputStream(
            samplerate=sample_rate,
            # Let ALSA/PortAudio choose its native period size.  A fixed large
            # callback block overflows on some embedded USB Audio devices.
            blocksize=0,
            channels=1,
            dtype="int16",
            device=device,
            latency="high",
        ) as stream:
            _put_latest(status_queue, ("ready", "sounddevice capture started"))
            while not stop_event.is_set():
                raw, overflowed = stream.read(blocksize)
                if overflowed:
                    _put_latest(status_queue, ("warning", "input overflow"))
                _put_latest(audio_queue, bytes(raw))
    except BaseException as error:
        _put_latest(status_queue, ("fatal", repr(error)))


class ArecordCapture:
    """Read ALSA PCM in a separate process so inference cannot block capture."""

    def __init__(self, audio_queue, device: str, sample_rate: int, blocksize: int):
        self.audio_queue = audio_queue
        self.device = device
        self.sample_rate = sample_rate
        self.block_bytes = blocksize * 2  # S16_LE, mono
        self.process: subprocess.Popen[bytes] | None = None
        self.thread: threading.Thread | None = None

    def start(self) -> None:
        self.process = subprocess.Popen(
            [
                "arecord", "-q", "-D", self.device, "-f", "S16_LE",
                "-r", str(self.sample_rate), "-c", "1", "-t", "raw", "-",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            # Must NOT be 0: a raw (unbuffered) stdout makes read(n) return a
            # partial frame whenever the pipe is not yet full, which _read()
            # misinterprets as EOF and stops capture after one block.  Use the
            # default buffered stream (which keeps reading until n bytes or EOF).
        )
        self.thread = threading.Thread(target=self._read, name="alsa-capture", daemon=True)
        self.thread.start()

    def _read(self) -> None:
        assert self.process is not None and self.process.stdout is not None
        while True:
            raw = self.process.stdout.read(self.block_bytes)
            if len(raw) < self.block_bytes:
                return
            _put_latest(self.audio_queue, raw)

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()


def is_sublist(sequence: tuple[int, ...], target: tuple[int, ...]) -> int:
    for offset in range(len(sequence) - len(target) + 1):
        if sequence[offset : offset + len(target)] == target:
            return offset
    return -1


def ctc_prefix_step(probs, current, keyword_ids: set[int], beam_size: int = 6):
    next_hyps = defaultdict(lambda: (0.0, 0.0, []))
    top_indices = np.argpartition(probs, -beam_size)[-beam_size:]
    top_indices = top_indices[np.argsort(probs[top_indices])[::-1]]
    candidates = [
        (int(index), float(probs[index]))
        for index in top_indices
        for probability in (probs[index],)
        if float(probability) > 0.02 and (int(index) == 0 or int(index) in keyword_ids)
    ]
    if not candidates:
        return current
    for token, probability in candidates:
        for prefix, (p_blank, p_non_blank, nodes) in current:
            last = prefix[-1] if prefix else None
            if token == 0:
                old_blank, old_non_blank, _ = next_hyps[prefix]
                next_hyps[prefix] = (
                    old_blank + (p_blank + p_non_blank) * probability,
                    old_non_blank,
                    nodes.copy(),
                )
            elif token == last:
                if p_non_blank:
                    old_blank, old_non_blank, old_nodes = next_hyps[prefix]
                    new_nodes = nodes.copy()
                    if probability > new_nodes[-1]["prob"]:
                        new_nodes[-1] = {"token": token, "frame": new_nodes[-1]["frame"], "prob": probability}
                    next_hyps[prefix] = (old_blank, old_non_blank + p_non_blank * probability, new_nodes)
                if p_blank:
                    new_prefix = prefix + (token,)
                    old_blank, old_non_blank, _ = next_hyps[new_prefix]
                    new_nodes = nodes.copy() + [{"token": token, "frame": 0, "prob": probability}]
                    next_hyps[new_prefix] = (old_blank, old_non_blank + p_blank * probability, new_nodes)
            else:
                new_prefix = prefix + (token,)
                old_blank, old_non_blank, _ = next_hyps[new_prefix]
                new_nodes = nodes.copy() + [{"token": token, "frame": 0, "prob": probability}]
                next_hyps[new_prefix] = (
                    old_blank,
                    old_non_blank + (p_blank + p_non_blank) * probability,
                    new_nodes,
                )
    return sorted(next_hyps.items(), key=lambda item: item[1][0] + item[1][1], reverse=True)[:20]


class StreamingFbank:
    def __init__(self):
        self.wave_remained = np.empty(0, dtype=np.float32)
        self.feature_remained: np.ndarray | None = None
        self.frame_offset = 0

    def accept(self, audio: np.ndarray) -> np.ndarray | None:
        # kaldi.fbank expects the int16 amplitude scale used by WeKWS training.
        wave = np.concatenate((self.wave_remained, np.asarray(audio, dtype=np.float32) * 32768.0))
        if len(wave) < 800:
            self.wave_remained = wave
            return None
        features = kaldi_fbank(wave)
        feature_count = len(features)
        self.wave_remained = wave[feature_count * 160 :]
        if feature_count <= 2:
            return None

        if self.feature_remained is None:
            padded = np.concatenate((np.repeat(features[:1], 2, axis=0), features), axis=0)
        else:
            padded = np.concatenate((self.feature_remained, features), axis=0)
        context_count = padded.shape[0] - 4
        if context_count <= 0:
            self.feature_remained = features[-4:]
            return None
        context = np.stack([padded[i : i + 5].reshape(-1) for i in range(context_count)])
        self.feature_remained = features[-4:]

        start = 0 if self.frame_offset == 0 else 3 - self.frame_offset
        remainder = (len(context) + start) % 3
        context = context[start::3]
        self.frame_offset = remainder if remainder == 0 else 3 - remainder
        return context.astype(np.float32, copy=False) if len(context) else None


class FsmnKws:
    def __init__(self, model: str, tokens: str, thresholds: dict[str, float]):
        options = ort.SessionOptions()
        options.intra_op_num_threads = 1
        options.inter_op_num_threads = 1
        self.session = ort.InferenceSession(model, sess_options=options, providers=["CPUExecutionProvider"])
        self.tokens = {}
        for line in Path(tokens).read_text(encoding="utf-8").splitlines():
            token, index = line.rsplit(maxsplit=1)
            # This ModelScope symbol table is one-based for lexical tokens,
            # while the 2599-way CTC output tensor is zero-based.
            token_id = int(index)
            if not token.startswith("<") and token != "sil" and token_id > 0:
                token_id -= 1
            self.tokens[token] = token_id
        missing = sorted({char for aliases in KEYWORD_ALIASES.values() for word in aliases for char in word if char not in self.tokens})
        if missing:
            raise ValueError(f"Tokens missing Chinese characters: {missing}")
        self.keyword_tokens = {
            alias: (canonical, tuple(self.tokens[char] for char in alias))
            for canonical, aliases in KEYWORD_ALIASES.items()
            for alias in aliases
        }
        self.keyword_ids = {index for _, ids in self.keyword_tokens.values() for index in ids}
        self.thresholds = thresholds
        self.frontend = StreamingFbank()
        self.cache = np.zeros((1, 128, 11, 4), dtype=np.float32)
        self.hyps = [(tuple(), (1.0, 0.0, []))]
        self.frame = 0
        self.keyword_scores = {keyword: 0.0 for keyword in KEYWORDS}

    def reset_stream(self) -> None:
        """Reset acoustic and CTC state at a VAD utterance boundary."""
        self.frontend = StreamingFbank()
        self.cache.fill(0.0)
        self.hyps = [(tuple(), (1.0, 0.0, []))]
        self.frame = 0
        self.keyword_scores = {keyword: 0.0 for keyword in KEYWORDS}

    def accept(self, audio: np.ndarray) -> list[tuple[str, float]]:
        features = self.frontend.accept(audio)
        if features is None:
            return []
        output, self.cache = self.session.run(None, {"input": features[None, ...], "cache": self.cache})
        events = []
        for probabilities in output[0]:
            old_lengths = {prefix: len(nodes) for prefix, (_, _, nodes) in self.hyps}
            self.hyps = ctc_prefix_step(probabilities, self.hyps, self.keyword_ids)
            # Assign the current absolute frame to nodes newly appended by this step.
            patched = []
            for prefix, (pb, pnb, nodes) in self.hyps:
                previous_length = old_lengths.get(prefix[:-1], len(nodes))
                for node in nodes[previous_length:]:
                    if node["frame"] == 0:
                        node["frame"] = self.frame
                patched.append((prefix, (pb, pnb, nodes)))
            self.hyps = patched
            self.frame += 3
            for prefix, (_, _, nodes) in self.hyps:
                for alias, (word, token_ids) in self.keyword_tokens.items():
                    offset = is_sublist(prefix, token_ids)
                    if offset < 0:
                        continue
                    selected = nodes[offset : offset + len(token_ids)]
                    score = float(np.prod([node["prob"] for node in selected]) ** (1.0 / len(selected)))
                    self.keyword_scores[word] = max(self.keyword_scores[word], score)
                    duration = selected[-1]["frame"] - selected[0]["frame"]
                    threshold = self.thresholds[word]
                    if score >= threshold and 3 <= duration <= 250:
                        events.append((word, score))
                        self.hyps = [(tuple(), (1.0, 0.0, []))]
                        return events
        return events


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", default=str(DEFAULT_MODEL))
    parser.add_argument("--tokens", default=str(DEFAULT_TOKENS))
    parser.add_argument("--threshold", type=float, default=None, help="override all keyword thresholds")
    parser.add_argument("--wake-threshold", type=float, default=DEFAULT_KEYWORD_THRESHOLDS["小龙小龙"])
    parser.add_argument("--nihao-threshold", type=float, default=DEFAULT_KEYWORD_THRESHOLDS["你好"])
    parser.add_argument("--peiwoshuohua-threshold", type=float, default=DEFAULT_KEYWORD_THRESHOLDS["陪我说话"])
    parser.add_argument("--jiuming-threshold", type=float, default=DEFAULT_KEYWORD_THRESHOLDS["救命"])
    parser.add_argument("--device", default=DEFAULT_INPUT_DEVICE)
    parser.add_argument(
        "--input-samplerate",
        type=int,
        default=DEFAULT_INPUT_SAMPLE_RATE,
        help="microphone capture rate; resampled to 16000 for FSMN (try 48000 on ALSA hardware)",
    )
    parser.add_argument(
        "--capture-backend",
        choices=("auto", "sounddevice", "arecord"),
        default="sounddevice",
        help="audio capture backend; auto uses isolated sounddevice capture",
    )
    parser.add_argument("--alsa-device", default=None, help="ALSA PCM name, e.g. plughw:1,0")
    parser.add_argument("--list-devices", action="store_true")
    parser.add_argument("--duration", type=float, default=0.0)
    parser.add_argument("--command-timeout", type=float, default=10.0)
    parser.add_argument("--no-vad", action="store_true", help="disable the energy VAD")
    parser.add_argument("--vad-threshold-db", type=float, default=DEFAULT_VAD_THRESHOLD_DB)
    parser.add_argument("--vad-noise-ratio", type=float, default=2.5)
    parser.add_argument("--vad-debug", action="store_true")
    parser.add_argument("--audio-debug", action="store_true", help="print input RMS and peak levels once per second")
    parser.add_argument("--show-scores", action="store_true", help="print best keyword scores for current speech")
    args = parser.parse_args()
    if args.list_devices:
        print(sd.query_devices())
        return
    device = int(args.device) if isinstance(args.device, str) and args.device.isdigit() else args.device
    capture_backend = args.capture_backend
    if capture_backend == "auto":
        capture_backend = "sounddevice"
    if capture_backend == "arecord" and not args.alsa_device:
        parser.error("--alsa-device is required with --capture-backend arecord")

    thresholds = {
        "小龙小龙": args.wake_threshold,
        "你好": args.nihao_threshold,
        "陪我说话": args.peiwoshuohua_threshold,
        "救命": args.jiuming_threshold,
    }
    if args.threshold is not None:
        thresholds = {keyword: args.threshold for keyword in KEYWORDS}
    detector = FsmnKws(args.model, args.tokens, thresholds)
    vad = None if args.no_vad else EnergyVad(
        sample_rate=SAMPLE_RATE,
        threshold_db=args.vad_threshold_db,
        noise_ratio=args.vad_noise_ratio,
    )
    active_until = 0.0
    # Read 100 ms at a time.  The PortAudio stream itself uses blocksize=0 so
    # ALSA can select the hardware-native period size.
    capture_blocksize = round(args.input_samplerate * 0.1)
    process_context = mp.get_context("spawn")
    if capture_backend == "sounddevice":
        audio_queue = process_context.Queue(maxsize=4)
        capture_status_queue = process_context.Queue(maxsize=16)
        capture_stop_event = process_context.Event()
    else:
        audio_queue = queue.Queue(maxsize=4)
        capture_status_queue = None
        capture_stop_event = None

    print("FSMN-CTC 监听中：先说“小龙小龙”，再说“你好 / 陪我说话 / 救命”")
    print("THRESHOLDS:", json.dumps(thresholds, ensure_ascii=False))
    print("VAD:", "disabled" if vad is None else f"energy ({args.vad_threshold_db:.1f} dBFS)")
    print(f"CAPTURE: {args.input_samplerate} Hz -> {SAMPLE_RATE} Hz")
    print(f"BACKEND: {capture_backend}")
    end_at = time.monotonic() + args.duration if args.duration else None

    def consume_audio() -> None:
        nonlocal active_until
        last_audio_report = 0.0
        while end_at is None or time.monotonic() < end_at:
            if capture_status_queue is not None:
                while True:
                    try:
                        status_kind, status_text = capture_status_queue.get_nowait()
                    except queue.Empty:
                        break
                    if status_kind == "fatal":
                        raise RuntimeError(f"sounddevice capture failed: {status_text}")
                    if status_kind == "warning":
                        print(f"CAPTURE WARNING: {status_text}")
            try:
                raw = audio_queue.get(timeout=1.0)
            except queue.Empty:
                continue
            capture_audio = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
            block = resample_block(capture_audio, args.input_samplerate)
            if args.audio_debug and time.monotonic() - last_audio_report >= 1.0:
                centered = block - np.mean(block, dtype=np.float32)
                rms = float(np.sqrt(np.mean(centered * centered, dtype=np.float32)))
                peak = float(np.max(np.abs(block))) if len(block) else 0.0
                rms_db = 20.0 * math.log10(max(rms, 1e-6))
                peak_db = 20.0 * math.log10(max(peak, 1e-6))
                print(f"AUDIO: rms={rms_db:.1f} dBFS peak={peak_db:.1f} dBFS")
                last_audio_report = time.monotonic()
            chunks = [VadChunk(block)] if vad is None else vad.accept(block)
            for chunk in chunks:
                if args.vad_debug and chunk.speech_started:
                    print("VAD: speech start")
                events = detector.accept(chunk.audio)
                if args.show_scores:
                    scores = {keyword: round(detector.keyword_scores[keyword], 4) for keyword in KEYWORDS}
                    print(json.dumps({"scores": scores}, ensure_ascii=False))
                for keyword, score in events:
                    now = time.monotonic()
                    print(json.dumps({"keyword": keyword, "score": round(score, 4)}, ensure_ascii=False))
                    if keyword == "小龙小龙":
                        active_until = now + args.command_timeout
                        print("WAKE: 小龙小龙")
                    elif now < active_until:
                        print(f"COMMAND: {keyword}")
                        # One wake grants exactly one command.  Return to standby
                        # immediately after dispatching it.
                        active_until = 0.0
                if chunk.speech_ended:
                    detector.reset_stream()
                    if args.vad_debug:
                        print("VAD: speech end")

    if capture_backend == "arecord":
        capture = ArecordCapture(audio_queue, args.alsa_device, args.input_samplerate, capture_blocksize)
        capture.start()
        try:
            consume_audio()
        finally:
            capture.stop()
    else:
        capture_process = process_context.Process(
            target=sounddevice_capture_worker,
            args=(
                audio_queue,
                capture_status_queue,
                capture_stop_event,
                device,
                args.input_samplerate,
                capture_blocksize,
            ),
            name="sounddevice-capture",
            daemon=True,
        )
        capture_process.start()
        try:
            consume_audio()
        finally:
            capture_stop_event.set()
            capture_process.join(timeout=3)
            if capture_process.is_alive():
                capture_process.terminate()
                capture_process.join(timeout=1)


if __name__ == "__main__":
    main()
