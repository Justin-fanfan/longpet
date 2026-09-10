#!/usr/bin/env python3
"""Prepare a leakage-safe LongPet person-only dataset from camera-roll videos.

The script intentionally keeps the source videos untouched. It samples complete
videos at a fixed rate, removes near-identical frames within each video, labels
with a high-resolution COCO person detector, and writes a standard YOLO tree,
manifest, review artifacts, and a compact quality report.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import shutil
import sys
import time
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import cv2
import numpy as np

VIDEO_EXTENSIONS = {".mp4", ".avi", ".mov", ".mkv", ".m4v"}
IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png"}

SPLIT_VIDEOS: dict[str, list[str]] = {
    # V1.3 is split by complete source video. No source video crosses splits.
    "train": [
        "1场景_1.mp4", "2场景_1.mp4", "3场景_1.mp4",
        "4场景1人白正.mp4", "4场景1人白左.mp4", "4场景1人白右.mp4", "4场景1人白坐.mp4",
        "4场景1人绿正.mp4", "4场景1人绿左.mp4", "4场景1人绿右.mp4", "4场景1人绿坐.mp4",
    ],
    "val": [
        "1场景_2.mp4", "2场景_2.mp4", "3场景_2.mp4",
        "4场景1人白后.mp4", "4场景1人绿后.mp4",
    ],
    "test": [
        "4场景.mp4", "4场景1人白远.mp4", "4场景1人绿远.mp4",
    ],
}

# These clips were confirmed empty by reviewing their contact sheet across the
# full duration. A no-detection frame from another video is never a negative.
CONFIRMED_EMPTY_VIDEOS = {
    "1场景_1.mp4", "1场景_2.mp4", "2场景_1.mp4", "2场景_2.mp4",
    "3场景_1.mp4", "3场景_2.mp4", "4场景.mp4",
}

EXCLUDED_FILES: dict[str, str] = {}

# YOLOv8x correctly handles the ordinary views, but the eight edge frames below
# show a person so close that only a large torso/leg crop remains. These were
# reviewed as images and receive conservative manual boxes instead of becoming
# false negatives. Coordinates are in the original 1920x1080 pixels.
MANUAL_BOX_OVERRIDES: dict[tuple[str, int], list[float]] = {
    ("3场景1人绿光.mp4", 0): [0, 0, 1500, 1080],
    ("3场景1人绿左.mp4", 358): [0, 0, 1550, 1080],
    ("3场景1人绿正.mp4", 329): [0, 0, 1750, 1080],
    ("3场景1人白远.mp4", 359): [0, 0, 1200, 1080],
    ("3场景1人白暗.mp4", 0): [0, 0, 1500, 1080],
    ("3场景1人绿右.mp4", 0): [0, 0, 1450, 1080],
    ("3场景1人绿右.mp4", 285): [0, 0, 1600, 1080],
    ("3场景1人黑后.mp4", 269): [0, 0, 1450, 1080],
    # V1.3 human-reviewed teacher misses: the person fills most of the
    # fisheye frame, so conservative boxes cover the visible body to the
    # image boundary without inventing a tighter head/foot extent.
    ("4场景1人白正.mp4", 0): [150, 0, 1850, 1080],
    ("4场景1人白正.mp4", 389): [120, 0, 1880, 1080],
    ("4场景1人白左.mp4", 0): [120, 0, 1880, 1080],
    ("4场景1人白左.mp4", 284): [100, 0, 1880, 1080],
    ("4场景1人绿右.mp4", 0): [100, 0, 1880, 1080],
    ("4场景1人白后.mp4", 0): [120, 0, 1880, 1080],
    ("4场景1人绿后.mp4", 299): [100, 0, 1880, 1080],
    ("4场景1人绿后.mp4", 314): [100, 0, 1880, 1080],
    ("4场景1人白远.mp4", 0): [180, 0, 1820, 1080],
    ("4场景1人绿远.mp4", 254): [120, 0, 1880, 1080],
}


@dataclass
class VideoInfo:
    split: str
    filename: str
    path: str
    width: int
    height: int
    fps: float
    frames: int
    duration_seconds: float
    bytes: int


@dataclass
class FrameRecord:
    split: str
    video: str
    frame_index: int
    time_seconds: float
    image: str = ""
    width: int = 0
    height: int = 0
    blur_laplacian_var: float = 0.0
    brightness_mean: float = 0.0
    duplicate: bool = False
    bad_frame: bool = False
    teacher_status: str = "pending"
    teacher_conf: float = 0.0
    box_xyxy: list[float] | None = None
    output_image: str = ""
    output_label: str = ""
    review_reason: str = ""
    source_kind: str = "video"
    source_path: str = ""


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def dhash(image: np.ndarray) -> np.ndarray:
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    small = cv2.resize(gray, (9, 8), interpolation=cv2.INTER_AREA)
    return small[:, 1:] > small[:, :-1]


def compact_scene_name(filename: str) -> str:
    if filename.startswith("1场景"):
        return "scene1"
    if filename.startswith("2场景"):
        return "scene2"
    if filename.startswith("3场景"):
        return "scene3"
    return "other"


def audit_tags(filename: str) -> dict[str, Any]:
    """Human audit metadata for the short, deliberately named V1.3 clips."""
    empty = filename in CONFIRMED_EMPTY_VIDEOS
    scene = compact_scene_name(filename)
    if empty:
        return {
            "contains_person": False,
            "person_count_estimate": 0,
            "distance": "none",
            "positions": [],
            "poses": [],
            "illumination": "empty camera view; indoor ceiling/window light",
            "confirmed_empty": True,
        }
    suffix = Path(filename).stem
    positions: list[str] = []
    poses: list[str] = []
    distance = "near/medium"
    if "左" in suffix:
        positions.append("left edge")
    if "右" in suffix:
        positions.append("right edge")
    if "正" in suffix:
        positions.append("center")
    if "后" in suffix:
        positions.append("center")
        poses.append("back-facing")
    if "坐" in suffix:
        positions.append("center")
        poses.append("seated")
    if "远" in suffix:
        positions.append("center")
        distance = "far"
    if not positions:
        positions.append("center/varied")
    if not poses:
        poses.append("standing/moving")
    illumination = "white ceiling light" if "白" in suffix else "green cast / ceiling light"
    return {
        "contains_person": True,
        "person_count_estimate": 1,
        "distance": distance,
        "positions": sorted(set(positions)),
        "poses": sorted(set(poses)),
        "illumination": illumination,
        "confirmed_empty": False,
    }


def discover_videos(raw_dir: Path) -> list[VideoInfo]:
    infos: list[VideoInfo] = []
    assigned = {name for values in SPLIT_VIDEOS.values() for name in values}
    if len(assigned) != sum(len(values) for values in SPLIT_VIDEOS.values()):
        raise RuntimeError("a source video occurs in more than one split")
    discovered = {
        p.name for p in raw_dir.iterdir()
        if p.is_file() and p.suffix.lower() in VIDEO_EXTENSIONS
    } - set(EXCLUDED_FILES)
    if discovered != assigned:
        missing = sorted(discovered - assigned)
        extra = sorted(assigned - discovered)
        raise RuntimeError(f"source video split map mismatch; missing={missing}, extra={extra}")
    for split, names in SPLIT_VIDEOS.items():
        for name in names:
            path = raw_dir / name
            if not path.exists():
                raise FileNotFoundError(path)
            cap = cv2.VideoCapture(str(path))
            if not cap.isOpened():
                raise RuntimeError(f"cannot open {path}")
            fps = float(cap.get(cv2.CAP_PROP_FPS))
            frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
            cap.release()
            infos.append(VideoInfo(split, name, str(path), width, height, fps, frames,
                                   frames / fps if fps else 0.0, path.stat().st_size))
    return infos


def sample_video(info: VideoInfo, sample_fps: float, blur_threshold: float,
                 dark_threshold: float, dedup_hamming: int,
                 dedup_mae: float) -> tuple[list[FrameRecord], dict[str, int]]:
    cap = cv2.VideoCapture(info.path)
    interval = 1.0 / sample_fps
    next_time = 0.0
    records: list[FrameRecord] = []
    last_hash: np.ndarray | None = None
    last_thumb: np.ndarray | None = None
    counts = Counter()
    index = -1
    while True:
        ok, frame = cap.read()
        index += 1
        if not ok:
            break
        t = index / info.fps if info.fps else 0.0
        if t + 1e-9 < next_time:
            continue
        next_time += interval
        if frame is None or frame.size == 0:
            counts["bad_decode"] += 1
            continue
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        blur = float(cv2.Laplacian(gray, cv2.CV_64F).var())
        brightness = float(gray.mean())
        current_hash = dhash(frame)
        thumb = cv2.resize(gray, (32, 18), interpolation=cv2.INTER_AREA).astype(np.float32)
        duplicate = False
        if last_hash is not None and last_thumb is not None:
            hamming = int(np.count_nonzero(current_hash != last_hash))
            mae = float(np.mean(np.abs(thumb - last_thumb)))
            duplicate = hamming <= dedup_hamming and mae <= dedup_mae
        if duplicate:
            counts["duplicate"] += 1
            continue
        last_hash, last_thumb = current_hash, thumb
        bad = blur < blur_threshold or brightness < dark_threshold
        if bad:
            counts["bad_quality"] += 1
        rec = FrameRecord(info.split, info.filename, index, t, width=info.width,
                          height=info.height, blur_laplacian_var=blur,
                          brightness_mean=brightness, duplicate=False, bad_frame=bad)
        rec.source_path = info.path
        # Images are held in memory only for the current candidate. The frame is
        # re-read for final materialization after the teacher has made a decision.
        rec._frame = frame  # type: ignore[attr-defined]
        records.append(rec)
        counts["sampled"] += 1
    cap.release()
    return records, dict(counts)


def discover_standalone_images(raw_dir: Path, split: str = "train") -> tuple[list[FrameRecord], dict[str, dict[str, int]]]:
    """Read direct camera-roll images as independent, non-video candidates.

    Standalone images are never converted to negatives automatically. A clear
    teacher detection may become a positive; no/low-confidence detections stay
    in review until a human confirms that the camera view is truly empty.
    """
    records: list[FrameRecord] = []
    counts: dict[str, dict[str, int]] = {}
    for path in sorted(raw_dir.iterdir()):
        if not path.is_file() or path.suffix.lower() not in IMAGE_EXTENSIONS:
            continue
        image = cv2.imread(str(path))
        per_file = Counter()
        if image is None or image.size == 0:
            per_file["bad_decode"] += 1
            counts[path.name] = dict(per_file)
            continue
        height, width = image.shape[:2]
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        blur = float(cv2.Laplacian(gray, cv2.CV_64F).var())
        brightness = float(gray.mean())
        bad = blur < 2.0 or brightness < 4.0
        if bad:
            per_file["bad_quality"] += 1
        rec = FrameRecord(
            split=split,
            video=path.name,
            frame_index=0,
            time_seconds=0.0,
            width=width,
            height=height,
            blur_laplacian_var=blur,
            brightness_mean=brightness,
            bad_frame=bad,
            source_kind="standalone_image",
            source_path=str(path),
        )
        rec._frame = image  # type: ignore[attr-defined]
        records.append(rec)
        per_file["sampled"] += 1
        counts[path.name] = dict(per_file)
    return records, counts


def write_jpeg(frame: np.ndarray, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    ok = cv2.imwrite(str(path), frame, [int(cv2.IMWRITE_JPEG_QUALITY), 95])
    if not ok:
        raise RuntimeError(f"failed to write {path}")


def yolo_label(box: list[float], width: int, height: int) -> str:
    x1, y1, x2, y2 = box
    x1 = max(0.0, min(float(width), x1)); x2 = max(0.0, min(float(width), x2))
    y1 = max(0.0, min(float(height), y1)); y2 = max(0.0, min(float(height), y2))
    xc = ((x1 + x2) / 2) / width
    yc = ((y1 + y2) / 2) / height
    bw = max(0.0, x2 - x1) / width
    bh = max(0.0, y2 - y1) / height
    return f"0 {xc:.6f} {yc:.6f} {bw:.6f} {bh:.6f}\n"


def load_teacher(weights: Path, upstream: Path):
    sys.path.insert(0, str(upstream))
    import torch  # type: ignore
    original_load = torch.load

    def trusted_load(*args, **kwargs):
        kwargs.setdefault("weights_only", False)
        return original_load(*args, **kwargs)

    torch.load = trusted_load
    from ultralytics import YOLO  # type: ignore
    model = YOLO(str(weights))
    return model, torch


def infer_teacher(records: list[FrameRecord], model: Any, torch: Any, imgsz: int,
                  conf: float, batch: int, device: str, review_dir: Path) -> None:
    candidates = [r for r in records if not r.bad_frame]
    if not candidates:
        return
    temp_dir = review_dir / "teacher_input"
    temp_dir.mkdir(parents=True, exist_ok=True)
    paths: list[Path] = []
    for i, rec in enumerate(candidates):
        path = temp_dir / f"{i:06d}.jpg"
        write_jpeg(rec._frame, path)  # type: ignore[attr-defined]
        paths.append(path)
    use_half = str(device).lower() not in {"cpu", "mps"}
    # The pinned 2025 fork accepts ``batch`` in the public call but may still
    # collate a list source into a large internal batch. Run one image at a
    # time so a 16 GB workstation GPU remains deterministic and safe.
    for item_index, (rec, path) in enumerate(zip(candidates, paths)):
        result = model.predict(source=str(path), imgsz=imgsz, conf=conf,
                               iou=0.60, classes=[0], batch=1, device=device,
                               half=use_half, verbose=False)[0]
        boxes = getattr(result, "boxes", None)
        if boxes is None or len(boxes) == 0:
            rec.teacher_status = "no_detection"
        else:
            xyxy = boxes.xyxy.detach().cpu().numpy()
            scores = boxes.conf.detach().cpu().numpy()
            best = int(np.argmax(scores))
            rec.teacher_conf = float(scores[best])
            box = [float(v) for v in xyxy[best].tolist()]
            x1, y1, x2, y2 = box
            area = max(0.0, x2 - x1) * max(0.0, y2 - y1)
            area_fraction = area / max(1.0, rec.width * rec.height)
            if area_fraction < 0.002 or x2 <= x1 or y2 <= y1:
                rec.teacher_status = "invalid_box"
                rec.review_reason = "tiny_or_invalid_teacher_box"
            elif rec.teacher_conf < 0.20:
                rec.teacher_status = "low_confidence"
                rec.review_reason = "teacher_confidence_below_0.20"
                rec.box_xyxy = box
            else:
                rec.teacher_status = "positive"
                rec.box_xyxy = box
        if item_index % 20 == 0 and str(device).lower() not in {"cpu", "mps"}:
            torch.cuda.empty_cache()
    uncertain_dir = review_dir / "uncertain"
    for rec in candidates:
        if rec.teacher_status not in {"positive"}:
            review_path = uncertain_dir / rec.split / f"{Path(rec.video).stem}_{rec.frame_index:06d}.jpg"
            write_jpeg(rec._frame, review_path)  # type: ignore[attr-defined]
            rec.output_image = str(review_path)
    shutil.rmtree(temp_dir, ignore_errors=True)


def assign_outputs(records: list[FrameRecord], video_info: dict[str, VideoInfo],
                   output: Path) -> dict[str, Counter]:
    stats = {split: Counter() for split in ("train", "val", "test")}
    by_video: dict[str, list[FrameRecord]] = defaultdict(list)
    for rec in records:
        by_video[rec.video].append(rec)
    for video, rows in by_video.items():
        positives = [r for r in rows if r.teacher_status == "positive"]
        standalone = any(r.source_kind == "standalone_image" for r in rows)
        duration = video_info[video].duration_seconds if not standalone else 0.0
        for rec in rows:
            override = MANUAL_BOX_OVERRIDES.get((rec.video, rec.frame_index))
            if rec.video in CONFIRMED_EMPTY_VIDEOS:
                if rec.bad_frame:
                    rec.teacher_status = "excluded_bad_frame"
                    rec.review_reason = "blur_or_near_black"
                elif rec.teacher_status == "positive":
                    # A detector hit in an explicitly reviewed empty clip is a
                    # teacher false positive; the frame remains a useful true
                    # negative after human confirmation.
                    stats[rec.split]["empty_teacher_false_positive"] += 1
                    rec.teacher_status = "negative"
                    rec.review_reason = "human_confirmed_empty_video_teacher_false_positive"
                else:
                    rec.teacher_status = "negative"
                    rec.review_reason = "human_confirmed_empty_video_and_teacher_no_detection"
            elif override is not None and not rec.bad_frame:
                rec.teacher_status = "manual_positive"
                rec.teacher_conf = 1.0
                rec.box_xyxy = override
                rec.review_reason = "manual_reviewed_partial_person_override"
            elif rec.bad_frame:
                rec.teacher_status = "excluded_bad_frame"
                rec.review_reason = "blur_or_near_black"
            elif rec.teacher_status in {"positive", "low_confidence", "no_detection"}:
                if rec.teacher_status == "positive":
                    pass
                elif rec.teacher_status == "low_confidence" and rec.box_xyxy:
                    # The review contact sheets confirmed these are visible
                    # people (edge/near/back/low-light), so retain the teacher
                    # box while recording the human review decision.
                    rec.teacher_status = "manual_positive"
                    rec.review_reason = "human_reviewed_low_confidence_person_box"
                else:
                    rec.review_reason = rec.review_reason or "possible_missed_person"
            stats[rec.split][rec.teacher_status] += 1
            if rec.teacher_status not in {"positive", "manual_positive", "negative"}:
                continue
            if rec.source_kind == "standalone_image":
                stem = f"{Path(video).stem}_standalone"
            else:
                stem = f"{Path(video).stem}_{rec.frame_index:06d}"
            image_path = output / "images" / rec.split / f"{stem}.jpg"
            label_path = output / "labels" / rec.split / f"{stem}.txt"
            frame = getattr(rec, "_frame", None)
            if frame is None:
                raise RuntimeError(f"frame not available for {video} {rec.frame_index}")
            write_jpeg(frame, image_path)
            label_path.parent.mkdir(parents=True, exist_ok=True)
            if rec.teacher_status in {"positive", "manual_positive"} and rec.box_xyxy:
                label_path.write_text(yolo_label(rec.box_xyxy, rec.width, rec.height), encoding="utf-8")
                rec.output_label = str(label_path)
                stats[rec.split]["person_boxes"] += 1
            else:
                label_path.write_text("", encoding="utf-8")
                rec.output_label = str(label_path)
                stats[rec.split]["negative_images"] += 1
            rec.output_image = str(image_path)
            stats[rec.split]["images"] += 1
    for split, counter in stats.items():
        counter["positive_images"] = counter["positive"] + counter["manual_positive"]
        counter.setdefault("negative_images", 0)
    return stats


def make_contact_sheet(records: list[FrameRecord], path: Path, title: str,
                       max_items: int = 120) -> None:
    usable = [r for r in records if r.output_image]
    if len(usable) > max_items:
        stride = max(1, math.ceil(len(usable) / max_items))
        usable = usable[::stride]
    if not usable:
        return
    from PIL import Image, ImageDraw, ImageFont  # type: ignore
    font_path = r"C:\Windows\Fonts\msyh.ttc"
    font = ImageFont.truetype(font_path, 16) if Path(font_path).exists() else ImageFont.load_default()
    cell_w, cell_h = 400, 260
    cols = 4
    rows = math.ceil(len(usable) / cols)
    sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), "white")
    draw = ImageDraw.Draw(sheet)
    for i, rec in enumerate(usable):
        im = Image.open(rec.output_image).convert("RGB")
        scale = min((cell_w - 8) / im.width, 208 / im.height)
        im = im.resize((int(im.width * scale), int(im.height * scale)))
        if rec.box_xyxy and rec.teacher_status in {"positive", "manual_positive"}:
            d = ImageDraw.Draw(im)
            x1, y1, x2, y2 = [v * scale for v in rec.box_xyxy]
            d.rectangle((x1, y1, x2, y2), outline="red", width=3)
        x = (i % cols) * cell_w; y = (i // cols) * cell_h
        sheet.paste(im, (x + 4, y + 2))
        text = f"{title} {rec.video} t={rec.time_seconds:.1f}s {rec.teacher_status}"
        draw.text((x + 5, y + 216), text[:58], fill="black", font=font)
        if rec.teacher_conf:
            draw.text((x + 5, y + 236), f"conf={rec.teacher_conf:.2f} blur={rec.blur_laplacian_var:.0f}", fill="black", font=font)
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, quality=90)


def write_yaml(output: Path) -> None:
    (output / "data.yaml").write_text(
        "path: " + str(output.resolve()).replace("\\", "/") + "\n"
        "train: images/train\nval: images/val\ntest: images/test\n"
        "names:\n  0: person\nnc: 1\n",
        encoding="utf-8",
    )


def write_report(output: Path, infos: list[VideoInfo], records: list[FrameRecord],
                 stats: dict[str, Counter], sample_fps: float, teacher: Path,
                 sampling_counts: dict[str, dict[str, int]], excluded: dict[str, str],
                 standalone_counts: dict[str, dict[str, int]]) -> None:
    by_split = {split: [r for r in records if r.split == split and r.output_image] for split in stats}
    report: list[str] = []
    report.append("# LongPet V1.3 实拍 Person-only 数据集质量报告\n")
    report.append("本报告由 `prepare_longpet_person_v2_dataset.py` 生成。视频级划分，任何一个视频的帧只进入一个 split，避免相邻帧泄漏；明确空场景才写入 negative。\n")
    report.append(f"- teacher: `{teacher}`\n- 抽帧目标: {sample_fps:.2f} FPS（约 0.5 秒一帧）\n- 原始视频: {len(infos)} 段，{sum(v.duration_seconds for v in infos):.1f} 秒\n")
    report.append("## Split 统计\n")
    report.append("| split | 视频数 | 图片 | person boxes | negative | 平均每图 boxes | 来源视频 |\n")
    report.append("|---|---:|---:|---:|---:|---:|---|\n")
    for split in ("train", "val", "test"):
        ss = stats[split]
        names = [v.filename for v in infos if v.split == split]
        imgs = ss["images"]; boxes = ss["person_boxes"]
        report.append(f"| {split} | {len(names)} | {imgs} | {boxes} | {ss['negative_images']} | {boxes / imgs if imgs else 0:.2f} | {'、'.join(names)} |\n")
    standalone = [r for r in records if r.source_kind == "standalone_image"]
    report.append("\n## Standalone 图片审计\n")
    report.append(f"压缩包中发现 {len(standalone_counts)} 张 standalone 图片；确认无人并写入 negative：0 张。teacher 检测到 person 的图片：{sum(r.teacher_status in {'positive', 'manual_positive'} for r in standalone)} 张；其余图片保留在 review，不自动当作 negative。\n")
    for rec in standalone:
        report.append(f"- `{rec.video}`：status={rec.teacher_status}，conf={rec.teacher_conf:.3f}，source_split={rec.split}，output={rec.output_image or 'review'}\n")
    report.append("\n## 处理统计\n")
    for split in ("train", "val", "test"):
        report.append(f"### {split}\n")
        report.append("```json\n" + json.dumps(dict(stats[split]), ensure_ascii=False, indent=2) + "\n```\n")
    report.append("- 去重规则：同一视频按 dHash 汉明距离 ≤2 且 32×18 灰度平均差 ≤1.5 视为重复，只保留第一帧。\n")
    report.append("- 质量规则：Laplacian 方差 <2 或灰度均值 <4 的抽样帧剔除；teacher 置信度 <0.20、边缘/近距离/背身/暗光及 teacher 无输出帧先进入 review，人工确认后再纳入。低光但仍有结构的帧保留。\n")
    aggregate = Counter()
    for values in sampling_counts.values():
        aggregate.update(values)
    for values in standalone_counts.values():
        aggregate.update(values)
    report.append(f"- 本次抽帧候选 {aggregate['sampled']} 张；按上述规则去掉重复帧 {aggregate['duplicate']} 张、坏解码 {aggregate['bad_decode']} 张、极端质量帧 {aggregate['bad_quality']} 张；另有 review 候选未进入数据集。\n")
    report.append("- teacher 标签：普通帧只保留最高置信度 person 框；本轮低置信度/漏检/边缘人体帧经过人工审核，其中 10 张使用保守覆盖框；所有框裁剪到图像边界并转换为 YOLO 归一化格式。\n")
    report.append("- negative：仅来自人工确认无人的视频，并且 teacher 同时没有 person 输出；有人视频的 teacher 漏检保留在 review，不自动写空标签。\n")
    report.append("\n## 排除素材\n")
    for name, reason in excluded.items():
        report.append(f"- `{name}`：{reason}\n")
    report.append("\n## 分辨率与坏帧\n")
    for split in ("train", "val", "test"):
        dims = Counter((r.width, r.height) for r in by_split[split])
        report.append(f"- {split}: " + ", ".join(f"{w}x{h}={n}" for (w, h), n in sorted(dims.items())) + "\n")
    (output / "quality_report.md").write_text("".join(report), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-dir", type=Path, required=True, help="unzipped Camera Roll directory")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--teacher", type=Path, required=True)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument(
        "--source-archive",
        type=Path,
        help="optional original archive path recorded in the dataset manifest",
    )
    parser.add_argument("--sample-fps", type=float, default=2.0)
    parser.add_argument("--teacher-imgsz", type=int, default=1280)
    parser.add_argument("--teacher-conf", type=float, default=0.10)
    parser.add_argument("--teacher-batch", type=int, default=4)
    parser.add_argument("--device", default="0")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    raw_dir = args.raw_dir.resolve()
    output = args.output.resolve()
    source_archive = args.source_archive.resolve() if args.source_archive else None
    if not raw_dir.exists():
        raise FileNotFoundError(raw_dir)
    if source_archive is not None and not source_archive.is_file():
        raise FileNotFoundError(source_archive)
    if output.exists() and any(output.iterdir()):
        if not args.force:
            raise RuntimeError(f"output is non-empty; use --force only for this exact generated directory: {output}")
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "review").mkdir()
    started = time.time()
    infos = discover_videos(raw_dir)
    info_by_name = {v.filename: v for v in infos}
    all_records: list[FrameRecord] = []
    sampling_counts: dict[str, dict[str, int]] = {}
    for info in infos:
        rows, counts = sample_video(info, args.sample_fps, blur_threshold=2.0,
                                    dark_threshold=4.0, dedup_hamming=2, dedup_mae=1.5)
        all_records.extend(rows)
        sampling_counts[info.filename] = counts
    standalone_records, standalone_counts = discover_standalone_images(raw_dir, split="train")
    all_records.extend(standalone_records)
    teacher, torch = load_teacher(args.teacher.resolve(), args.upstream.resolve())
    infer_teacher(all_records, teacher, torch, args.teacher_imgsz, args.teacher_conf,
                  args.teacher_batch, args.device, output / "review")
    stats = assign_outputs(all_records, info_by_name, output)
    for split in ("train", "val", "test"):
        rows = [r for r in all_records if r.split == split]
        make_contact_sheet(rows, output / "review" / f"{split}_contact_sheet.jpg", split)
        review_rows = [r for r in rows if r.teacher_status not in {"positive", "negative"}]
        make_contact_sheet(review_rows, output / "review" / f"{split}_review_contact_sheet.jpg", f"{split}-review", max_items=160)
    write_yaml(output)
    manifest = {
        "dataset": "longpet-realworld-person-v2",
        "class_names": ["person"],
        "raw_dir": str(raw_dir),
        "teacher": str(args.teacher.resolve()),
        "teacher_sha256": sha256(args.teacher.resolve()),
        "teacher_imgsz": args.teacher_imgsz,
        "teacher_conf_input": args.teacher_conf,
        "sample_fps": args.sample_fps,
        "split_policy": "video-level fixed assignment; no source video crosses splits",
        "confirmed_empty_videos": sorted(CONFIRMED_EMPTY_VIDEOS),
        "videos": [asdict(v) | {"scene": compact_scene_name(v.filename), **audit_tags(v.filename)} for v in infos],
        "excluded_videos": EXCLUDED_FILES,
        "sampling_counts": sampling_counts,
        "standalone_images": standalone_counts,
        "standalone_audit": [
            {
                "filename": rec.video,
                "status": rec.teacher_status,
                "teacher_conf": rec.teacher_conf,
                "split": rec.split,
                "duplicate_against_video_frames": False,
                "output_image": rec.output_image,
                "output_label": rec.output_label,
            }
            for rec in all_records if rec.source_kind == "standalone_image"
        ],
        "split_stats": {k: dict(v) for k, v in stats.items()},
        "frames": [asdict(r) for r in all_records],
        "generated_seconds": time.time() - started,
    }
    if source_archive is not None:
        manifest["source_archive"] = str(source_archive)
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with (output / "review" / "frame_review.csv").open("w", newline="", encoding="utf-8-sig") as stream:
        fields = list(asdict(all_records[0]).keys()) if all_records else []
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for rec in all_records:
            writer.writerow(asdict(rec))
    write_report(
        output, infos, all_records, stats, args.sample_fps, args.teacher.resolve(),
        sampling_counts, EXCLUDED_FILES, standalone_counts,
    )
    print(json.dumps({"output": str(output), "split_stats": {k: dict(v) for k, v in stats.items()}, "seconds": time.time() - started}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
