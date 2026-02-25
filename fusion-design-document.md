# Design Document

## Fusion — Linux-Only Programming Language with LLVM Backend, C Runtime, and Full FFI for `.so` Libraries (CUDA/NCCL-capable)

## 1. Goal

Build **Fusion**, a programming language that:

* runs on **Linux only**
* compiles to native code using **LLVM**
* uses a **C runtime**
* supports **full FFI** to call functions from shared libraries (`.so`)
* is robust enough to interoperate with **CUDA Runtime (`cudart`)** and **NCCL**
* minimizes custom backend and ABI code by leveraging:

  * **LLVM** (codegen)
  * **libffi** (dynamic function calls)
  * **libdl** (`dlopen`, `dlsym`)
  * later: **Clang/libclang** for bindgen

The language is named **Fusion**.

---

## 2. Non-Goals (for v1)

To keep this finishable, v1 will **not** include:

* Windows/macOS support
* Full C parser/header import at runtime
* Full optimizer for Fusion’s IR
* Debugger integration / DWARF debug info
* Generics, classes, advanced type system
* Garbage collector (optional later)
* GPU kernel compilation (you are **calling** CUDA/NCCL APIs, not compiling CUDA kernels)

---

## 3. High-Level Architecture

```text
Source Code (.fusion)
   ↓
Lexer
   ↓
Parser
   ↓
AST
   ↓
Semantic Analysis / Type Checking
   ↓
Lowered IR (optional but recommended)
   ↓
LLVM IR Generation
   ↓
[JIT via ORC] or [AOT object/executable]
   ↓
Calls into C Runtime (rt_*)
   ↓
FFI Engine (libdl + libffi)
   ↓
Shared libraries (.so): libc, libm, libcudart.so, libnccl.so
```

### Key design principle

Keep all OS and ABI complexity in the **C runtime**, not in the compiler frontend.

---

## 4. Why LLVM + C Runtime + libffi

### LLVM reduces code you must write for:

* machine code generation
* register allocation
* instruction selection
* object code emission
* JIT execution infrastructure

### C runtime handles:

* core runtime functions (`print`, panic, alloc, strings)
* Linux dynamic loading (`dlopen`, `dlsym`)
* FFI call execution (`libffi`)
* ABI/type marshaling
* error handling around dynamic calls

### libffi reduces code you must write for:

* arbitrary C call signatures
* avoiding a combinatorial explosion of handwritten wrappers

---

## 5. Core Components

## 5.1 Compiler Frontend (Fusion implementation)

### Responsibilities

* lexing/parsing source
* AST construction
* semantic analysis (types, names, function signatures)
* lowering to LLVM IR
* emitting calls to runtime functions (e.g., `rt_ffi_call`, `rt_dlopen`)

### Suggested implementation language

* **C++** (best LLVM support), or
* **Rust** if you prefer safety and are okay with some LLVM binding friction

If your goal is to move fast with LLVM docs/examples, C++ is the smoothest.

---

## 5.2 C Runtime (`runtime_c`)

### Responsibilities

* stable ABI boundary between generated code and OS/libs
* memory helpers (optional minimal allocator wrappers)
* string handling helpers
* runtime errors/panic
* dynamic loader wrappers (`dlopen`, `dlsym`, `dlerror`)
* FFI type registry and call engine (`libffi`)
* optional CUDA/NCCL convenience wrappers/checkers

### Why a C runtime is a good fit

* easy to link from LLVM-generated code
* easy to interop with Linux `.so`
* predictable ABI
* easy to expose `extern "C"` functions for compiler-generated calls

---

## 5.3 FFI System (Full FFI)

This is the critical piece for NCCL/cudart compatibility.

### Required capabilities

* primitive types (`i8/i16/i32/i64`, `u*`, `f32/f64`, `bool`, `void`)
* pointers (`ptr`, pointer-to-pointer)
* structs (especially by pointer)
* enums (treated as integer types)
* strings (`char*`)
* arrays/buffers (as pointers + lengths)
* return values and out-parameters
* exact size/alignment layout handling
* dynamic symbol lookup

### Implementation strategy

* **libdl** for library loading and symbol lookup
* **libffi** for call execution using runtime-defined signatures

---

## 5.4 Bindings Layer (How Fusion knows C signatures)

### v1 approach (manual declarations)

Users declare extern functions manually in Fusion, e.g. conceptually:

* library path/name
* symbol name
* parameter types
* return type

This gets Fusion working quickly.

