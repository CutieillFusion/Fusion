#!/usr/bin/env bash
# CI build and test for Fusion (Linux).
# Run from repo root: ./test.sh

set -e
cd build

echo "=== Configuring ==="
cmake ..

echo "=== Building ==="
make fusion fusion_tests

echo "=== Running tests ==="
ctest --output-on-failure

echo "=== Done ==="
cd ..