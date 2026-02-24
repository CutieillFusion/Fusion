# Fusion

Linux-only programming language with LLVM backend, C runtime, and full FFI for `.so` libraries (CUDA/NCCL-capable). See [fusion-design-document.md](fusion-design-document.md) for the full design.

## Build and test (Linux)

Same as CI:

```bash
./ci/build_and_test.sh
```

Or manually:

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

## Exit criteria (Phase 0)

- **`fusion --help`** — Run `./build/compiler/fusion --help` for usage.
- **Runtime** — `build/runtime_c/libruntime.so` and `build/runtime_c/libruntime.a` are produced.
- **Tests** — `ctest --test-dir build` runs the C test runner and the `fusion --help` test.

## Optional: LLVM

If LLVM is installed and found by CMake (`find_package(LLVM)`), the compiler links LLVM and `fusion --version` prints the LLVM version.

**Auto-download (no sudo):** On Linux, if LLVM is not found, CMake will download a pre-built LLVM (Ubuntu 22.04 x86_64) into `build/deps/llvm` on first configure. You can disable this with:

```bash
cmake -B build -S . -DFUSION_DOWNLOAD_LLVM=OFF
```

To use a different LLVM version when auto-downloading:

```bash
cmake -B build -S . -DFUSION_LLVM_VERSION=18.1.7
```

Without LLVM (and with `FUSION_DOWNLOAD_LLVM=OFF` or on non-Linux), the project still builds and `fusion --version` reports "LLVM not linked".
