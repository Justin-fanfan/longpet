#!/usr/bin/env python3

import time

import numpy as np
import sounddevice as sd


RATE = 44_100
CHANNELS = 2
BLOCKSIZE = 4_410
blocks = []
statuses = []


def callback(indata, frames, time_info, status):
    del frames, time_info
    if status:
        statuses.append(str(status))
    blocks.append(np.asarray(indata, dtype=np.float32).copy())


with sd.InputStream(
    device=0,
    samplerate=RATE,
    channels=CHANNELS,
    dtype="float32",
    blocksize=BLOCKSIZE,
    callback=callback,
):
    time.sleep(8.0)

print(f"blocks={len(blocks)} statuses={statuses}", flush=True)
if blocks:
    data = np.concatenate(blocks)
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