### v2 approach (bindgen)

Use **Clang/libclang** to parse headers and generate:

* Fusion-level extern declarations
* constants/enums
* struct layout metadata

This is especially valuable for `cuda_runtime_api.h` and `nccl.h`.

---

## 6. Fusion Language Design for FFI (Minimal but sufficient)

Fusion does **not** need a huge type system to support serious FFI.

## 6.1 Core types (v1)

* `i32`, `i64`
* `u32`, `u64`
* `f32`, `f64`
* `bool`
* `void`
* `ptr<T>` (or raw `ptr`)
* `cstring` (read-only null-terminated string)
* `struct` definitions (at least C-layout structs)

## 6.2 FFI declaration syntax (recommended direction)

A Fusion-level way to declare:

* library name/path
* symbol
* ABI (default SysV)
* parameter list
* return type

Also add a way to mark:

* opaque types (e.g., `opaque ncclComm_t`)
* C-layout structs
* constants

## 6.3 Runtime values vs FFI values

Be explicit about how Fusion values map to FFI:

* `i32` → C `int32_t`
* `i64` → C `int64_t` / `long long` (pick a canonical mapping)
* `f64` → C `double`
* `bool` → C `int` or `_Bool` (pick and document)
* `cstring` → `const char*`
* `ptr<T>` → `void*` at ABI layer with type metadata retained in Fusion

---

## 7. ABI and Layout Rules (Important)

This is where most FFI bugs happen.

## 7.1 Platform assumptions (v1)

* Linux x86-64
* SysV AMD64 ABI
* ELF shared libraries

## 7.2 Rules to enforce/document

* exact primitive sizes
* struct field alignment
* padding insertion
* pointer size = 8 bytes
* endianness = little-endian (on x86-64 Linux)
* enum backing type policy (usually explicit integer in bindings)

## 7.3 Practical recommendation

Prefer passing structs by pointer in v1 for CUDA/NCCL interop.
Structs-by-value can be added later (much more ABI-sensitive).

---

## 8. Runtime API Design (C Runtime Surface)

Your compiler-generated code should call a small stable runtime API.

## 8.1 Dynamic loading API

* `rt_dlopen(path)`
* `rt_dlsym(handle, symbol)`
* `rt_dlclose(handle)`
* `rt_dlerror_last()` (or equivalent error reporting)

## 8.2 FFI type construction API (runtime metadata)

* create primitive type descriptors
* create pointer type descriptors
* create struct type descriptors
* create function signature descriptors

## 8.3 FFI invocation API

A generic call path such as:

* prepare call interface (`ffi_cif`)
* marshal args
* invoke function pointer
* unmarshal return value

You may expose one high-level runtime function to generated code:

* `rt_ffi_call(sig, fnptr, args_buf, ret_buf)`

Generated code can build or reference static metadata descriptors.

## 8.4 Core runtime helpers

* `rt_panic(msg)`
* `rt_print_i64`, `rt_print_f64`, `rt_print_cstr`
* `rt_alloc`, `rt_free` (optional)
* `rt_memcpy`, `rt_memset` wrappers (optional but useful)

## 8.5 CUDA/NCCL convenience helpers (recommended)

These are optional but strongly practical:

* `rt_cuda_check(code)`
* `rt_nccl_check(code)`
* wrappers for error string functions
* helpers to allocate/read small out-values

This keeps Fusion ergonomics sane while retaining full FFI.

---

## 9. Execution Modes

## 9.1 v1: JIT first (recommended)

Use LLVM ORC JIT for fastest development loop:

* compile source
* JIT execute
* test runtime + FFI quickly
* no linker command orchestration yet

## 9.2 v2: AOT

Add:

* object emission
* executable linking against runtime
* packaging/runtime path handling

---

## 10. Project Structure (Recommended)

```text
fusion/
  compiler/
    lexer/
    parser/
    ast/
    sema/
    lower/              # AST -> internal lowered IR (optional but recommended)
    llvm_backend/
    driver/             # CLI: compile/run/check
  runtime_c/
    include/
    src/
      core/
      ffi/
      dl/
      abi/
      gpu/              # optional cuda/nccl helpers
    tests/
  bindings/
    manual/
      libc.fusionffi
      libm.fusionffi
      cudart.fusionffi
      nccl.fusionffi
    tools/
      bindgen/          # later (libclang-based)
    generated/          # later
  examples/
    hello/
    ffi_libm/
    ffi_cuda_runtime/
    ffi_nccl_smoke/
  tests/
    lexer/
    parser/
    sema/
    codegen/
    runtime/
    ffi/
    integration/
  docs/
    language.md
    ffi.md
    abi.md
```

