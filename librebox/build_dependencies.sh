#!/bin/bash
set -e

echo "=========================================================="
echo "           BUILDING THIRD-PARTY DEPENDENCIES"
echo "=========================================================="

# -- Configuration --
CMAKE_BUILD_TYPE="Release"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# -- Paths --
LUAU_SOURCE_DIR="${SCRIPT_DIR}/luau"
LUAU_BUILD_DIR="${LUAU_SOURCE_DIR}/build"
VENDOR_DIR="${SCRIPT_DIR}/third_party/vendor"

# Assuming Raylib is already present in VENDOR_DIR/raylib

echo
echo "[1/3] Configuring Luau..."
echo "      Source: ${LUAU_SOURCE_DIR}"
echo "      Build:  ${LUAU_BUILD_DIR}"
echo "      Install Destination: ${VENDOR_DIR}/luau"

[ ! -d "${LUAU_BUILD_DIR}" ] && mkdir -p "${LUAU_BUILD_DIR}"
cd "${LUAU_BUILD_DIR}"

# Generate the build files and set the install location
cmake "${LUAU_SOURCE_DIR}" -DCMAKE_INSTALL_PREFIX="${VENDOR_DIR}/luau"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] CMake configuration for Luau failed."
    exit 1
fi

echo
echo "[2/3] Building Luau (${CMAKE_BUILD_TYPE})..."
cmake --build . --config "${CMAKE_BUILD_TYPE}"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] Luau build failed."
    exit 1
fi

echo
echo "[3/3] Installing Luau to vendor directory..."
cmake --install . --config "${CMAKE_BUILD_TYPE}"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] Luau installation failed."
    exit 1
fi

cd "${SCRIPT_DIR}"
echo
echo "=========================================================="
echo "          DEPENDENCIES BUILT AND INSTALLED!"
echo "          You can now run build_engine.sh"
echo "=========================================================="
