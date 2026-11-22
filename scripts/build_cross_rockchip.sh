#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build_cross"
TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/toolchain-arm-rockchip830-uclibcgnueabihf.cmake"

BUILD_TYPE="Release"

if [[ $# -gt 0 && "$1" == "debug" ]]; then
    BUILD_TYPE="Debug"
fi

echo "=========================================================="
echo " Building UAV Camera Firmware [${BUILD_TYPE}]"
echo " Toolchain: ${TOOLCHAIN_FILE}"
echo "=========================================================="

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      "${PROJECT_ROOT}"

make -j$(nproc)

echo "=========================================================="
echo " Build successful: ${BUILD_DIR}"
echo "=========================================================="
