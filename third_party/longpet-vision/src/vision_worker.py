from __future__ import annotations

import argparse
import json
import signal
import sys
import time
from datetime import datetime, timezone

try:
    import cv2
    import numpy as np
except Exception as error:  # pragma: no cover - target degradation path
    print(
        json.dumps(
            {
                "event": "runtime_status",
                "state": "degraded",
                "available": False,
                "monitoring": False,
                "detail": f"视觉依赖加载失败：{error}",
            },
            ensure_ascii=False,
        ),
        flush=True,
    )
    raise SystemExit(2)

from motion_detector import MotionVisionDetector


STOP_REQUESTED = False


def emit(payload: dict) -> None:
    print(json.dumps(payload, ensure_ascii=False, separators=(",", ":")), flush=True)


def request_stop(_signum: int, _frame: object) -> None:
    global STOP_REQUESTED
    STOP_REQUESTED = True


def available_memory_mb() -> float | None:
    try:
        with open("/proc/meminfo", "r", encoding="ascii") as source:
            for line in source:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) / 1024.0
    except (OSError, ValueError, IndexError):
        return None
    return None


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="LongPet local vision worker")
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--fps", type=float, default=8.0)
    parser.add_argument("--fall-enabled", action="store_true")
    parser.add_argument("--wave-enabled", action="store_true")
    parser.add_argument("--heartbeat-seconds", type=float, default=5.0)
    parser.add_argument("--min-available-mb", type=float, default=32.0)
    parser.add_argument("--self-test", action="store_true")
    return parser


def synthetic_frames(kind: str):
    blank = np.zeros((240, 320, 3), dtype=np.uint8)
    for _ in range(40):
        yield blank
    if kind == "fall":
        for _ in range(8):
            frame = blank.copy()
            cv2.rectangle(frame, (135, 40), (180, 220), (205, 205, 205), -1)
            yield frame
        for step in range(10):
            frame = blank.copy()
            x = 135 - step * 6
            y = 40 + step * 12
            width = 45 + step * 13
            height = 180 - step * 14
            cv2.rectangle(
                frame,
                (x, y),
                (min(319, x + width), min(239, y + height)),
                (205, 205, 205),
                -1,
            )
            yield frame
        for _ in range(12):
            frame = blank.copy()
            cv2.rectangle(frame, (72, 180), (254, 228), (205, 205, 205), -1)
            yield frame
        return

    positions = (
        (92, 92), (108, 72), (128, 58), (150, 72), (170, 94),
        (150, 112), (128, 126), (108, 112), (88, 92), (108, 72),
        (128, 58), (150, 72), (172, 94), (150, 112), (128, 126),
        (108, 112), (88, 92), (108, 72), (128, 58), (150, 72),
        (172, 94),
    )
    for x, y in positions:
        frame = blank.copy()
        cv2.rectangle(frame, (145, 125), (178, 229), (80, 80, 80), -1)
        cv2.circle(frame, (x, y), 14, (90, 145, 190), -1)
        yield frame


def run_self_test() -> int:
    results: dict[str, list[dict]] = {}
    timings: dict[str, float] = {}
    for kind in ("fall", "wave"):
        detector = MotionVisionDetector(
            fall_enabled=kind == "fall", wave_enabled=kind == "wave"
        )
        events: list[dict] = []
        started = time.perf_counter()
        frame_count = 0
        for frame_count, frame in enumerate(synthetic_frames(kind), start=1):
            if STOP_REQUESTED:
                break
            events.extend(detector.process(frame, (frame_count - 1) / 8.0))
        elapsed = time.perf_counter() - started
        results[kind] = events
        timings[kind] = elapsed * 1000.0 / max(1, frame_count)
    emit(
        {
            "event": "self_test",
            "passed": bool(results["fall"] and results["wave"]),
            "results": results,
            "mean_ms": {key: round(value, 3) for key, value in timings.items()},
        }
    )
    return 0 if results["fall"] and results["wave"] else 1


def open_camera(args: argparse.Namespace):
    if sys.platform.startswith("linux"):
        capture = cv2.VideoCapture(args.camera, cv2.CAP_V4L2)
    else:
        capture = cv2.VideoCapture(args.camera)
    if not capture.isOpened():
        capture.release()
        return None
    capture.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    capture.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    capture.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
    capture.set(cv2.CAP_PROP_FPS, args.fps)
    capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    return capture


