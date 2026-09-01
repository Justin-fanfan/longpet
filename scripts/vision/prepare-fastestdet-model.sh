#!/usr/bin/env bash

set -euo pipefail

OUTPUT_PATH="${1:-./fastestdet.onnx}"
MODEL_URL="https://raw.githubusercontent.com/dog-qiuqiu/FastestDet/main/example/onnx-runtime/FastestDet.onnx"
EXPECTED_SHA256="31cb14c017fce347cb4c846ec62fdf00d76bb3beecdf4be21e7116ac67feb880"

if [[ -e "${OUTPUT_PATH}" && "${LONGPET_VISION_MODEL_FORCE:-0}" != "1" ]]; then
    echo "ERROR: output already exists: ${OUTPUT_PATH}" >&2
    echo "Set LONGPET_VISION_MODEL_FORCE=1 only after backing it up." >&2
    exit 2
fi

OUTPUT_DIR="$(dirname -- "${OUTPUT_PATH}")"
mkdir -p -- "${OUTPUT_DIR}"
TEMP_PATH="$(mktemp "${OUTPUT_DIR}/.fastestdet.onnx.XXXXXX")"
trap 'rm -f -- "${TEMP_PATH}"' EXIT

if command -v curl >/dev/null 2>&1; then
    curl --fail --location --silent --show-error \
        --output "${TEMP_PATH}" "${MODEL_URL}"
elif command -v wget >/dev/null 2>&1; then
    wget --quiet --output-document="${TEMP_PATH}" "${MODEL_URL}"
else
    echo "ERROR: curl or wget is required" >&2
    exit 3
fi

ACTUAL_SHA256="$(sha256sum "${TEMP_PATH}" | awk '{print $1}')"
if [[ "${ACTUAL_SHA256}" != "${EXPECTED_SHA256}" ]]; then
    echo "ERROR: FastestDet checksum mismatch" >&2
    echo "expected=${EXPECTED_SHA256}" >&2
    echo "actual=${ACTUAL_SHA256}" >&2
    exit 4
fi

install -m 0644 "${TEMP_PATH}" "${OUTPUT_PATH}"
echo "FastestDet model prepared: ${OUTPUT_PATH}"
echo "sha256=${ACTUAL_SHA256}"
echo "source=${MODEL_URL}"
echo "license=BSD-3-Clause (see scripts/vision/FASTESTDET-NOTICE.md)"
