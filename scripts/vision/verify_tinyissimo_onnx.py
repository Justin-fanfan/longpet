#!/usr/bin/env python3
"""Compare Tinyissimo PyTorch and ONNX raw outputs on real images."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys


def letterbox(image, size: int):
    import cv2
    import numpy as np

    height, width = image.shape[:2]
    scale = min(size / width, size / height)
    resized_width = round(width * scale)
    resized_height = round(height * scale)
    resized = cv2.resize(
        image, (resized_width, resized_height),
        interpolation=cv2.INTER_AREA if scale < 1.0 else cv2.INTER_LINEAR,
    )
    half_width = (size - resized_width) / 2
    half_height = (size - resized_height) / 2
    left, right = round(half_width - 0.1), round(half_width + 0.1)
    top, bottom = round(half_height - 0.1), round(half_height + 0.1)
    padded = cv2.copyMakeBorder(
        resized, top, bottom, left, right, cv2.BORDER_CONSTANT,
        value=(114, 114, 114),
    )
    rgb = cv2.cvtColor(padded, cv2.COLOR_BGR2RGB)
    return np.ascontiguousarray(rgb.transpose(2, 0, 1)[None], dtype=np.float32) / 255.0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=pathlib.Path)
    parser.add_argument("--checkpoint", required=True, type=pathlib.Path)
    parser.add_argument("--onnx", required=True, type=pathlib.Path)
    parser.add_argument("--images", required=True, type=pathlib.Path)
    parser.add_argument("--data", type=pathlib.Path,
                        help="optional dataset YAML for PyTorch/ONNX mAP comparison")
    parser.add_argument("--limit", type=int, default=50)
    parser.add_argument("--atol", type=float, default=1e-4)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.upstream.resolve()))

    import cv2
    import numpy as np
    import onnxruntime as ort
    import torch
    if not hasattr(np, "trapz"):
        np.trapz = np.trapezoid
    original_torch_load = torch.load
    def trusted_local_checkpoint_load(*load_args, **load_kwargs):
        load_kwargs.setdefault("weights_only", False)
        return original_torch_load(*load_args, **load_kwargs)
    torch.load = trusted_local_checkpoint_load
    from ultralytics import YOLO

    session = ort.InferenceSession(
        str(args.onnx.resolve()), providers=["CPUExecutionProvider"]
    )
    input_info = session.get_inputs()[0]
    if len(input_info.shape) != 4 or input_info.shape[2] != input_info.shape[3]:
        raise RuntimeError(f"unexpected input shape: {input_info.shape}")
    size = int(input_info.shape[2])
    pytorch_yolo = YOLO(str(args.checkpoint.resolve()))
    model = pytorch_yolo.model.eval()
    paths = sorted(
        path for path in args.images.rglob("*")
        if path.suffix.lower() in {".jpg", ".jpeg", ".png"}
    )[: args.limit]
    if not paths:
        raise RuntimeError(f"no images under {args.images}")

    rows: list[dict] = []
    for path in paths:
        image = cv2.imread(str(path))
        if image is None:
            raise RuntimeError(f"cannot read {path}")
        tensor = letterbox(image, size)
        with torch.no_grad():
            torch_result = model(torch.from_numpy(tensor))
        torch_output = torch_result[0] if isinstance(torch_result, tuple) else torch_result
        expected = torch_output.detach().cpu().numpy()
        actual = session.run(None, {input_info.name: tensor})[0]
        difference = np.abs(expected - actual)
        rows.append(
            {
                "image": str(path),
                "max_abs_diff": float(difference.max()),
                "mean_abs_diff": float(difference.mean()),
                "pytorch_candidates_above_025": int((expected[:, 4, :] >= 0.25).sum()),
                "onnx_candidates_above_025": int((actual[:, 4, :] >= 0.25).sum()),
            }
        )
    result = {
        "images": len(rows),
        "max_abs_diff": max(row["max_abs_diff"] for row in rows),
        "mean_abs_diff": sum(row["mean_abs_diff"] for row in rows) / len(rows),
        "atol": args.atol,
        "passed": all(row["max_abs_diff"] <= args.atol for row in rows),
        "details": rows,
    }
    if args.data:
        pytorch_metrics = pytorch_yolo.val(
            data=str(args.data.resolve()), imgsz=size, batch=64,
            device="cpu", plots=False, verbose=False,
        )
        onnx_metrics = YOLO(str(args.onnx.resolve())).val(
            data=str(args.data.resolve()), imgsz=size, batch=1,
            device="cpu", plots=False, verbose=False,
        )
        result["validation"] = {
            "pytorch": {
                "precision": float(pytorch_metrics.box.mp),
                "recall": float(pytorch_metrics.box.mr),
                "map50": float(pytorch_metrics.box.map50),
                "map50_95": float(pytorch_metrics.box.map),
            },
            "onnxruntime": {
                "precision": float(onnx_metrics.box.mp),
                "recall": float(onnx_metrics.box.mr),
                "map50": float(onnx_metrics.box.map50),
                "map50_95": float(onnx_metrics.box.map),
            },
        }
    text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if result["passed"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