---

## 11. Build and Dependency Strategy (Linux)

## 11.1 Required dependencies (v1)

* LLVM development libraries/tools
* `libffi`
* `libdl` (glibc)
* a C/C++ compiler (clang or gcc)
* build system (CMake recommended)

## 11.2 Optional dependencies (later)

* Clang/libclang for bindgen
* CUDA toolkit headers/libs
* NCCL headers/libs (depending on system install)

## 11.3 Build approach

Use one top-level build (e.g., CMake) to build:

* compiler
* runtime shared/static lib
* tests
* examples

---

## 12. Step-by-Step Implementation Plan

This is the practical part. Follow these phases in order.

---

## Phase 0 — Project Setup (1–2 days)

### Goals

* repo structure
* build system working
* LLVM links
* runtime compiles
* test harness runs

### Steps

1. Create repository and folder structure.
2. Add CMake (or chosen build system).
3. Add compiler stub executable (`fusion`).
4. Add runtime stub library (`libruntime.so` or static `libruntime.a`).
5. Add unit test framework (or simple custom test runner).
6. Add CI script for Linux build/test (optional but worth it early).

### Exit criteria

* `fusion --help` runs
* runtime library builds
* tests can run (even if empty)

---

## Phase 1 — Minimal Language + LLVM “Hello World” (3–7 days)

### Goals

* parse a tiny expression
* generate LLVM IR
* call runtime print
* JIT execute

### Language features

* integer literals
* addition
* top-level expression
* `print(...)`

### Steps

1. Implement lexer for numbers, identifiers, punctuation.
2. Implement parser for expressions and `print`.
3. Build AST nodes.
4. Implement minimal semantic checks (types = i64 only initially).
5. Add LLVM codegen for integer ops.
6. Add runtime function `rt_print_i64`.
7. Declare runtime function in LLVM IR and emit call.
8. JIT-run expression.

### Testing procedures

* Unit test lexer tokenization
* Unit test parser AST shape
* Codegen smoke test for `print(1+2)`
* Snapshot test of generated LLVM IR (optional but useful)
* Integration test asserts output `3`

### Exit criteria

* `print(1+2)` works via JIT

---

## Phase 2 — C Runtime Dynamic Loading (`dlopen`, `dlsym`) (2–4 days)

### Goals

* load `.so` libraries
* resolve symbols
* report loader errors cleanly

### Steps

1. Implement `rt_dlopen(path)` wrapper.
2. Implement `rt_dlsym(handle, name)` wrapper.
3. Implement `rt_dlclose(handle)` wrapper.
4. Implement error capture/reporting via `dlerror`.
5. Add runtime tests for loading:

   * `libm.so.6`
   * symbol `cos`
6. Expose opaque handle type to language runtime (`LibraryHandle`, `SymbolHandle`, or raw ptr).

### Testing procedures

* Runtime unit test: invalid path returns error
* Runtime unit test: invalid symbol returns error
* Runtime integration test: load `libm.so.6`, resolve `cos`
* Error message tests include symbol/path context

### Exit criteria

* runtime can load a real `.so` and resolve known symbols reliably

---

## Phase 3 — Full FFI Engine with libffi (Core) (5–12 days)

### Goals

* generic function invocation by signature
* primitive and pointer arguments
* return values
* stable runtime API for generated code

### Scope for this phase

Support:

* `void`
* `i32`, `i64`
* `f32`, `f64`
* `ptr`
* `cstring` (as `const char*`)
* out-params via pointers

No structs-by-value yet.

### Steps

1. Add runtime type descriptors (primitive, pointer, function signature).
2. Map descriptors to `ffi_type`.
3. Implement signature object / call interface caching (`ffi_cif`) (important for performance).
4. Implement argument marshaling buffers.
5. Implement `rt_ffi_call`.
6. Implement return value unmarshaling.
7. Add robust error handling for unsupported types/signatures.
8. Add tests against known libc/libm symbols:

   * `puts`
   * `strlen`
   * `cos`
   * `malloc`/`free` (carefully)

### Testing procedures

* Unit tests: type descriptor → `ffi_type` mapping
* Unit tests: signature cache hit/miss behavior
* Integration tests:

  * call `strlen("abc") == 3`
  * call `cos(0.0)` approx `1.0`
  * call `puts` returns non-negative value (platform-dependent exact value, so don’t over-specify)
