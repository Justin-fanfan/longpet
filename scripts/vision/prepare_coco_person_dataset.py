#!/usr/bin/env python3
"""Create a deterministic COCO-2017 person-only YOLO dataset subset."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import random
import time
import urllib.error
import urllib.request
import zipfile


# The official custom hostname currently presents a mismatched certificate on
# some Windows networks. This is the same official public S3 bucket through the
# standard AWS endpoint, so TLS verification stays enabled.
COCO_BUCKET = "https://s3.amazonaws.com/images.cocodataset.org"
ANNOTATIONS_URL = f"{COCO_BUCKET}/annotations/annotations_trainval2017.zip"
IMAGE_BASE_URL = f"{COCO_BUCKET}/{{split}}/{{file_name}}"
EXPECTED_ARCHIVE_SHA256 = "113a836d90195ee1f884e704da6304dfaaecff1f023f49b6ca93c4aaae470268"
EXPECTED_ANNOTATION_SHA256 = {
    "instances_train2017.json": "610fce4944abdeb15354cc765333805529359d12d88f2f711393ca586901d01d",
    "instances_val2017.json": "e8c7f7908f1d7278341fae127d0da654f102f11bd7b21d8aeefa635b8c810b6f",
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, destination: pathlib.Path, retries: int = 5) -> None:
    if destination.exists() and destination.stat().st_size > 0:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": "LongPet-Vision/1.1"})
    for attempt in range(1, retries + 1):
        try:
            with urllib.request.urlopen(request, timeout=60) as response, temporary.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    output.write(chunk)
            temporary.replace(destination)
            return
        except (urllib.error.URLError, TimeoutError, OSError):
            if attempt == retries:
                raise
            time.sleep(min(2 ** (attempt - 1), 8))


def prepare_annotations(cache: pathlib.Path) -> pathlib.Path:
    extracted = cache / "annotations"
    train = extracted / "instances_train2017.json"
    val = extracted / "instances_val2017.json"
    if not (train.exists() and val.exists()):
        archive = cache / "annotations_trainval2017.zip"
        download(ANNOTATIONS_URL, archive)
        if sha256(archive) != EXPECTED_ARCHIVE_SHA256:
            raise RuntimeError("COCO annotation archive SHA-256 mismatch")
        with zipfile.ZipFile(archive) as source:
            members = [
                "annotations/instances_train2017.json",
                "annotations/instances_val2017.json",
            ]
            for member in members:
                source.extract(member, cache)
    for annotation in (train, val):
        if sha256(annotation) != EXPECTED_ANNOTATION_SHA256[annotation.name]:
            raise RuntimeError(f"COCO annotation SHA-256 mismatch: {annotation}")
    return extracted


def choose_images(
    annotation_path: pathlib.Path,
    positive_limit: int,
    negative_limit: int,
    seed: int,
) -> tuple[list[dict], dict[int, list[dict]], dict[str, int]]:
    coco = json.loads(annotation_path.read_text(encoding="utf-8"))
    person_ids = {item["id"] for item in coco["categories"] if item["name"] == "person"}
    if len(person_ids) != 1:
        raise RuntimeError(f"expected one COCO person category, got {person_ids}")
    person_id = next(iter(person_ids))
    person_by_image: dict[int, list[dict]] = {}
    any_person_image: set[int] = set()
    for annotation in coco["annotations"]:
        if annotation["category_id"] != person_id:
            continue
        image_id = int(annotation["image_id"])
        any_person_image.add(image_id)
        if not annotation.get("iscrowd", 0) and annotation.get("area", 0) > 0:
            x, y, width, height = annotation["bbox"]
            if width > 0 and height > 0:
                person_by_image.setdefault(image_id, []).append(annotation)

    positives = [image for image in coco["images"] if image["id"] in person_by_image]
    # Crowd-only person images are excluded from both pools, not mislabeled negative.
    negatives = [image for image in coco["images"] if image["id"] not in any_person_image]
    rng = random.Random(seed)
    rng.shuffle(positives)
    rng.shuffle(negatives)
    selected_positive = positives[:positive_limit] if positive_limit else positives
    selected_negative = negatives[:negative_limit] if negative_limit else negatives
    selected = selected_positive + selected_negative
    rng.shuffle(selected)
    counts = {
        "positive": len(selected_positive),
        "negative": len(selected_negative),
        "boxes": sum(len(person_by_image.get(image["id"], [])) for image in selected),
    }
    return selected, person_by_image, counts


def yolo_lines(image: dict, annotations: list[dict]) -> str:
    width = float(image["width"])
    height = float(image["height"])
    lines: list[str] = []
    for annotation in annotations:
        x, y, box_width, box_height = map(float, annotation["bbox"])
        center_x = min(max((x + box_width / 2.0) / width, 0.0), 1.0)
        center_y = min(max((y + box_height / 2.0) / height, 0.0), 1.0)
        normalized_width = min(max(box_width / width, 0.0), 1.0)
        normalized_height = min(max(box_height / height, 0.0), 1.0)
        lines.append(
            f"0 {center_x:.8f} {center_y:.8f} "
            f"{normalized_width:.8f} {normalized_height:.8f}"
        )
    return "\n".join(lines) + ("\n" if lines else "")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--cache", required=True, type=pathlib.Path)
    parser.add_argument("--train-positive", type=int, default=4000)
    parser.add_argument("--train-negative", type=int, default=1000)
    parser.add_argument("--val-positive", type=int, default=1000)
    parser.add_argument("--val-negative", type=int, default=250)
    parser.add_argument("--seed", type=int, default=20260901)
    parser.add_argument("--workers", type=int, default=12)
    args = parser.parse_args()
    if min(args.train_positive, args.train_negative,
           args.val_positive, args.val_negative) < 0:
        parser.error("sample limits must be non-negative")

    output = args.output.resolve()
    cache = args.cache.resolve()
    annotations_root = prepare_annotations(cache)
    manifest: dict = {
        "format": 1,
        "source": "COCO 2017 train2017 + val2017",
        "class_names": ["person"],
        "seed": args.seed,
        "splits": {},
    }
    selected_by_split: dict[str, list[dict]] = {}
    labels_by_split: dict[str, dict[int, list[dict]]] = {}
    for split, positive_limit, negative_limit, seed_offset in (
        ("train2017", args.train_positive, args.train_negative, 0),
        ("val2017", args.val_positive, args.val_negative, 1),
    ):
        annotation_path = annotations_root / f"instances_{split}.json"
        selected, labels, counts = choose_images(
            annotation_path, positive_limit, negative_limit,
            args.seed + seed_offset,
        )
        selected_by_split[split] = selected
        labels_by_split[split] = labels
        manifest["splits"][split] = {
            **counts,
            "positive_limit": positive_limit,
            "negative_limit": negative_limit,
            "annotation_sha256": sha256(annotation_path),
            "selected_image_ids": [int(image["id"]) for image in selected],
        }

    jobs: list[tuple[str, dict, pathlib.Path]] = []
    for split, images in selected_by_split.items():
        short_split = "train" if split == "train2017" else "val"
        image_dir = output / "images" / short_split
        label_dir = output / "labels" / short_split
        image_dir.mkdir(parents=True, exist_ok=True)
        label_dir.mkdir(parents=True, exist_ok=True)
        list_lines: list[str] = []
        for image in images:
            destination = image_dir / image["file_name"]
            jobs.append((split, image, destination))
            label = label_dir / f"{pathlib.Path(image['file_name']).stem}.txt"
            label.write_text(
                yolo_lines(image, labels_by_split[split].get(image["id"], [])),
                encoding="utf-8",
            )
            list_lines.append(destination.resolve().as_posix())
        (output / f"{short_split}.txt").write_text(
            "\n".join(list_lines) + "\n", encoding="utf-8"
        )

    def fetch(job: tuple[str, dict, pathlib.Path]) -> None:
        split, image, destination = job
        download(
            IMAGE_BASE_URL.format(split=split, file_name=image["file_name"]),
            destination,
        )

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = [executor.submit(fetch, job) for job in jobs]
        for index, future in enumerate(concurrent.futures.as_completed(futures), 1):
            future.result()
            if index % 250 == 0 or index == len(futures):
                print(f"downloaded_or_verified={index}/{len(futures)}", flush=True)

    dataset_yaml = output / "coco-person.yaml"
    dataset_yaml.write_text(
        f"path: {output.as_posix()}\n"
        "train: train.txt\n"
        "val: val.txt\n"
        "names:\n  0: person\n",
        encoding="utf-8",
    )
    manifest["dataset_yaml"] = dataset_yaml.as_posix()
    manifest_path = output / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"dataset={dataset_yaml}")
    print(f"manifest={manifest_path}")
    summary = {
        split: {key: value for key, value in details.items()
                if key != "selected_image_ids"}
        for split, details in manifest["splits"].items()
    }
    print(json.dumps(summary, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
