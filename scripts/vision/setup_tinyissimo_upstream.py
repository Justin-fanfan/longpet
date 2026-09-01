#!/usr/bin/env python3
"""Prepare a pinned, minimally patched TinyissimoYOLO training checkout."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


REPOSITORY = "https://github.com/ETH-PBL/TinyissimoYOLO.git"


def run(command: list[str], cwd: pathlib.Path | None = None) -> str:
    completed = subprocess.run(
        command, cwd=cwd, check=True, text=True, capture_output=True
    )
    return completed.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", required=True, type=pathlib.Path)
    parser.add_argument(
        "--integration-dir",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[2]
        / "third_party"
        / "tinyissimo-yolo",
    )
    args = parser.parse_args()
    integration = args.integration_dir.resolve()
    commit = (integration / "UPSTREAM_COMMIT").read_text(encoding="utf-8").strip()
    patch = integration / "patches" / "0001-longpet-v1-small-training-export.patch"
    checkout = args.checkout.resolve()

    if not checkout.exists():
        checkout.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--no-checkout", REPOSITORY, str(checkout)])
    if not (checkout / ".git").exists():
        raise RuntimeError(f"not a Git checkout: {checkout}")

    run(["git", "fetch", "origin", commit], checkout)
    run(["git", "checkout", "--detach", commit], checkout)
    head = run(["git", "rev-parse", "HEAD"], checkout)
    if head != commit:
        raise RuntimeError(f"expected {commit}, got {head}")

    status = subprocess.run(
        ["git", "status", "--porcelain"], cwd=checkout, check=True,
        text=True, capture_output=True,
    ).stdout.rstrip()
    if status:
        # An already-applied *exact* patch is allowed; reject untracked files or
        # additional edits even when `git apply --reverse --check` would happen
        # to accept the expected hunks.
        current_diff = run(
            [
                "git", "diff", "--no-ext-diff", "--binary", "HEAD", "--",
                "ultralytics/engine/exporter.py",
                "ultralytics/nn/modules/head.py",
            ],
            checkout,
        )
        expected_diff = patch.read_text(encoding="utf-8").strip()
        if (set(status.splitlines()) != {
                " M ultralytics/engine/exporter.py",
                " M ultralytics/nn/modules/head.py",
            }
                or current_diff.replace("\r\n", "\n")
                != expected_diff.replace("\r\n", "\n")):
            raise RuntimeError(
                "checkout has unrelated changes; use a clean dedicated checkout"
            )
    else:
        run(["git", "apply", "--check", str(patch)], checkout)
        run(["git", "apply", str(patch)], checkout)

    print(f"checkout={checkout}")
    print(f"upstream_commit={head}")
    print("patch_state=applied")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