* Negative tests:

  * null fn pointer
  * unsupported type
  * mismatched arg count

### Exit criteria

* runtime can dynamically call arbitrary primitive/pointer-based C functions from `.so`

---

## Phase 4 — Language-Level FFI Declarations and Calls (5–10 days)

### Goals

* user can declare external functions in Fusion
* compiler lowers FFI calls to runtime API
* end-to-end `.so` function calls from Fusion source code

### Steps

1. Add syntax for external library and function declarations.
2. Add semantic checking for FFI signatures.
3. Add type representation for FFI types in compiler.
4. Lower FFI declarations to runtime metadata (or static descriptors).
5. Generate calls to:

   * `rt_dlopen`
   * `rt_dlsym`
   * `rt_ffi_call`
6. Add language ergonomics for pointers and strings.
7. Add error propagation when load/symbol/call fails.

### Testing procedures

* Parser tests for extern syntax
* Semantic tests for invalid signatures
* Integration examples:

  * `libm.cos`
  * `libc.strlen`
  * `libc.malloc/free`
* Regression tests for repeated calls (cif cache correctness)

### Exit criteria

* End users can write Fusion source code that calls functions from `.so` libraries

---

## Phase 5 — Variables and let-bindings (3–7 days)

### Goals

* support local variable declarations and use in expressions
* allow binding names to values for reuse (e.g. intermediate results before FFI or print)
* keep scope and semantics simple (e.g. single scope or sequential let-bindings)

### Steps

1. Add syntax for variable declarations (e.g. `let x = expr;` or similar) and allow multiple statements/bindings before the final expression, or a single expression that can contain let-bindings.
2. Extend the AST with a variable-binding construct and an expression form that references a variable by name.
3. In semantic analysis, build a scope (e.g. map name to type), check that variables are defined before use and that types match.
4. In codegen, emit allocas (or SSA-style values) for each binding and use loads when a variable is referenced; ensure the single top-level expression (or last statement) uses the same codegen path as today.
5. Update the program shape if needed (e.g. allow a list of let-bindings plus one expression, or a block with bindings and expression).
6. Add parser tests for let syntax and sema tests for undefined variable and type mismatch; add a JIT test that uses a variable (e.g. `let x = 1 + 2; print(x)`).

### Testing procedures

* Parser tests for let syntax and variable references.
* Semantic tests: undefined variable, wrong type on use.
* Integration test: bind result of expression (or extern call) to a variable and use it in print or another call.

### Exit criteria

* Users can declare local variables, bind values to them, and use those variables in expressions (e.g. `let x = cos(0.0); print(x)`).

---

## Phase 6 — Structs, Opaque Types, and Out-Parameters (CUDA/NCCL Prep) (5–15 days)

### Goals

* support C-style structs (at least by pointer)
* support opaque handles cleanly
* support pointer-to-pointer patterns
* support output parameter workflows

### Steps

1. Add C-layout struct definitions in Fusion (or binding format).
2. Implement struct layout computation in runtime/compiler metadata.
3. Add layout validation hooks (size/alignment assertions).
4. Add `alloc<T>`, `addr_of`, pointer deref/load/store operations (minimal safe subset).
5. Add opaque type declarations in Fusion (`opaque cudaStream_t`, `opaque ncclComm_t` etc. or pointer aliases).
6. Add helper APIs for reading/writing primitive values behind pointers.
7. Add tests using a small custom test `.so` with:

   * structs
   * out-params
   * pointer-to-pointer

### Testing procedures

* Unit tests: struct size/alignment/offset computation
* Cross-check tests with compiled C helper functions that return `sizeof` and offsets
* Integration tests:

  * struct passed by pointer
  * function writes to out-param
  * pointer-to-pointer allocation pattern
* Negative tests:

  * invalid field alignment assumptions
  * null pointer deref in runtime helper

### Exit criteria

* FFI supports the pointer-heavy and struct-heavy patterns common in CUDA/NCCL APIs

---

## Phase 7 — CUDA Runtime (`cudart`) Bring-Up (3–10 days, machine-dependent)

### Goals

* prove real GPU API interoperability through FFI
* validate pointer/out-param workflows
* validate error handling

### Suggested first APIs

Pick simple runtime APIs first (example categories):

* version/query functions
* device count query
* basic runtime initialization behavior
* error string functions

