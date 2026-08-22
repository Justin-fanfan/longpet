#!/usr/bin/env python3

import argparse
import queue
import time

import numpy as np
import sounddevice as sd


DEVICE = 0
RATE = 44_100
CHANNELS = 2
FRAMES = 4_410


def mark(message):
    print(f"[{time.monotonic():.3f}] {message}", flush=True)


def blocking(simulate_decode):
    with sd.InputStream(
        device=args_device,
        samplerate=RATE,
        channels=CHANNELS,
        dtype="float32",
        blocksize=FRAMES,
    ) as microphone:
        for index in range(30):
            mark(f"#{index} BEFORE read")
            started = time.monotonic()
            samples, overflowed = microphone.read(FRAMES)
            elapsed = time.monotonic() - started
            rms = float(np.sqrt(np.mean(samples[:, 0] ** 2)))
            mark(
                f"#{index} AFTER read elapsed={elapsed:.3f}s "
                f"overflow={overflowed} rms={rms:.6f}"
            )
            if simulate_decode and index in (3, 7, 11, 15):
                mark(">>> simulate decode for 1 second")
                time.sleep(1.0)
                mark("<<< simulate decode finished")
    mark("DONE")


def callback_probe(seconds):
    chunks = queue.Queue(maxsize=4)
    counters = {"callbacks": 0, "dropped": 0, "status": 0}

    def audio_callback(indata, frames, time_info, status):
        del frames, time_info
        counters["callbacks"] += 1
        if status:
            counters["status"] += 1
        mono = np.asarray(indata[:, 0], dtype=np.float32).copy()
        try:
            chunks.put_nowait(mono)
        except queue.Full:
            counters["dropped"] += 1
            try:
                chunks.get_nowait()
            except queue.Empty:
                pass
            try:
                chunks.put_nowait(mono)
            except queue.Full:
                pass

    with sd.InputStream(
        device=args_device,
        samplerate=RATE,
        channels=CHANNELS,
        dtype="float32",
        blocksize=FRAMES,
        callback=audio_callback,
    ):
        deadline = time.monotonic() + seconds
        consumed = 0
        while time.monotonic() < deadline:
            try:
                samples = chunks.get(timeout=0.5)
            except queue.Empty:
                mark("WAITING: no callback data")
                continue
            consumed += 1
            rms = float(np.sqrt(np.mean(samples ** 2)))
            mark(
                f"consume={consumed} callback={counters['callbacks']} "
                f"dropped={counters['dropped']} status={counters['status']} "
                f"rms={rms:.6f}"
            )
            if consumed % 4 == 0:
                mark(">>> slow consumer for 1 second")
                time.sleep(1.0)
                mark("<<< slow consumer finished")
    mark(
        f"DONE callback={counters['callbacks']} "
        f"consumed={consumed} dropped={counters['dropped']} "
        f"status={counters['status']}"
    )


def main():
    global args_device
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "mode", choices=("blocking", "blocking-slow", "callback")
    )
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--device", type=int, default=DEVICE)
    args = parser.parse_args()
    args_device = args.device

    if args.mode == "blocking":
        blocking(False)
    elif args.mode == "blocking-slow":
        blocking(True)
    else:
        callback_probe(args.seconds)


if __name__ == "__main__":
    main()
