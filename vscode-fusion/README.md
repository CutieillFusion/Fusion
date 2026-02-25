# Fusion language support for VS Code / Cursor

Syntax highlighting for `.fusion` files.

## Install

1. Open the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`).
2. Run **Developer: Install Extension from Location...**
3. Select this folder: `vscode-fusion` (the one containing `package.json`).
4. Reload the window if prompted.

`.fusion` files should now use Fusion highlighting. You can also choose "Fusion" from the language selector in the status bar.

**Brackets/parentheses:** The extension registers `()` and `{}` as bracket pairs. With **Editor: Bracket Pair Colorization** enabled (default in VS Code/Cursor), parentheses and braces are colored by nesting depth. Ensure the language is set to Fusion for the file.
