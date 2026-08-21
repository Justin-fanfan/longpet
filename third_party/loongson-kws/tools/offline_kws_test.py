#!/usr/bin/env python3

import argparse
import time
import wave
from pathlib import Path

import numpy as np
import sherpa_onnx


PROJECT_ROOT = Path(__file__).resolve().parents[1]
MODEL_DIR = (
    PROJECT_ROOT
    / "models"
    / "sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01"
)


def load_wave(path):
    with wave.open(str(path), "rb") as source:
        rate = source.getframerate()
        channels = source.getnchannels()
        width = source.getsampwidth()
        frames = source.readframes(source.getnframes())
    if width != 2:
        raise ValueError(f"{path}: expected 16-bit PCM, got width={width}")
    samples = np.frombuffer(frames, dtype="<i2")
    if channels > 1:
        samples = samples.reshape(-1, channels)[:, 0]
    return rate, np.asarray(samples, dtype=np.float32) / 32768.0


def create_kws(keywords, int8, threshold, score):
    suffix = ".int8.onnx" if int8 else ".onnx"
    print(
        f"CREATE model={'INT8' if int8 else 'FP32'} "
        f"keywords={keywords.name} threshold={threshold} score={score}",
        flush=True,
    )
    started = time.monotonic()
    kws = sherpa_onnx.KeywordSpotter(
        encoder=str(MODEL_DIR / f"encoder-epoch-12-avg-2-chunk-16-left-64{suffix}"),
        decoder=str(MODEL_DIR / f"decoder-epoch-12-avg-2-chunk-16-left-64{suffix}"),
        joiner=str(MODEL_DIR / f"joiner-epoch-12-avg-2-chunk-16-left-64{suffix}"),
        tokens=str(MODEL_DIR / "tokens.txt"),
        keywords_file=str(keywords),
        num_threads=1,
        sample_rate=16_000,
        keywords_score=score,
        keywords_threshold=threshold,
    )
    print(f"CREATED elapsed={time.monotonic() - started:.3f}s", flush=True)
    return kws


def decode_file(kws, path):
    rate, samples = load_wave(path)
    stream = kws.create_stream()
    # KWS needs trailing context after the spoken phrase.
    samples = np.concatenate((samples, np.zeros(int(rate * 1.0), dtype=np.float32)))
    started = time.monotonic()
    stream.accept_waveform(rate, samples)
    stream.input_finished()
    decodes = 0
    result = ""
    while kws.is_ready(stream):
        kws.decode_stream(stream)
        decodes += 1
        current = kws.get_result(stream)
        if current:
            result = current
            break
    elapsed = time.monotonic() - started
    duration = len(samples) / rate
    print(
        f"FILE={path.name} audio={duration:.3f}s decodes={decodes} "
        f"elapsed={elapsed:.3f}s rtf={elapsed / duration:.3f} result={result!r}",
        flush=True,
    )
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--keywords", type=Path)
    parser.add_argument("--fp32", action="store_true")
    parser.add_argument("--threshold", type=float, default=0.25)
    parser.add_argument("--score", type=float, default=1.0)
    args = parser.parse_args()

    keywords = args.keywords or MODEL_DIR / "test_wavs" / "test_keywords.txt"
    paths = [Path(value) for value in args.paths]
    if not paths:
        paths = sorted((MODEL_DIR / "test_wavs").glob("*.wav"))
    kws = create_kws(keywords, not args.fp32, args.threshold, args.score)
    hits = 0
    for path in paths:
        if decode_file(kws, path):
            hits += 1
    print(f"SUMMARY files={len(paths)} hits={hits}", flush=True)


if __name__ == "__main__":
    main()
