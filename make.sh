#!/usr/bin/env bash
# CI build and test for Fusion (Linux).
# Run from repo root: ./test.sh
# If any argument is given, removes build/ then configures, builds, and runs tests (full rebuild).
# If no arguments, preserves existing build directory.

set -e

if [[ " $@ " =~ " -r " ]]; then
  rm -rf build
fi

if [[ " $@ " =~ " -d " ]]; then
  cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -g -fno-omit-frame-pointer"
else
  cmake -B build -S .
fi

cmake --build build

ctest --test-dir build --output-on-failure