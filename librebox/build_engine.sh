#!/bin/bash
set -e

echo "=========================================================="
echo "                BUILDING LIBREBOX ENGINE"
echo "=========================================================="

CMAKE_BUILD_TYPE="Release"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_ROOT="${SCRIPT_DIR}"
BUILD_DIR="${PROJECT_ROOT}/build"
DIST_DIR="${PROJECT_ROOT}/dist"
CMAKE_PREFIX_PATH="${PROJECT_ROOT}/third_party/vendor"

NO_CLEAN=0
if [[ "$1" == "-no-clean" ]]; then
    NO_CLEAN=1
fi

echo
if [[ "$NO_CLEAN" == "0" ]]; then
    echo "[1/4] Cleaning build directory only..."
    if [[ -d "${BUILD_DIR}" ]]; then
        rm -rf "${BUILD_DIR}"
    fi
else
    echo "[1/4] Skipping clean (because -no-clean was specified)"
fi

echo
echo "[2/4] Configuring Librebox..."
echo "      Build Dir: ${BUILD_DIR}"
[ ! -d "${BUILD_DIR}" ] && mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake .. -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] CMake configuration failed."
    exit 1
fi

echo
echo "[3/4] Building Librebox (${CMAKE_BUILD_TYPE})..."
cmake --build . --config "${CMAKE_BUILD_TYPE}"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] Librebox build failed."
    exit 1
fi

echo
echo "[4/4] Installing to 'dist'..."
DESTDIR="$DIST_DIR" cmake --install . --config "${CMAKE_BUILD_TYPE}"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] Installation failed."
    exit 1
fi

echo
echo "=========================================================="
echo "                BUILD SUCCESSFUL"
echo "=========================================================="