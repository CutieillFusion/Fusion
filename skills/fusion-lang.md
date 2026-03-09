---
description: Reference for writing programs in the Fusion language. TRIGGER when: user asks to write, debug, or understand a Fusion program, or needs syntax/builtin reference.
---

# Fusion Language: Writing Fusion Programs

Use this as a reference when writing code in the Fusion language.

## Types

| Type  | Description              |
|-------|--------------------------|
| `i64` | 64-bit integer (default) |
| `i32` | 32-bit integer           |
| `f64` | 64-bit float             |
| `f32` | 32-bit float             |
| `str` | string                   |
| `ptr` | opaque pointer           |
| `ptr[T]` | typed pointer to T  |
| `void`| no value (return only)   |

## Syntax Reference

### Functions
```fusion
fn add(a: i64, b: i64) -> i64 {
    return a + b;
}
```

### Variables
```fusion
let x: i64 = 42;
let s: str = "hello";
```

### Control Flow
```fusion
if x > 0 {
    print("positive");
} else {
    print("non-positive");
}

while x > 0 {
    x = x - 1;
}
```

### Structs
```fusion
struct Point {
    x: i64;
    y: i64;
}

let p: Point = stack(Point);
p.x = 10;
p.y = 20;
```

### Casts
```fusion
let f: f64 = 3.14;
let i: i64 = f as i64;
```

## Builtins

### I/O
```fusion
print("hello");          // print string + newline
print(42);               // print i64
let s: str = read_line(); // read a line from stdin
flush(0);                 // flush stdout (0) or stderr (1)
```

### Terminal / TUI
```fusion
let key: i64 = read_key();         // read single keypress (raw mode); returns ASCII or 256=Up 257=Down 258=Right 259=Left
let h: i64 = terminal_height();    // terminal rows (0 if not a TTY)
let w: i64 = terminal_width();     // terminal cols (0 if not a TTY)
let c: str = chr(65);              // integer (0-255) to single-character string ("A")
```

### String Operations
```fusion
let s: str = "hello" + " world";   // string concatenation
let n: str = to_str(42);            // i64/f64 to string
let x: i64 = from_str(s, i64);     // string to i64
let f: f64 = from_str(s, f64);     // string to f64
let u: str = str_upper("hello");   // "HELLO"
let l: str = str_lower("HELLO");   // "hello"
let c: i64 = str_contains("hello world", "world"); // 1 (true) or 0 (false)
let t: str = str_strip("  hello  "); // "hello" (trim whitespace both ends)
let i: i64 = str_find("hello world", "world");     // 6 (byte offset, or -1 if not found)
let parts = str_split("a,b,c", ","); // array of strings: ["a", "b", "c"]
println(len(parts));               // 3
println(parts[0]);                 // "a"

// String equality (== and != compare by content, not pointer address)
if a == "hello" { print("match"); }
if a != b { print("different"); }
```

### HTTP
```fusion
// All methods: GET, POST, PUT, PATCH, DELETE, HEAD. Body must be "" for bodyless methods.
let body: str = http_request("GET", "https://example.com", "");
let code: i64 = http_status();   // last response status (e.g. 200); 0 if no request yet
if body != 0 {
    print(body);
}
// Response string is runtime-owned (reclaimed at shutdown). NULL on failure.
```

### Memory
```fusion
// Stack allocation
let p: Point = stack(Point);

// Heap allocation (must free!)
let p: ptr = heap(Point);
free(p);

// Heap array (must free!)
let arr: ptr = heap_array(i64, 10);   // 10-element i64 array
free_array(as_array(arr, i64));
```

### Pointer Operations
```fusion
let x: i64 = 42;
let p: ptr = addr_of(x);      // take address (VarRef only, not literals)
let v: i64 = load(p, i64);    // load value from pointer
store(p, 99);                  // store value through pointer
```

## ptr[T] Typed Pointer Syntax

Typed pointer annotations — same AST as bare types, just more readable:

```fusion
fn get_next(node: ptr[Node]) -> ptr[Node] {
    return node.next;
}

struct List {
    head: ptr[Node];
}

let p: ptr = heap(ptr[Node]);     // same as heap(Node)
let q: ptr = x as ptr[Node];     // cast to typed ptr
```

- `ptr[void]` is equivalent to bare `ptr` (opaque)
- Only valid in: param types, struct field types, casts, `heap()`/`stack()`
- Return type `-> ptr[T]` has different semantics (array element struct)

## Example Programs

### Hello World
```fusion
fn main() -> void {
    print("Hello, world!");
}
```

### Sum 1..N
```fusion
fn sum(n: i64) -> i64 {
    let acc: i64 = 0;
    let i: i64 = 1;
    while i <= n {
        acc = acc + i;
        i = i + 1;
    }
    return acc;
}

fn main() -> void {
    print(sum(100));
}
```

### Linked List
```fusion
struct Node {
    val: i64;
    next: ptr;
}

fn main() -> void {
    let a: ptr = heap(Node);
    let b: ptr = heap(Node);
    a.val = 1;
    b.val = 2;
    a.next = b;
    free(b);
    free(a);
}
```

## How to Run / Test

Programs go in `examples/` or `tests/`. Build and run all tests with:

```bash
./make.sh
```

Run from project root. This invokes cmake + ctest.

## Known Quirks and Gotchas

- **`as i32` bug**: `print(y)` where `y = x as i32` may hit FPToSI. Workaround: `print(y as i64)`
- **`addr_of`** only accepts a VarRef (variable name), not a literal
- **`as ptr`** cast only works ptr→ptr, not int→ptr
- **String `==`/`!=`**: compares by content (via `strcmp`) when both sides are `ptr[char]`/`str`. For other pointer types, `==`/`!=` compares pointer identity
- **ptr ordering**: `ptr < ptr` is rejected; `ptr == ptr` and `ptr != ptr` are allowed
- **`rt_read_line_file`** strips the trailing `\n` from input
- **`rt_line_count_file`** consumes the file handle (reads the entire file)
- **ASan/LeakSanitizer** is active: all heap allocations in JIT programs must be freed
- **String concat** uses `+` operator between `str` values
- **`from_str`** takes a type keyword as second argument: `from_str(s, i64)` not `from_str(s, "i64")`
