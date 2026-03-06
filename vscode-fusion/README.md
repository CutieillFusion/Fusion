# Fusion language support for VS Code / Cursor

Full language support for `.fusion` files: syntax highlighting, semantic tokens, and LSP-powered code intelligence.

## Features

- Syntax highlighting and bracket colorization
- Semantic token coloring (keywords, types, functions, variables, parameters)
- Diagnostics (errors/warnings from the compiler, with multi-error squiggle support)
- Hover documentation
- Go to definition
- Document and workspace symbols
- Completion (keywords, functions, variables)
- Signature help
- Document highlight and references
- Rename symbol
- Folding ranges
- Inlay hints (parameter names at call sites)

## Install

1. Open the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`).
2. Run **Developer: Install Extension from Location...**
3. Select the `vscode-fusion` folder (the one containing `package.json`).
4. Reload the window if prompted.

`.fusion` files will use Fusion syntax highlighting and LSP features automatically.

## LSP server

The extension auto-detects `build/fusion_lsp` relative to the workspace root. To use a custom binary location, set `fusion.serverPath` in VS Code settings to the absolute path of `fusion_lsp`.

To build the LSP server:

```bash
cmake --build build --target fusion_lsp
```

## Requirements

- VS Code 1.74+ or Cursor
- `fusion_lsp` binary built from source (see project README)
