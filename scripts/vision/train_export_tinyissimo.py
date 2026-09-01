#!/usr/bin/env python3
"""Train TinyissimoYOLO v1-small person-only and export checked FP32 ONNX."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def static_shape(value_info) -> list[int]:
    result: list[int] = []
    for dimension in value_info.type.tensor_type.shape.dim:
        if not dimension.HasField("dim_value"):
            raise RuntimeError(f"dynamic dimension in {value_info.name}")
        result.append(int(dimension.dim_value))
    return result


def add_metadata(model, values: dict[str, str]) -> None:
    existing = {item.key: item.value for item in model.metadata_props}
    existing.update(values)
    del model.metadata_props[:]
    for key in sorted(existing):
        item = model.metadata_props.add()
        item.key = key
        item.value = existing[key]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=pathlib.Path)
    parser.add_argument("--data", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--model-output", required=True, type=pathlib.Path)
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--batch", type=int, default=64)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--imgsz", type=int, default=128)
    parser.add_argument("--seed", type=int, default=20260901)
    parser.add_argument("--patience", type=int, default=25)
    parser.add_argument("--name", default="tinyissimo-v1-small-person-128")
    args = parser.parse_args()
    if args.imgsz % 32 != 0:
        parser.error(
            "v1-small stride is 32; use a multiple of 32. "
            "The official exporter rounds 112 to 128."
        )

    upstream = args.upstream.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(upstream))
    os.environ.setdefault("YOLO_CONFIG_DIR", str(output_dir / "ultralytics-config"))

    import numpy as np
    import onnx
    import onnxruntime as ort
    import torch
    import yaml
    if not hasattr(np, "trapz"):
        # The pinned fork calls the pre-NumPy-2.4 spelling during validation.
        np.trapz = np.trapezoid
    # The pinned 2025 fork predates Torch 2.6's weights_only=True default and
    # stores its locally generated DetectionModel object in checkpoints.
    original_torch_load = torch.load
    def trusted_local_checkpoint_load(*load_args, **load_kwargs):
        load_kwargs.setdefault("weights_only", False)
        return original_torch_load(*load_args, **load_kwargs)
    torch.load = trusted_local_checkpoint_load
    from ultralytics import YOLO

    architecture_path = (
        upstream
        / "ultralytics"
        / "cfg"
        / "models"
        / "tinyissimo"
        / "tinyissimo-v1-small.yaml"
    )
    architecture = yaml.safe_load(architecture_path.read_text(encoding="utf-8"))
    architecture["nc"] = 1
    derived_architecture = output_dir / "tinyissimo-v1-small-person.yaml"
    derived_architecture.write_text(
        yaml.safe_dump(architecture, sort_keys=False), encoding="utf-8"
    )

    model = YOLO(str(derived_architecture))
    detect_head = model.model.model[-1]
    if getattr(detect_head, "reg_max", None) != 16:
        raise RuntimeError(
            "Tinyissimo v1 requires Detect.reg_max=16; run "
            "setup_tinyissimo_upstream.py first"
        )
    if list(map(float, model.model.stride)) != [32.0]:
        raise RuntimeError(f"unexpected model stride: {model.model.stride}")
    parameter_count = sum(parameter.numel() for parameter in model.model.parameters())

    started = time.time()
    model.train(
        data=str(args.data.resolve()),
        imgsz=args.imgsz,
        epochs=args.epochs,
        batch=args.batch,
        workers=args.workers,
        device=args.device,
        project=str(output_dir / "runs"),
        name=args.name,
        exist_ok=False,
        optimizer="SGD",
        seed=args.seed,
        deterministic=True,
        patience=args.patience,
        pretrained=False,
        plots=True,
        verbose=True,
    )
    run_dir = pathlib.Path(model.trainer.save_dir)
    best_checkpoint = run_dir / "weights" / "best.pt"
    if not best_checkpoint.exists():
        raise RuntimeError(f"training did not produce {best_checkpoint}")

    trained = YOLO(str(best_checkpoint))
    exported = pathlib.Path(
        trained.export(
            format="onnx",
            imgsz=args.imgsz,
            dynamic=False,
            simplify=False,
            opset=17,
            device="cpu",
        )
    ).resolve()
    if not exported.exists():
        raise RuntimeError(f"export did not produce {exported}")

    onnx_model = onnx.load(str(exported))
    onnx.checker.check_model(onnx_model)
    input_shape = static_shape(onnx_model.graph.input[0])
    output_shape = static_shape(onnx_model.graph.output[0])
    expected_candidates = (args.imgsz // 32) ** 2
    if input_shape != [1, 3, args.imgsz, args.imgsz]:
        raise RuntimeError(f"unexpected ONNX input: {input_shape}")
    if output_shape not in ([1, 5, expected_candidates],
                            [1, expected_candidates, 5]):
        raise RuntimeError(f"unexpected ONNX output: {output_shape}")
    if ([(item.domain, item.version) for item in onnx_model.opset_import]
            != [("", 17)]):
        raise RuntimeError("export is not a plain ONNX opset-17 graph")

    manifest_path = args.data.resolve().parent / "manifest.json"
    metadata = {
        "longpet.detector": "tinyissimo-yolo-v1-small-person",
        "longpet.input_size": str(args.imgsz),
        "longpet.parameter_count": str(parameter_count),
        "longpet.upstream_commit": subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=upstream,
            check=True, capture_output=True, text=True,
        ).stdout.strip(),
        "longpet.dataset_manifest_sha256": (
            sha256(manifest_path) if manifest_path.exists() else "unavailable"
        ),
    }
    add_metadata(onnx_model, metadata)
    args.model_output.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(onnx_model, str(args.model_output))

    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    session = ort.InferenceSession(
        str(args.model_output), sess_options=options,
        providers=["CPUExecutionProvider"],
    )
    dummy = np.zeros(input_shape, dtype=np.float32)
    output = session.run(None, {session.get_inputs()[0].name: dummy})[0]
    if list(output.shape) != output_shape or not np.isfinite(output).all():
        raise RuntimeError("ONNX Runtime smoke test returned invalid output")

    result = {
        "model": str(args.model_output.resolve()),
        "sha256": sha256(args.model_output),
        "bytes": args.model_output.stat().st_size,
        "parameter_count": parameter_count,
        "input_shape": input_shape,
        "output_shape": output_shape,
        "best_checkpoint": str(best_checkpoint),
        "run_dir": str(run_dir),
        "training_seconds": time.time() - started,
        "onnxruntime_version": ort.__version__,
        "torch_version": torch.__version__,
    }
    result_path = args.model_output.with_suffix(".training.json")
    result_path.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
