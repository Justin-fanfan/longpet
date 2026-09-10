#!/usr/bin/env python3
"""Evaluate one LongPet detector on the fixed test split.

The script reports product-oriented frame detection statistics in addition to
the standard Ultralytics mAP report produced by verify_tinyissimo_onnx.py. It
also writes annotated test images so baseline and fine-tuned predictions can be
inspected side by side without touching the source videos.
"""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import sys
from collections import defaultdict
from typing import Any

import cv2
import numpy as np


def iou_xyxy(a: list[float], b: list[float]) -> float:
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    inter = max(0.0, ix2 - ix1) * max(0.0, iy2 - iy1)
    area_a = max(0.0, ax2 - ax1) * max(0.0, ay2 - ay1)
    area_b = max(0.0, bx2 - bx1) * max(0.0, by2 - by1)
    union = area_a + area_b - inter
    return inter / union if union > 0 else 0.0


def read_yolo_boxes(label_path: pathlib.Path, width: int, height: int) -> list[list[float]]:
    if not label_path.exists() or not label_path.is_file():
        return []
    boxes: list[list[float]] = []
    for line in label_path.read_text(encoding="utf-8").splitlines():
        values = line.split()
        if len(values) < 5:
            continue
        _, xc, yc, bw, bh = map(float, values[:5])
        boxes.append([
            (xc - bw / 2) * width,
            (yc - bh / 2) * height,
            (xc + bw / 2) * width,
            (yc + bh / 2) * height,
        ])
    return boxes


