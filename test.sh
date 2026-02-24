#!/usr/bin/env bash
# CI build and test for Fusion (Linux).
# Run from repo root: ./test.sh

set -e

echo "=== Configuring ==="
cmake -B build -S .

echo "=== Building ==="
cmake --build build

echo "=== Running tests ==="
ctest --test-dir build --output-on-failure

echo "=== Done ==="
