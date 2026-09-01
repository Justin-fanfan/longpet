#!/usr/bin/env bash

set -euo pipefail

MODEL_PATH="${1:-}"
CAMERA_DEVICE="${2:-${LONGPET_CAMERA_DEVICE:-${LONGPET_CALL_CAMERA_DEVICE:-/dev/video0}}}"
BENCHMARK_BIN="${LONGPET_VISION_BENCHMARK_BIN:-/home/longpet/LongPetVisionBench}"
DURATION_SECONDS="${LONGPET_VISION_BENCHMARK_DURATION:-60}"
WARMUP_FRAMES="${LONGPET_VISION_BENCHMARK_WARMUP:-10}"

if [[ -z "${MODEL_PATH}" ]]; then
    echo "Usage: $0 /path/to/fastestdet.onnx [/dev/video0]" >&2
    exit 2
fi
if [[ ! -r "${MODEL_PATH}" ]]; then
    echo "ERROR: model is not readable: ${MODEL_PATH}" >&2
    exit 3
fi
if [[ ! -c "${CAMERA_DEVICE}" ]]; then
    echo "ERROR: camera node is unavailable: ${CAMERA_DEVICE}" >&2
    exit 4
fi
if [[ ! -x "${BENCHMARK_BIN}" ]]; then
    echo "ERROR: benchmark executable is missing: ${BENCHMARK_BIN}" >&2
    exit 5
fi
if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "ERROR: gst-launch-1.0 is not installed" >&2
    exit 6
fi
if ! command -v ldd >/dev/null 2>&1; then
    echo "ERROR: ldd is required for the dependency check" >&2
    exit 7
fi

echo "===== LongPet Vision V1 dependency check ====="
MISSING_LIBRARIES="$(ldd "${BENCHMARK_BIN}" 2>&1 | awk '/not found/ {print}')"
ldd "${BENCHMARK_BIN}"
if [[ -n "${MISSING_LIBRARIES}" ]]; then
    echo "ERROR: benchmark has missing dynamic libraries:" >&2
    echo "${MISSING_LIBRARIES}" >&2
    exit 8
fi

echo "===== LongPet Vision V1 camera benchmark ====="
exec "${BENCHMARK_BIN}" \
    --model "${MODEL_PATH}" \
    --camera "${CAMERA_DEVICE}" \
    --duration "${DURATION_SECONDS}" \
    --warmup "${WARMUP_FRAMES}" \
    --interval-ms 1