def annotate(
    image: np.ndarray,
    gt_boxes: list[list[float]],
    pred_boxes: list[list[float]],
    pred_scores: list[float],
    title: str,
) -> np.ndarray:
    canvas = image.copy()
    for x1, y1, x2, y2 in gt_boxes:
        cv2.rectangle(canvas, (round(x1), round(y1)), (round(x2), round(y2)), (0, 210, 0), 4)
        cv2.putText(canvas, "GT person", (round(x1), max(24, round(y1) - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 180, 0), 2, cv2.LINE_AA)
    for box, score in zip(pred_boxes, pred_scores):
        x1, y1, x2, y2 = box
        cv2.rectangle(canvas, (round(x1), round(y1)), (round(x2), round(y2)), (0, 70, 255), 4)
        cv2.putText(canvas, f"person {score:.2f}", (round(x1), min(canvas.shape[0] - 8, round(y2) + 24)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 40, 220), 2, cv2.LINE_AA)
    cv2.rectangle(canvas, (0, 0), (canvas.shape[1], 44), (20, 20, 20), -1)
    cv2.putText(canvas, title, (12, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.85,
                (255, 255, 255), 2, cv2.LINE_AA)
    return canvas


def load_model(model_path: pathlib.Path, upstream: pathlib.Path, device: str):
    sys.path.insert(0, str(upstream.resolve()))
    import torch  # type: ignore

    original_load = torch.load

    def trusted_load(*args, **kwargs):
        kwargs.setdefault("weights_only", False)
        return original_load(*args, **kwargs)

    torch.load = trusted_load
    from ultralytics import YOLO  # type: ignore
    return YOLO(str(model_path.resolve())), torch


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--model", required=True, type=pathlib.Path)
    parser.add_argument("--name", required=True, help="baseline or finetuned")
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--imgsz", type=int, default=128)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--score-threshold", type=float, default=0.25)
    parser.add_argument("--iou-threshold", type=float, default=0.50)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    rows = [
        r for r in manifest["frames"]
        if r.get("split") == "test"
        and r.get("output_image")
        and r.get("teacher_status") in {"positive", "manual_positive", "negative"}
    ]
    if not rows:
        raise RuntimeError("manifest has no materialized test frames")
    model, torch = load_model(args.model, args.upstream, args.device)
    output = args.output_dir.resolve() / args.name
    image_output = output / "test"
    image_output.mkdir(parents=True, exist_ok=True)

    per_video: dict[str, dict[str, Any]] = defaultdict(lambda: {
        "images": 0, "positive_images": 0, "detected_images": 0,
        "any_prediction_images": 0, "boxes": 0, "best_iou_sum": 0.0,
        "low_conf_images": 0, "missed_images": 0, "negative_images": 0,
        "false_positive_images": 0,
    })
    frame_rows: list[dict[str, Any]] = []
    use_half = str(args.device).lower() not in {"cpu", "mps"}
    for row in rows:
        image_path = pathlib.Path(row["output_image"])
        image = cv2.imread(str(image_path))
        if image is None:
            raise RuntimeError(f"cannot read {image_path}")
        height, width = image.shape[:2]
        label_path = pathlib.Path(row.get("output_label", ""))
        gt_boxes = read_yolo_boxes(label_path, width, height)
        result = model.predict(
            source=str(image_path), imgsz=args.imgsz, conf=0.001, iou=0.7,
            classes=[0], batch=1, device=args.device, half=use_half, verbose=False,
        )[0]
        boxes_obj = getattr(result, "boxes", None)
        pred_boxes = boxes_obj.xyxy.detach().cpu().numpy().tolist() if boxes_obj is not None else []
        pred_scores = boxes_obj.conf.detach().cpu().numpy().tolist() if boxes_obj is not None else []
        selected = [(b, float(s)) for b, s in zip(pred_boxes, pred_scores) if float(s) >= args.score_threshold]
        selected_boxes = [b for b, _ in selected]
        selected_scores = [s for _, s in selected]
        source = row.get("video", image_path.name)
        agg = per_video[source]
        agg["images"] += 1
        agg["boxes"] += len(selected)
        if gt_boxes:
            agg["positive_images"] += 1
            best_iou = max((iou_xyxy(gt, pred) for gt in gt_boxes for pred in selected_boxes), default=0.0)
            detected = best_iou >= args.iou_threshold
            any_prediction = bool(selected_boxes)
            agg["best_iou_sum"] += best_iou
            agg["detected_images"] += int(detected)
            agg["any_prediction_images"] += int(any_prediction)
            agg["low_conf_images"] += int(not any_prediction)
            agg["missed_images"] += int(not detected)
            category = "correct" if detected else ("low_conf" if not any_prediction else "wrong_box")
        else:
            agg["negative_images"] += 1
            false_positive = bool(selected_boxes)
            agg["false_positive_images"] += int(false_positive)
            category = "negative_false_positive" if false_positive else "negative_clean"
            best_iou = 0.0
        annotated = annotate(image, gt_boxes, selected_boxes, selected_scores,
                             f"{args.name} | {source} | {category}")
        cv2.imwrite(str(image_output / image_path.name), annotated, [int(cv2.IMWRITE_JPEG_QUALITY), 92])
        frame_rows.append({
            "split": "test", "source": source, "image": str(image_path),
            "category": category, "gt_boxes": len(gt_boxes),
            "pred_boxes_at_threshold": len(selected_boxes),
            "max_score": max(pred_scores, default=0.0), "best_iou": best_iou,
        })
        if str(args.device).lower() not in {"cpu", "mps"} and len(frame_rows) % 20 == 0:
            torch.cuda.empty_cache()

    video_rows: list[dict[str, Any]] = []
    for source, agg in sorted(per_video.items()):
        positive_images = agg["positive_images"]
        negative_images = agg["negative_images"]
        video_rows.append({
            "source": source,
            **agg,
            "positive_frame_detection_rate": (
                agg["detected_images"] / positive_images if positive_images else None
            ),
            "positive_any_prediction_rate": (
                agg["any_prediction_images"] / positive_images if positive_images else None
            ),
            "mean_best_iou": agg["best_iou_sum"] / positive_images if positive_images else None,
            "negative_false_positive_rate": (
                agg["false_positive_images"] / negative_images if negative_images else None
            ),
        })
    total = {k: sum(int(row[k]) for row in video_rows) for k in (
        "images", "positive_images", "detected_images", "any_prediction_images",
        "boxes", "low_conf_images", "missed_images", "negative_images", "false_positive_images",
    )}
    total_iou = sum(float(row["best_iou_sum"]) for row in video_rows)
    summary = {
        "name": args.name,
        "model": str(args.model.resolve()),
        "split": "test",
        "images": len(frame_rows),
        "positive_images": total["positive_images"],
        "negative_images": total["negative_images"],
        "positive_frame_detection_rate": total["detected_images"] / total["positive_images"] if total["positive_images"] else None,
        "positive_any_prediction_rate": total["any_prediction_images"] / total["positive_images"] if total["positive_images"] else None,
        "negative_false_positive_rate": total["false_positive_images"] / total["negative_images"] if total["negative_images"] else None,
        "mean_best_iou": total_iou / total["positive_images"] if total["positive_images"] else None,
        "score_threshold": args.score_threshold,
        "iou_threshold": args.iou_threshold,
        "note": "negative false-positive rate is undefined because this test split contains zero confirmed negative images",
        "per_video": video_rows,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with (output / "per_video.csv").open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(video_rows[0].keys()) if video_rows else ["source"])
        writer.writeheader(); writer.writerows(video_rows)
    with (output / "per_frame.csv").open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(frame_rows[0].keys()))
        writer.writeheader(); writer.writerows(frame_rows)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
