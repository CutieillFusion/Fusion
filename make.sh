#!/usr/bin/env bash
# CI build and test for Fusion (Linux).
# Run from repo root: ./make.sh
# If -r is given, removes build/ then configures, builds, and runs tests (full rebuild).
# If -d is given, uses Debug with ASan.
# If -n is given, uses Ninja as the CMake generator (requires ninja-build).
# Prefer system cmake/ctest so JIT tests link the runtime correctly (PATH may have wrappers).

set -e

# Install system dependencies (idempotent)
# Check if all required packages are missing
MISSING_PKGS=()
for pkg in build-essential ninja-build ccache cmake llvm-18-dev libffi-dev libcurl4-openssl-dev nlohmann-json3-dev zlib1g-dev libncurses-dev libzstd-dev; do
    if ! dpkg -s "$pkg" &> /dev/null; then
        MISSING_PKGS+=("$pkg")
    fi
done

if [ "${#MISSING_PKGS[@]}" -eq 10 ]; then
    echo "All required packages are missing. Installing..."
    sudo apt install -y "${MISSING_PKGS[@]}"
fi

# Use system CMake/CTest when available so runtime is linked with --whole-archive/--export-dynamic.
if [[ -x /usr/bin/cmake ]]; then
  CMAKE=/usr/bin/cmake
  CTEST=/usr/bin/ctest
else
  CMAKE=cmake
  CTEST=ctest
fi

# Parse flags
USE_NINJA=""
if [[ " $@ " =~ " -n " ]]; then
  USE_NINJA="-G Ninja"
fi

if [[ " $@ " =~ " -r " ]]; then
  rm -rf build
fi

CMAKE_ARGS=()
if [[ -n "$USE_NINJA" ]]; then
  CMAKE_ARGS+=(-G Ninja)
fi

if [[ " $@ " =~ " -d " ]]; then
  "$CMAKE" -B build -S . "${CMAKE_ARGS[@]}" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -g -fno-omit-frame-pointer"
else
  "$CMAKE" -B build -S . "${CMAKE_ARGS[@]}"
fi

"$CMAKE" --build build -j"$(nproc)"

"$CTEST" --test-dir build --output-on-failure