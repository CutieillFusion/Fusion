# Fusion test setup

Tests use **CTest** as the orchestrator with **multiple discrete tests**. Compiler and runtime tests use **GoogleTest**; CLI smoke tests are plain CTest commands.

## Layout

| File / target | Role |
|----------------|------|
| `fusion_test_compiler` | GoogleTest binary: lexer, parser, sema, codegen, JIT (discovered as many CTest tests, label `compiler`). |
| `fusion_test_runtime` | GoogleTest binary: runtime basic, dlopen/dlsym, FFI with `GTEST_SKIP()` when FFI unavailable (label `runtime`). |
| `compiler/test_compiler.cpp` | GoogleTest compiler tests. |
| `runtime/test_runtime_*.cpp` | GoogleTest runtime tests (basic, dl, ffi). |
| `common/`, `data/` | Helpers and test data (e.g. `data/test.fusion`). |

## CTest tests and labels

- **Discovered GoogleTest tests** from `fusion_test_compiler` (label: `compiler`) and `fusion_test_runtime` (label: `runtime`)
- **cli.help** — `fusion --help` (labels: `cli`, `smoke`)
- **cli.run.test_fusion** — `fusion run` on repo-root `test.fusion` with `LC_ALL=C` (labels: `cli`, `smoke`)

## How to run tests

- **All tests:**  
  `make check`  
  or from build dir: `ctest --output-on-failure`

- **By label:**  
  `make check-runtime` → `ctest -L runtime --output-on-failure`  
  `make check-compiler` → `ctest -L compiler --output-on-failure`  
  `make check-cli` → `ctest -L cli --output-on-failure`  
  or run `ctest -L <label>` from the build directory.

- **List tests:**  
  `ctest -N` (from build dir)

- **Single GoogleTest binary (e.g. for debugging):**  
  `./build/tests/fusion_test_compiler --gtest_filter=ParserTests.* --gtest_color=yes`  
  `./build/tests/fusion_test_runtime --gtest_filter=RuntimeDlTests.* --gtest_color=yes`

## Optional libffi

When libffi is not found at configure time, the runtime uses an FFI stub. FFI GoogleTest cases in `fusion_test_runtime` call `GTEST_SKIP()` when the runtime reports FFI "not available", so CTest reports them as skipped rather than failed.

## Test data

- CLI smoke test `cli.run.test_fusion` uses `CMAKE_SOURCE_DIR/test.fusion` (repo root).
- A copy under `tests/data/test.fusion` is available for future use (e.g. moving all test data under `tests/`).
