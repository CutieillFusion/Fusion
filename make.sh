#!/usr/bin/env bash
# CI build and test for Fusion (Linux).
# Run from repo root: ./make.sh
# If -r is given, removes build/ then configures, builds, and runs tests (full rebuild).
# If -d is given, uses Debug with ASan.
# Prefer system cmake/ctest so JIT tests link the runtime correctly (PATH may have wrappers).

set -e

# Use system CMake/CTest when available so runtime is linked with --whole-archive/--export-dynamic.
if [[ -x /usr/bin/cmake ]]; then
  CMAKE=/usr/bin/cmake
  CTEST=/usr/bin/ctest
else
  CMAKE=cmake
  CTEST=ctest
fi

if [[ " $@ " =~ " -r " ]]; then
  rm -rf build
fi

if [[ " $@ " =~ " -d " ]]; then
  "$CMAKE" -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -g -fno-omit-frame-pointer"
else
  "$CMAKE" -B build -S .
fi

"$CMAKE" --build build

"$CTEST" --test-dir build --output-on-failure