#!/usr/bin/env bash
# CI build and test for Fusion (Linux).
# Run from repo root: ./test.sh
# If any argument is given, removes build/ then configures, builds, and runs tests (full rebuild).
# If no arguments, preserves existing build directory.

set -e

if [[ $# -gt 0 ]]; then
  rm -rf build
fi

cmake -B build -S .

cmake --build build

ctest --test-dir build --output-on-failure