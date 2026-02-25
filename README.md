# Fusion

Linux-only programming language with LLVM backend, C runtime, and full FFI for `.so` libraries (CUDA/NCCL-capable). See [fusion-design-document.md](fusion-design-document.md) for the full design.

## Build and test (Linux)

Same as CI:

```bash
./ci/build_and_test.sh
```

Or use the wrapper Makefile (out-of-tree build):

```bash
make
make check
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

## User-local dependencies (~/.local)

On systems where libffi or zlib are not available as dev packages (e.g. no sudo), you can install them under `~/.local` and point the build there.

**1. Install libffi**

```bash
mkdir -p "$HOME/src" "$HOME/.local"
cd "$HOME/src"
curl -LO https://github.com/libffi/libffi/releases/download/v3.4.6/libffi-3.4.6.tar.gz
tar -xzf libffi-3.4.6.tar.gz
cd libffi-3.4.6
./configure --prefix="$HOME/.local"
make -j
make install
```

**2. Install zlib** (if you see link errors for `compress2`, `uncompress`, `crc32`)

```bash
cd "$HOME/src"
curl -LO https://zlib.net/zlib-1.3.1.tar.gz
tar -xzf zlib-1.3.1.tar.gz
cd zlib-1.3.1
./configure --prefix="$HOME/.local"
make -j
make install
```

**3. Use them when building**

Source the env script so CMake and the compiler find headers and libs in `~/.local`:

```bash
source ./env_local_deps.sh
./test.sh
```

Or for a manual build: `source ./env_local_deps.sh` then run `cmake -B build`, `cmake --build build`, etc.

**Sanity check** (after sourcing): `pkg-config --modversion libffi` and `pkg-config --modversion zlib` should print versions.

**Option A via CMake:** If you prefer not to install libffi manually, you can have CMake download and build it into the build tree:

```bash
cmake -B build -S . -DFUSION_FETCH_LIBFFI=ON
cmake --build build
```

This uses ExternalProject to fetch libffi 3.4.6 and install it under `build/_deps/libffi-install` (no `~/.local` or `env_local_deps.sh` needed).

## LLVM (required)

LLVM is required to build Fusion. If LLVM is not installed, CMake will try to find it; on Linux, if not found, CMake can download a pre-built LLVM into `build/_deps/llvm` on first configure (set `FUSION_DOWNLOAD_LLVM=ON`, which is the default on Linux).

**Auto-download (no sudo):** On Linux, if LLVM is not found, CMake downloads a pre-built LLVM (x86_64 Linux) into `build/_deps/llvm`. To disable auto-download and supply LLVM yourself:

```bash
cmake -B build -S . -DFUSION_DOWNLOAD_LLVM=OFF
```

To use a different LLVM version when auto-downloading:

```bash
cmake -B build -S . -DFUSION_LLVM_VERSION=18.1.7
```

If LLVM cannot be found or downloaded, configure will fail with an error.
