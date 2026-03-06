---
description: Use when adding a new feature to the Fusion compiler (lexer, parser, sema, codegen). TRIGGER when: user asks to add a builtin, expression, statement, type, or any language feature to the Fusion compiler pipeline.
---

# Fusion Compiler: Adding a New Feature

Use this workflow when adding a builtin, expression, statement, or other compiler feature to the Fusion language.

## Pipeline Overview

```
lex → parse → sema → codegen → JIT (LLVM ORC)
```

Key source files:
- `compiler/src/lexer.hpp` / `lexer.cpp` — TokenKind enum, lexer logic
- `compiler/src/ast.hpp` / `ast.cpp` — AST node kinds and factory methods
- `compiler/src/parser.cpp` — recursive descent parser
- `compiler/src/sema.cpp` — type checking and semantic validation
- `compiler/src/codegen.cpp` — LLVM IR emission
- `runtime_c/src/stub.c` — runtime C implementations
- `tests/compiler/test_sema.cpp` / `test_jit.cpp` — test suite

## Step-by-Step Process

### 1. Lexer (if new token needed)
- Add to `TokenKind` enum in `compiler/src/lexer.hpp`
- Add keyword string mapping in `lexer.cpp` (keyword table or switch)

### 2. AST
- Add `Expr::Kind::YourKind` or `Stmt::Kind::YourKind` in `compiler/src/ast.hpp`
- Add fields to `Expr` or `Stmt` struct as needed
- Add a factory method (e.g., `Expr::make_your_thing(...)`) in `ast.cpp`

### 3. Parser (`compiler/src/parser.cpp`)
- For expressions: add a case in `parse_primary()` or postfix/infix handling
- For statements: add a case in `parse_block()` or the statement dispatcher
- Use `consume_ptr_bracket()` if your syntax involves `ptr[T]`

### 4. Sema (`compiler/src/sema.cpp`)
- Add a case in `check_expr()` or the appropriate statement checker
- Push errors via `SemaResult.errors` (vector); `.error = errors[0]` for backward compat
- Note: sema NEVER sets error line/col (always 0) — this is intentional

### 5. Codegen (`compiler/src/codegen.cpp`)
- Add IR emission in `emit_expr()` or the statement emitter
- Use `s_codegen_error(msg)` for fatal codegen errors
- Access environment via `CodegenEnv` (variables, builder, module)
- Note: `expr_type()` returns `Void` for non-ptr Cast nodes (known discrepancy vs sema)

### 6. Tests
Add tests in:
- `tests/compiler/test_sema.cpp` — type error cases, multi-error cases
- `tests/compiler/test_jit.cpp` — end-to-end execution with stdout capture

JIT test stdout capture pattern:
```cpp
int saved = dup(STDOUT_FILENO);
freopen(tmp_path, "w", stdout);
// run JIT
fflush(stdout);
dup2(saved, STDOUT_FILENO);
close(saved);
// read tmp_path for expected output
```

### 7. Build and Test

**ALWAYS build with:**
```bash
./make.sh
```
Run from project root. This runs cmake + ctest together. Do not use cmake directly for final verification.

## Key Patterns

**FfiType enum** — used for pointer/type annotations:
- `FfiType::Ptr` — typed pointer (e.g., param `p: ptr[Value]`)
- `FfiType::Void` — struct field with named type

**ExprPtr / StmtPtr** — `std::unique_ptr<Expr>` / `std::unique_ptr<Stmt>`

**SemaResult:**
```cpp
SemaResult sr = check_expr(env, expr);
// sr.errors — all errors (vector)
// sr.error  — first error (backward compat alias)
// sr.type   — resolved type
```

**ptr[T] typed pointer syntax** (parser only):
- `fn f(p: ptr[Value])` — param type
- `struct S { next: ptr[Node]; }` — field type
- `x as ptr[Value]` — cast
- `heap(ptr[T])` / `stack(ptr[T])` — allocation
- `ptr[void]` → bare opaque ptr
- Helper: `consume_ptr_bracket()` in parser.cpp

## Known Quirks

- `as i32` codegen: `print(y)` where `y = x as i32` hits FPToSI bug; workaround: `print(y as i64)`
- `expr_type()` returns `Void` for non-ptr Cast (codegen vs sema discrepancy)
- Sema line/col in errors is always 0
- `addr_of` requires a VarRef argument, not a literal
- `ptr==ptr` and `ptr!=ptr` are allowed; ptr ordering (`<`, `<=`, `>`, `>=`) is rejected
- `as ptr` cast ONLY works ptr→ptr (not int→ptr)
- ASan/LeakSanitizer active: heap allocs in JIT programs must be freed
