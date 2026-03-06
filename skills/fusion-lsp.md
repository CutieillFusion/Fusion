---
description: Use when adding a new LSP feature to the Fusion language server. TRIGGER when: user asks to add or modify hover, completion, rename, diagnostics, or other LSP capabilities.
---

# Fusion LSP: Adding a New Language Server Feature

Use this workflow when adding a new LSP feature (hover, completion, rename, etc.) to the Fusion language server.

## Architecture Overview

```
vscode-fusion/ (TypeScript extension)
       ↕ JSON-RPC over stdio
lsp/src/ (C++ language server)
       ↕ fusion_frontend (no LLVM)
compiler/src/ (parser, sema, ast — shared)
```

Key source files:
- `lsp/src/server.cpp` — request dispatcher, text sync, URI handling
- `lsp/src/features.cpp` — all LSP feature implementations
- `lsp/src/analysis.cpp` — file analysis (parse + sema), diagnostic emission
- `lsp/src/index.cpp` / `index.hpp` — `FileIndex`, `SymInfo` symbol table
- `vscode-fusion/` — VS Code extension (TypeScript, vscode-languageclient v9)

## Step-by-Step Process

### 1. Implement the Feature (`lsp/src/features.cpp`)

Add a function:
```cpp
nlohmann::json feature_your_thing(const FileIndex& idx, const nlohmann::json& params) {
    // Extract document URI and position from params
    // Use token_at(), best_sym(), analyze_file() as needed
    // Return JSON per LSP spec
}
```

Key helpers:
- `token_at(file_index, uri, line, col)` — find token at cursor
- `best_sym(file_index, name)` — look up symbol by name
- `analyze_file(uri, source)` — parse + sema, returns diagnostics and index data

### 2. Register the Handler (`lsp/src/server.cpp`)

In `handle_request()`, add:
```cpp
if (method == "textDocument/yourThing") {
    return feature_your_thing(index_, params);
}
```

### 3. Declare Server Capability (if needed)

In `server.cpp`'s `initialize` response, add to `capabilities`:
```json
"yourThingProvider": true
```

### 4. Update VS Code Extension (if needed)

If the feature requires client-side registration or a new command:
- Edit `vscode-fusion/src/extension.ts`
- Declare capability in `clientOptions` or `serverOptions`
- Run `npm run compile` in `vscode-fusion/` to rebuild the extension

### 5. Build

For fast LSP-only iteration:
```bash
cmake --build build --target fusion_lsp
```

For full build + all tests (REQUIRED before finishing):
```bash
./make.sh
```

Always run `./make.sh` from project root for final verification.

## Key Patterns

**Text document sync:**
- `TextDocumentSyncKind.Incremental` (value `2`) — server receives incremental edits
- `apply_text_edit()` in `server.cpp` applies edits to the in-memory document store

**URI handling:**
- `uri_to_path()` in `server.cpp` strips `file:///` prefix and decodes percent-encoding

**Diagnostics (analysis.cpp):**
- Calls `resolve_imports_and_merge` before sema (import errors silently ignored)
- Iterates `sr.errors` to emit one diagnostic per error (multi-squiggle support)
- Sema never sets error line/col (always 0); analysis.cpp falls back to first code token

**Semantic token legend:**
| Index | Token Type |
|-------|-----------|
| 0     | keyword   |
| 1     | type      |
| 2     | function  |
| 3     | variable  |
| 4     | parameter |

## Implemented Features (reference)

- hover, definition, document symbols
- completion, signature help
- document highlight, references
- prepare-rename, rename
- semantic tokens, workspace symbols
- folding ranges, inlay hints (parameter names at call sites)

## Known Quirks

- Sema line/col is always 0 — analysis.cpp falls back to first code token for squiggle placement
- `fusion_frontend` target excludes LLVM (LSP must not link LLVM)
- Build LSP target: `cmake --build build --target fusion_lsp`
- Multi-error support: `sr.errors` is a vector; iterate all of them in analysis.cpp