def main() -> int:
    args = make_parser().parse_args()
    if args.self_test:
        return run_self_test()
    if not args.fall_enabled and not args.wave_enabled:
        emit(
            {
                "event": "runtime_status",
                "state": "disabled",
                "available": True,
                "camera_available": False,
                "monitoring": False,
                "detail": "视觉算法均未启用",
            }
        )
        return 0

    memory = available_memory_mb()
    if memory is not None and memory < args.min_available_mb:
        emit(
            {
                "event": "runtime_status",
                "state": "degraded",
                "available": False,
                "camera_available": False,
                "monitoring": False,
                "detail": f"系统可用内存不足，视觉未启动（{memory:.0f} MiB）",
            }
        )
        return 3

    capture = open_camera(args)
    if capture is None:
        emit(
            {
                "event": "runtime_status",
                "state": "no_camera",
                "available": False,
                "camera_available": False,
                "monitoring": False,
                "detail": f"摄像头 /dev/video{args.camera} 不可用",
                "camera_index": args.camera,
            }
        )
        return 4

    detector = MotionVisionDetector(
        width=args.width,
        height=args.height,
        fall_enabled=args.fall_enabled,
        wave_enabled=args.wave_enabled,
    )
    capabilities = ["挥手"] if args.wave_enabled else []
    if args.fall_enabled:
        capabilities.append("跌倒候选（实验）")
    emit(
        {
            "event": "runtime_status",
            "state": "monitoring",
            "available": True,
            "camera_available": True,
            "monitoring": True,
            "detail": "本地视觉：" + "、".join(capabilities),
            "camera_index": args.camera,
        }
    )

    interval = 1.0 / max(1.0, args.fps)
    next_process = time.monotonic()
    window_started = next_process
    heartbeat_at = next_process + max(1.0, args.heartbeat_seconds)
    processed = 0
    process_seconds = 0.0
    try:
        while not STOP_REQUESTED:
            now = time.monotonic()
            if now < next_process:
                # The board UVC camera exposes MJPEG at 30 FPS even when the
                # requested processing rate is lower. grab() advances the
                # device buffer without paying JPEG decode cost for frames the
                # detector will not consume.
                if capture.grab():
                    continue
                if STOP_REQUESTED:
                    break
                emit(
                    {
                        "event": "runtime_status",
                        "state": "degraded",
                        "available": False,
                        "camera_available": False,
                        "monitoring": False,
                        "detail": "摄像头取帧失败，视觉监护已停止",
                        "camera_index": args.camera,
                    }
                )
                return 5
            ok, frame = capture.read()
            if not ok:
                if STOP_REQUESTED:
                    break
                emit(
                    {
                        "event": "runtime_status",
                        "state": "degraded",
                        "available": False,
                        "camera_available": False,
                        "monitoring": False,
                        "detail": "摄像头取帧失败，视觉监护已停止",
                        "camera_index": args.camera,
                    }
                )
                return 5
            now = time.monotonic()
            next_process = max(next_process + interval, now)
            started = time.perf_counter()
            events = detector.process(frame, now)
            process_seconds += time.perf_counter() - started
            processed += 1
            for event in events:
                emit(
                    {
                        "event": "vision_detected",
                        "type": event["type"],
                        "confidence": event["confidence"],
                        "timestamp": datetime.now(timezone.utc).isoformat(
                            timespec="milliseconds"
                        ),
                        "source": "opencv_mog2_trajectory",
                        "metadata": event.get("metrics", {}),
                    }
                )

            if now >= heartbeat_at:
                elapsed = max(1e-6, now - window_started)
                memory = available_memory_mb()
                fps = processed / elapsed
                mean_ms = process_seconds * 1000.0 / max(1, processed)
                if memory is not None and memory < args.min_available_mb:
                    emit(
                        {
                            "event": "runtime_status",
                            "state": "degraded",
                            "available": False,
                            "camera_available": True,
                            "monitoring": False,
                            "detail": f"可用内存降至 {memory:.0f} MiB，视觉已主动退出",
                            "fps": round(fps, 2),
                            "frame_ms": round(mean_ms, 2),
                            "camera_index": args.camera,
                        }
                    )
                    return 6
                emit(
                    {
                        "event": "runtime_status",
                        "state": "monitoring",
                        "available": True,
                        "camera_available": True,
                        "monitoring": True,
                        "detail": "本地视觉：" + "、".join(capabilities),
                        "fps": round(fps, 2),
                        "frame_ms": round(mean_ms, 2),
                        "camera_index": args.camera,
                    }
                )
                processed = 0
                process_seconds = 0.0
                window_started = now
                heartbeat_at = now + max(1.0, args.heartbeat_seconds)
    finally:
        capture.release()

    emit(
        {
            "event": "runtime_status",
            "state": "stopped",
            "available": True,
            "camera_available": True,
            "monitoring": False,
            "detail": "视觉感知已停止",
            "camera_index": args.camera,
        }
    )
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    raise SystemExit(main())
