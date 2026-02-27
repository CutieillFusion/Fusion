# Fusion native IO examples

These examples demonstrate every native IO built-in. Use the list below to find the right file.

| File | Built-ins demonstrated |
|------|------------------------|
| [print.fusion](print.fusion) | `print` (i64, f64, ptr, optional stream) |
| [stdin.fusion](stdin.fusion) | `read_line` |
| [string_conversion.fusion](string_conversion.fusion) | `to_str`, `from_str` |
| [file_read.fusion](file_read.fusion) | `open`, `read_line_file`, `eof_file`, `line_count_file`, `close` |
| [file_write.fusion](file_write.fusion) | `open`, `write_file` (i64, f64, ptr), `close` |
| [roundtrip.fusion](roundtrip.fusion) | `read_line`, `print`, `open`, `write_file`, `close` (stdin → file) |

For file_read.fusion, run file_write.fusion first to create `out.txt`, or use an existing file path.
