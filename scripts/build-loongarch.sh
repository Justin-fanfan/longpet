#!/usr/bin/env bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HOME/build/LongPet-my-loongarch64-release}"
BUILD_JOBS="${LONGPET_BUILD_JOBS:-$(nproc)}"

cmake \
    -S "${PROJECT_DIR}" \
    -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${PROJECT_DIR}/cmake/toolchains/loongarch64-buildroot.cmake"

cmake \
    --build "${BUILD_DIR}" \
    --parallel "${BUILD_JOBS}"

file "${BUILD_DIR}/LongPet"
