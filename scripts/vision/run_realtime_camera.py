#!/usr/bin/env python3
"""Run a YOLO detector on webcam and show boxes with terminal stats.

Usage example:
python scripts/vision/run_realtime_camera.py `
--upstream third_party/tinyissimo-yolo `
--model "D:/LongPet-Vision-Final-Handoff/03_models/V1.2_FINAL/tinyissimo-person-128-longpet-v1.onnx" `
--backend onnx --device cpu --imgsz 128 --conf 0.25 --nms 0.45 --camera 0 --print-interval 10
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

import cv2


def load_model_pt(model_path: pathlib.Path, upstream: pathlib.Path, device: str):
    # Load a PyTorch checkpoint via Ultralytics YOLO (requires ultralytics installed)
    sys.path.insert(0, str(upstream.resolve()))
    import torch  # type: ignore

    original_load = torch.load

    def trusted_load(*args, **kwargs):
        kwargs.setdefault("weights_only", False)
        return original_load(*args, **kwargs)

    torch.load = trusted_load
    from ultralytics import YOLO  # type: ignore
    return YOLO(str(model_path.resolve())), torch


def load_model_onnx(model_path: pathlib.Path, upstream: pathlib.Path, device: str):
    # Ultralytics supports loading ONNX via YOLO(...) too; keep upstream in sys.path
    sys.path.insert(0, str(upstream.resolve()))
    from ultralytics import YOLO  # type: ignore
    return YOLO(str(model_path.resolve())), None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=pathlib.Path)
    parser.add_argument("--model", required=True, type=pathlib.Path)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--imgsz", type=int, default=128)
    parser.add_argument("--conf", type=float, default=0.25)
    parser.add_argument("--nms", type=float, default=0.45)
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--backend", choices=("pt", "onnx"), default="pt",
                        help="Inference backend: 'pt' for PyTorch checkpoint, 'onnx' for ONNX model")
    parser.add_argument("--print-interval", type=int, default=5,
                        help="How many frames between terminal prints (0=every frame)")
    args = parser.parse_args()
    backend = args.backend.lower()
    if backend == "pt":
        model, torch = load_model_pt(args.model, args.upstream, args.device)
        use_half = str(args.device).lower() not in {"cpu", "mps"}
    else:
        model, torch = load_model_onnx(args.model, args.upstream, args.device)
        use_half = False

    cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        print(f"cannot open camera {args.camera}")
        return 2

    # ensure a resizable window (opencv headless can't show windows)
    try:
        cv2.namedWindow("LongPet Realtime", cv2.WINDOW_NORMAL)
    except Exception:
        pass

    frame_index = 0
    last_time = time.perf_counter()
    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                print("camera frame read failed")
                break
            frame_index += 1
            t0 = time.perf_counter()
            # ultralytics accepts numpy frames directly
            result = model.predict(source=frame, imgsz=args.imgsz,
                                   conf=0.001, iou=args.nms,
                                   classes=[0], batch=1,
                                   device=args.device, half=use_half,
                                   verbose=False)[0]
            t1 = time.perf_counter()
            boxes_obj = getattr(result, "boxes", None)
            if boxes_obj is not None:
                try:
                    pred_boxes = boxes_obj.xyxy.detach().cpu().numpy().tolist()
                    pred_scores = boxes_obj.conf.detach().cpu().numpy().tolist()
                except Exception:
                    pred_boxes = boxes_obj.xyxy.numpy().tolist()
                    pred_scores = boxes_obj.conf.numpy().tolist()
            else:
                pred_boxes = []
                pred_scores = []

            # draw
            canvas = frame.copy()
            for box, score in zip(pred_boxes, pred_scores):
                x1, y1, x2, y2 = map(int, box)
                cv2.rectangle(canvas, (x1, y1), (x2, y2), (0, 70, 255), 2)
                cv2.putText(canvas, f"person {score:.2f}", (x1, max(16, y1 - 6)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 40, 220), 2, cv2.LINE_AA)

            infer_ms = (t1 - t0) * 1000.0
            person_count = len(pred_boxes)
            # overlay stats on image
            now = time.perf_counter()
            fps = 1.0 / (now - last_time) if now > last_time else 0.0
            last_time = now
            stats = f"frame={frame_index} fps={fps:.1f} infer_ms={infer_ms:.1f} persons={person_count}"
            cv2.putText(canvas, stats, (12, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (255, 255, 255), 2, cv2.LINE_AA)
            if args.print_interval <= 0 or (frame_index % args.print_interval) == 0:
                print(stats)

            cv2.imshow("LongPet Realtime", canvas)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
