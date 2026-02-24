#!/usr/bin/env bash
# CI build and test for Fusion (Linux).
# Run from repo root: ./test.sh

set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
[ -f env_local_deps.sh ] && . ./env_local_deps.sh

mkdir -p build
echo "=== Configuring ==="
cmake -S . -B build

echo "=== Building ==="
cmake --build build --target fusion fusion_tests test_runner

echo "=== Running tests ==="
(cd build && ctest --output-on-failure)
./build/tests/test_runner

echo "=== Done ==="