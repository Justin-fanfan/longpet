#!/usr/bin/env python3

import argparse
import subprocess
import time

import numpy as np


RATE = 44_100
CHANNELS = 2
BLOCK_FRAMES = 4_410
BLOCK_BYTES = BLOCK_FRAMES * CHANNELS * 2


def read_exact(stream, size):
    data = bytearray()
    while len(data) < size:
        chunk = stream.read(size - len(data))
        if not chunk:
            return None
        data.extend(chunk)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="hw:0,1")
    parser.add_argument("--seconds", type=float, default=8.0)
    args = parser.parse_args()
    command = [
        "arecord", "-M", "-D", args.device, "-t", "raw", "-f", "S16_LE",
        "-c", str(CHANNELS), "-r", str(RATE), "-q",
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, bufsize=0)
    blocks = []
    started = time.monotonic()
    try:
        while time.monotonic() - started < args.seconds:
            raw = read_exact(process.stdout, BLOCK_BYTES)
            if raw is None:
                break
            blocks.append(np.frombuffer(raw, dtype="<i2").reshape(-1, 2).copy())
    finally:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    print(
        f"device={args.device} blocks={len(blocks)} "
        f"elapsed={time.monotonic() - started:.3f}s exit={process.returncode}",
        flush=True,
    )
    if not blocks:
        return 1
    data = np.concatenate(blocks).astype(np.float32) / 32768.0
    for channel in range(CHANNELS):
        x = data[:, channel]
        centered = x - np.mean(x)
        print(
            f"channel={channel} mean={float(np.mean(x)):.7f} "
            f"rms={float(np.sqrt(np.mean(x*x))):.7f} "
            f"ac_rms={float(np.sqrt(np.mean(centered*centered))):.7f} "
            f"peak={float(np.max(np.abs(x))):.7f} "
            f"ac_peak={float(np.max(np.abs(centered))):.7f}",
            flush=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