Avoid complex stream/memory APIs on day one.

### Steps

1. Create manual FFI declarations for a tiny subset of cudart.
2. Add constants/enums manually (or in binding files).
3. Call a simple `cudart` function via FFI.
4. Check return code and convert to human-readable error string.
5. Expand to out-param based functions (e.g., device count style API).

### Testing procedures

* Smoke test on machine with CUDA runtime available:

  * library load succeeds
  * symbol resolution succeeds
  * query call returns sane value / documented error
* Negative tests:

  * library missing
  * symbol missing
  * API unavailable on target host
* Error handling tests ensure messages are actionable

### Exit criteria

* successful real call into `libcudart.so` from Fusion via full FFI

---

## Phase 8 — NCCL Bring-Up (3–15 days, environment-dependent)

### Goals

* load `libnccl.so`
* call basic NCCL functions
* prove compatibility with opaque handles + structs + enums + pointers in Fusion

### Suggested first steps

* version/query APIs before communicator setup
* error string/status APIs
* then communicator initialization in a controlled single-process test (if applicable to your setup)

### Steps

1. Add manual bindings for a minimal NCCL subset.
2. Implement/validate opaque type handling.
3. Add any required struct definitions (e.g., IDs) with layout validation.
4. Perform simple NCCL API calls and check result codes.
5. Add small smoke example (single-node setup).

### Testing procedures

* Environment-aware tests (skip if NCCL unavailable)
* Library/symbol resolution tests
* Error code + error string round-trip tests
* Struct layout cross-checks (critical)
* Smoke test that calls a harmless NCCL function and verifies return status

### Exit criteria

* Fusion can successfully call a minimal NCCL API surface without ABI issues

---

## Phase 9 — Bindgen (Clang/libclang) (Optional but strongly recommended)

### Goals

* reduce manual declaration maintenance
* generate bindings for CUDA/NCCL headers
* generate constants/struct layouts/enums

### Steps

1. Build a standalone bindgen tool using libclang.
2. Parse selected headers (`cuda_runtime_api.h`, `nccl.h`).
3. Extract:

   * function signatures
   * typedefs
   * enums/constants
   * struct definitions
4. Generate Fusion binding files and/or binary metadata.
5. Add allowlist/blocklist controls to avoid huge imports.
6. Validate generated bindings against hand-written Fusion tests.

### Testing procedures

* Golden-file tests for generated output
* Diff against manually curated bindings for critical symbols
* Compile-time validation tests using generated bindings
* Runtime smoke tests using generated cudart/nccl bindings

### Exit criteria

* generated bindings are good enough to replace most manual FFI declarations

---

## 13. Testing Strategy (Overall)

You asked specifically for testing procedures, so this section is intentionally detailed.

## 13.1 Test layers

### A. Unit tests

Fast tests for small components:

* lexer
* parser
* semantic/type checking
* struct layout logic
* FFI type mapping (`ffi_type` conversion)
* runtime error formatting

### B. Integration tests

Cross-component tests:

* compile + JIT + runtime call
* `.so` load + symbol lookup + FFI call
* pointer/out-param workflows
* generated code calling runtime APIs

### C. End-to-end tests

Run actual `.fusion` programs and check outputs:

* stdout
* exit code
* expected errors

### D. Environment-dependent smoke tests

For CUDA/NCCL:

* skip when libs/hardware unavailable
* run when environment is present
* report clear skip reason

---

## 13.2 Test matrix (recommended)

### Always run (CI-friendly)

* lexer/parser/sema
* LLVM codegen smoke
* runtime core
* `libm`/`libc` FFI tests (`strlen`, `cos`, `puts`)

### Optional/host-gated

* CUDA tests (requires `libcudart.so`)
* NCCL tests (requires `libnccl.so`)
* GPU-present tests (if actual device required)

---

## 13.3 Specific testing procedures by subsystem

## Compiler Frontend

### Procedure

1. Feed source string.
2. Assert token sequence.
3. Parse AST.
4. Assert AST shape/types.
5. Run semantic analysis and check diagnostics.

### Examples

* valid expression
* invalid syntax
* undefined identifier
* invalid FFI declaration type

---

## LLVM Codegen

### Procedure

1. Compile known input to LLVM IR.
2. Verify module (`llvm::verifyModule`).
3. JIT-execute.
4. Compare result/stdout.

### Checks

* module verification passes
* no unresolved runtime symbols
* expected runtime calls emitted

---

## Runtime Dynamic Loading

