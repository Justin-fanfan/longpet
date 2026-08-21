#!/usr/bin/env python3

import argparse
import subprocess
import time
import wave
from pathlib import Path

import numpy as np
import sounddevice as sd


RATE = 44_100
CHANNELS = 2
DEVICE = 0
BLOCKSIZE = 4_410


def load_wav(path):
    with wave.open(str(path), "rb") as source:
        source_rate = source.getframerate()
        source_channels = source.getnchannels()
        width = source.getsampwidth()
        frames = source.readframes(source.getnframes())
    if width != 2:
        raise ValueError("only 16-bit PCM wave files are supported")
    samples = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    samples = samples.reshape(-1, source_channels)
    mono = samples[:, 0]
    if source_rate != RATE:
        count = round(len(mono) * RATE / source_rate)
        old_positions = np.arange(len(mono), dtype=np.float64)
        new_positions = np.arange(count, dtype=np.float64) * source_rate / RATE
        mono = np.interp(new_positions, old_positions, mono).astype(np.float32)
    return mono


def write_wav(path, samples):
    samples = np.clip(samples, -1.0, 1.0)
    pcm = (samples * 32767.0).astype("<i2")
    with wave.open(str(path), "wb") as target:
        target.setnchannels(1)
        target.setsampwidth(2)
        target.setframerate(RATE)
        target.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("--gain", type=float, default=0.8)
    parser.add_argument("--before", type=float, default=1.0)
    parser.add_argument("--after", type=float, default=1.2)
    args = parser.parse_args()

    source = load_wav(args.source) * args.gain
    playback = np.clip(np.column_stack((source, source)), -1.0, 1.0)
    playback_pcm = (playback * 32767.0).astype("<i2")
    duration = args.before + len(source) / RATE + args.after
    captured = []
    counters = {"callbacks": 0, "status": 0}

    def callback(indata, frames, time_info, status):
        del frames, time_info
        counters["callbacks"] += 1
        if status:
            counters["status"] += 1
            print(f"INPUT_STATUS {status}", flush=True)
        captured.append(np.asarray(indata[:, 0], dtype=np.float32).copy())

    print(
        f"START source={args.source.name} source_seconds={len(source) / RATE:.3f} "
        f"record_seconds={duration:.3f}",
        flush=True,
    )
    with sd.InputStream(
        device=DEVICE,
        samplerate=RATE,
        channels=CHANNELS,
        dtype="float32",
        blocksize=BLOCKSIZE,
        callback=callback,
    ):
        time.sleep(args.before)
        subprocess.run(
            [
                "aplay",
                "-D",
                "hw:0,0",
                "-t",
                "raw",
                "-f",
                "S16_LE",
                "-c",
                "2",
                "-r",
                str(RATE),
                "-q",
            ],
            input=playback_pcm.tobytes(),
            check=True,
        )
        time.sleep(args.after)

    mono = np.concatenate(captured)
    peak = float(np.max(np.abs(mono)))
    rms = float(np.sqrt(np.mean(mono ** 2)))
    write_wav(args.target, mono)
    print(
        f"DONE callbacks={counters['callbacks']} status={counters['status']} "
        f"samples={len(mono)} peak={peak:.6f} rms={rms:.6f} "
        f"target={args.target}",
        flush=True,
    )


if __name__ == "__main__":
    main()
