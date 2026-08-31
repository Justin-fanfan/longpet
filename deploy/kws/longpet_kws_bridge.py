#!/usr/bin/env python3
"""Stable JSON-lines bridge around the upstream loongpet_kws runtime."""

import argparse
import json
import multiprocessing as mp
import queue
import sys
import threading
import time
from pathlib import Path

import numpy as np

PROTOCOL = "longpet-kws"
VERSION = 1
SUPPORTED_KEYWORDS = ["小龙小龙", "你好", "陪我说话", "救命"]
OUTPUT_LOCK = threading.Lock()


def emit(event, **fields):
    payload = {"protocol": PROTOCOL, "version": VERSION, "event": event}
    payload.update(fields)
    with OUTPUT_LOCK:
        print(json.dumps(payload, ensure_ascii=False, separators=(",", ":")), flush=True)


def command_reader(target):
    for line in sys.stdin:
        try:
            payload = json.loads(line)
            if payload.get("protocol") != PROTOCOL or payload.get("version") != VERSION:
                emit("error", message="unsupported command protocol/version")
                continue
            command = payload.get("command")
            if command in ("pause", "resume", "stop"):
                target.put(command)
            else:
                emit("error", message="unsupported command")
        except (TypeError, ValueError, json.JSONDecodeError) as error:
            emit("error", message=f"invalid command JSON: {error}")
    target.put("stop")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kws-root", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--tokens", required=True)
    parser.add_argument("--capture-backend", choices=("sounddevice", "arecord"),
                        default="sounddevice")
    parser.add_argument("--device", default=None)
    parser.add_argument("--alsa-device", default=None)
    parser.add_argument("--input-samplerate", type=int, default=48_000)
    parser.add_argument("--wake-threshold", type=float, default=0.15)
    parser.add_argument("--nihao-threshold", type=float, default=0.10)
    parser.add_argument("--peiwoshuohua-threshold", type=float, default=0.05)
    parser.add_argument("--jiuming-threshold", type=float, default=0.05)
    parser.add_argument("--vad-threshold-db", type=float, default=-60.0)
    parser.add_argument("--vad-noise-ratio", type=float, default=2.5)
    return parser.parse_args()


def main():
    args = parse_args()
    kws_root = Path(args.kws_root).resolve()
    sys.path.insert(0, str(kws_root / "src"))

    # Acoustic inference, FBank, VAD and capture stay in the requested upstream
    # project.  LongPet's C++ business layer only sees the JSONL protocol.
    from longpet_kws.cli import (  # pylint: disable=import-error,import-outside-toplevel
        ArecordCapture,
        FsmnKws,
        resample_block,
        sounddevice_capture_worker,
    )
    from longpet_kws.vad import EnergyVad  # pylint: disable=import-error,import-outside-toplevel

    if args.capture_backend == "arecord" and not args.alsa_device:
        raise ValueError("--alsa-device is required for arecord capture")
    device = int(args.device) if args.device and args.device.isdigit() else args.device
    thresholds = {
        "小龙小龙": args.wake_threshold,
        "你好": args.nihao_threshold,
        "陪我说话": args.peiwoshuohua_threshold,
        "救命": args.jiuming_threshold,
    }
    detector = FsmnKws(args.model, args.tokens, thresholds)

    def new_vad():
        return EnergyVad(sample_rate=16_000,
                         threshold_db=args.vad_threshold_db,
                         noise_ratio=args.vad_noise_ratio)

    vad = new_vad()
    commands = queue.Queue()
    threading.Thread(target=command_reader, args=(commands,), daemon=True,
                     name="longpet-kws-command").start()

    blocksize = round(args.input_samplerate * 0.1)
    process_context = mp.get_context("spawn")
    audio_queue = None
    status_queue = None
    stop_event = None
    capture = None
    capture_process = None
    paused = False
    announced_ready = False

    def drain_audio():
        if audio_queue is None:
            return
        while True:
            try:
                audio_queue.get_nowait()
            except queue.Empty:
                return

    def stop_capture():
        nonlocal capture, capture_process
        if capture is not None:
            capture.stop()
            capture = None
        if capture_process is not None:
            stop_event.set()
            capture_process.join(timeout=2)
            if capture_process.is_alive():
                capture_process.terminate()
                capture_process.join(timeout=1)
            capture_process = None
        drain_audio()

    def start_capture():
        nonlocal audio_queue, status_queue, stop_event, capture, capture_process
        if args.capture_backend == "arecord":
            audio_queue = queue.Queue(maxsize=4)
            status_queue = None
            capture = ArecordCapture(audio_queue, args.alsa_device,
                                     args.input_samplerate, blocksize)
            capture.start()
            return True
        audio_queue = process_context.Queue(maxsize=4)
        status_queue = process_context.Queue(maxsize=16)
        stop_event = process_context.Event()
        capture_process = process_context.Process(
            target=sounddevice_capture_worker,
            args=(audio_queue, status_queue, stop_event, device,
                  args.input_samplerate, blocksize),
            name="longpet-kws-capture", daemon=True)
        capture_process.start()
        return False

    capture_ready = start_capture()
    try:
        while True:
            while True:
                try:
                    command = commands.get_nowait()
                except queue.Empty:
                    break
                if command == "stop":
                    stop_capture()
                    emit("stopped")
                    return
                if command == "pause" and not paused:
                    stop_capture()
                    detector.reset_stream()
                    vad = new_vad()
                    paused = True
                    capture_ready = False
                    emit("paused")
                elif command == "pause":
                    emit("paused")
                elif command == "resume" and paused:
                    detector.reset_stream()
                    vad = new_vad()
                    capture_ready = start_capture()
                    paused = False
                    if capture_ready:
                        emit("resumed")
                elif command == "resume" and not paused:
                    emit("resumed")

            if paused:
                time.sleep(0.05)
                continue

            if status_queue is not None:
                while True:
                    try:
                        kind, message = status_queue.get_nowait()
                    except queue.Empty:
                        break
                    if kind == "fatal":
                        raise RuntimeError(f"sounddevice capture failed: {message}")
                    if kind == "ready":
                        capture_ready = True
                        if announced_ready:
                            emit("resumed")
            if args.capture_backend == "arecord" and capture is not None:
                process = capture.process
                if process is not None and process.poll() is not None:
                    raise RuntimeError(f"arecord exited with code {process.returncode}")

            if capture_ready and not announced_ready:
                announced_ready = True
                emit("ready", supported_keywords=SUPPORTED_KEYWORDS)

            try:
                raw = audio_queue.get(timeout=0.1)
            except queue.Empty:
                continue
            capture_audio = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
            block = resample_block(capture_audio, args.input_samplerate)
            for chunk in vad.accept(block):
                for keyword, score in detector.accept(chunk.audio):
                    emit("keyword", keyword=keyword, score=round(score, 6),
                         timestamp_ms=int(time.time() * 1000))
                if chunk.speech_ended:
                    detector.reset_stream()
    finally:
        stop_capture()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:  # Process boundary: report, then let Qt restart safely.
        emit("error", message=f"{type(error).__name__}: {error}")
        raise
