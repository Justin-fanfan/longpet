#!/usr/bin/env bash

set -euo pipefail
PROJECT_DIR="/mnt/d/code_qt/longpet"
BUILD_DIR="$HOME/build/LongPet-loongarch64-release"
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