### Procedure

1. Attempt to load known library (`libm.so.6`).
2. Resolve known symbol (`cos`).
3. Resolve invalid symbol and assert error handling.
4. Close library.

### Checks

* handles are non-null on success
* errors contain path/symbol and `dlerror()` detail
* no crashes on cleanup

---

## FFI Invocation (libffi)

### Procedure

1. Build signature metadata.
2. Resolve symbol pointer.
3. Marshal arguments.
4. Call via `libffi`.
5. Verify return value.
6. Repeat call to test cache reuse.

### Checks

* primitive args/returns are correct
* pointer args are passed correctly
* repeated calls stable
* invalid signatures fail gracefully (not UB if avoidable)

---

## Struct Layout

### Procedure

1. Define struct in Fusion/binding metadata.
2. Cross-check with C helper library compiled for tests:

   * returns `sizeof(struct)`
   * returns `offsetof(field)`
3. Compare runtime/compiler-computed layout.

### Checks

* size matches
* alignment matches
* field offsets match exactly

This is **mandatory** before trusting CUDA/NCCL struct-heavy calls.

---

## CUDA/NCCL Smoke

### Procedure

1. Detect library availability.
2. Load library and resolve a small function set.
3. Perform a minimal call.
4. Check return code and error string.
5. Clean up any handles/resources.

### Checks

* symbols resolve
* return codes interpreted correctly
* no memory corruption/crash
* test skips cleanly if environment missing

---

## 13.4 Regression testing (important)

When you add support for a new FFI feature (e.g., pointer-to-pointer, struct field alignment), add:

* a focused runtime test
* at least one end-to-end language test
* one negative/error test

FFI regressions are subtle; lock them down early.

---

## 14. Safety and Reliability Considerations

Full FFI means unsafe behavior is possible. You should design for **controlled unsafety**.

## 14.1 Reality

Incorrect FFI declarations can crash the process.

## 14.2 Mitigations

* strict semantic checks on FFI declarations
* explicit type sizes only (no ambiguous “int” internally)
* layout validation helpers
* null checks in runtime wrappers
* descriptive runtime errors for load/symbol/signature issues
* optional debug mode with extra FFI validation/logging

## 14.3 Recommended Fusion design choice

Make FFI calls and raw pointer ops explicitly “unsafe” (syntactically or semantically), in Fusion.
This prevents accidental misuse and communicates risk.

---

## 15. Milestones and Deliverables

## Milestone 1 — LLVM + runtime print

* `print(1+2)` via JIT
* basic tests passing

## Milestone 2 — `.so` loading

* load `libm.so.6`
* resolve symbols
* runtime tests passing

## Milestone 3 — full primitive/pointer FFI

* `strlen`, `cos`, `puts` work through generic FFI
* end-to-end FFI syntax in Fusion

## Milestone 4 — structs + out-params + opaque handles

* custom test `.so` proves layout correctness
* pointer-heavy APIs work

## Milestone 5 — CUDA smoke

* basic `cudart` function calls succeed

## Milestone 6 — NCCL smoke

* basic NCCL function calls succeed

## Milestone 7 — bindgen (optional)

* generated bindings for a subset of CUDA/NCCL headers

---

## 16. Practical Advice for Fusion (NCCL + cudart)

Fusion, as a language that can drive NCCL/cudart, is still a serious systems project. The good news is this architecture is the right one.

### Best decisions you’ve already made

* Linux-only
* C runtime
* LLVM backend
* full FFI requirement

### Most important implementation discipline

Before doing real CUDA/NCCL work in Fusion, build a **small custom C test library** that intentionally exercises:

* opaque pointers
* pointer-to-pointer
* out-params
* structs
* strings
* error codes

If Fusion’s FFI passes that library cleanly, CUDA/NCCL integration becomes much more predictable.

---

## 17. Suggested First 2-Week Build Plan (Concrete)

If you want momentum, do this exact sequence:

### Week 1

1. Project/build setup
2. Lexer/parser for tiny expressions
3. LLVM JIT hello-world
4. C runtime `rt_print_i64`
5. End-to-end test `print(1+2)`

### Week 2

1. `rt_dlopen` / `rt_dlsym`
2. `libffi` integration in runtime
3. Call `strlen`, `cos` from runtime tests
4. Add minimal Fusion extern declaration syntax
5. End-to-end `.so` call from Fusion source

At the end of week 2, you’ll have the hardest architectural pieces proven for Fusion.