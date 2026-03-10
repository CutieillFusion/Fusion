#ifndef FUSION_RUNTIME_H
#define FUSION_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rt_init(void);
/* Free runtime-owned resources (e.g., string arena for rt_str_*). Safe to call multiple times. */
void rt_shutdown(void);

void rt_print_cstring(const char *s, int64_t stream);

/* Read one line from stdin. Returns NUL-terminated buffer (Fusion-owned; invalid after next read_line). NULL on EOF/error. */
const char *rt_read_line(void);

/* Read a single keypress (raw mode). Returns ASCII code (1-255), or 256=Up, 257=Down, 258=Right, 259=Left. 0 on EOF/error. */
int64_t rt_read_key(void);

/* Terminal dimensions. Returns 0 on failure or non-TTY. */
int64_t rt_terminal_height(void);
int64_t rt_terminal_width(void);

/* Flush a stream: 0 = stdout, 1 = stderr. */
void rt_flush(int64_t stream);

/* Sleep for the given number of milliseconds. */
void rt_sleep(int64_t milliseconds);

/* Convert integer (0-255) to a single-character NUL-terminated string. Runtime-owned. */
const char *rt_chr(int64_t code);

/* Number to string. Returns NUL-terminated buffer (Fusion-owned; invalid after next to_str or read_line). */
const char *rt_to_str_i64(int64_t value);
const char *rt_to_str_f64(double value);

/* String to number. atoi/atof style; 0 / 0.0 on null/empty/invalid. */
int64_t rt_from_str_i64(const char *s);
double rt_from_str_f64(const char *s);

/* Concatenate two strings.
 * Returns heap-allocated NUL-terminated string that is owned by the runtime.
 * Callers MUST NOT free the returned pointer; memory is reclaimed by rt_shutdown().
 */
const char *rt_str_concat(const char *a, const char *b);

/* Copy a string.
 * Returns heap-allocated NUL-terminated copy that is owned by the runtime.
 * Callers MUST NOT free the returned pointer; memory is reclaimed by rt_shutdown().
 * Used so left operand of + is preserved before right is evaluated (e.g. to_str(x) + to_str(y)).
 */
const char *rt_str_dup(const char *s);

/* String operations. All return runtime-owned memory (reclaimed by rt_shutdown()). */
const char *rt_str_upper(const char *s);
const char *rt_str_lower(const char *s);
int64_t rt_str_contains(const char *haystack, const char *needle);
const char *rt_str_strip(const char *s);
int64_t rt_str_find(const char *haystack, const char *needle);
const char *rt_str_split(const char *s, const char *delim);
int64_t rt_str_eq(const char *a, const char *b);

/* Register a heap-allocated string for rt_shutdown() to free. For use by runtime modules (e.g. http.c). */
void rt_track_string(char *p);

/* File I/O. Handle is opaque ptr; NULL = invalid. */
void *rt_open(const char *path, const char *mode);
void rt_close(void *handle);
const char *rt_read_line_file(void *handle);
void rt_write_file_ptr(void *handle, const char *s);
/* Raw byte I/O. buf = buffer, count = number of bytes. Return bytes written/read, or -1 on error. */
int64_t rt_write_bytes(void *handle, const void *buf, int64_t count);
int64_t rt_read_bytes(void *handle, void *buf, int64_t count);
int64_t rt_eof_file(void *handle);
int64_t rt_line_count_file(void *handle);

/* HTTP (libcurl). method/url/body are NUL-terminated; body may be NULL or "" for bodyless methods.
 * Returns response body (runtime-owned; reclaimed by rt_shutdown()) or NULL on failure. */
const char *rt_http_request(const char *method, const char *url, const char *body);
/* Last HTTP status code from rt_http_request (0 if no request made yet). Not thread-safe; Fusion is single-threaded. */
int64_t rt_http_status(void);

/* Print message to stderr and abort. Used when dlopen/dlsym/ffi_call fails. */
void rt_panic(const char *msg);

/* Dynamic loading (dlopen/dlsym). Handles are opaque pointers from dlopen/dlsym. */
typedef void *rt_lib_handle_t;

/* Load .so; returns handle or NULL on failure. On failure, rt_dlerror_last() describes the error (path context). */
rt_lib_handle_t rt_dlopen(const char *path);

/* Resolve symbol in library; returns function/data pointer or NULL on failure. On failure, rt_dlerror_last() describes (symbol context). */
void *rt_dlsym(rt_lib_handle_t handle, const char *symbol_name);

/* Close library. Returns 0 on success, -1 on failure. On failure, rt_dlerror_last() has details. */
int rt_dlclose(rt_lib_handle_t handle);

/* Last error string from dlopen/dlsym/dlclose. Lifetime: until next dl call. Not thread-safe in v1. */
const char *rt_dlerror_last(void);

/* --- FFI (libffi) ---
 * args_buf: array of argument slots; each slot 8 bytes (pointer-sized), 8-byte aligned.
 *   i32/i64/u32/u64/ptr -> one 8-byte slot; f32 -> 4 bytes in low half (or 8 for double); f64 -> 8 bytes. Order = arg order.
 * ret_buf: caller-allocated, 8 bytes for integer/pointer/double return; unused for void.
 */
typedef enum {
  RT_FFI_VOID,
  RT_FFI_I32,
  RT_FFI_I64,
  RT_FFI_F32,
  RT_FFI_F64,
  RT_FFI_PTR
} rt_ffi_type_kind_t;

typedef struct rt_ffi_sig rt_ffi_sig_t;

/* Create (or return cached) signature. Returns NULL on error (unsupported type). Caller does not free. */
rt_ffi_sig_t *rt_ffi_sig_create(rt_ffi_type_kind_t return_kind, unsigned nargs, const rt_ffi_type_kind_t *arg_kinds);

/* Call foreign function. Returns 0 on success, non-zero on failure. On failure, rt_ffi_error_last() describes. */
int rt_ffi_call(rt_ffi_sig_t *sig, void *fnptr, const void *args_buf, void *ret_buf);

/* Last FFI error string. Lifetime: until next FFI call or sig create. Not thread-safe in v1. */
const char *rt_ffi_error_last(void);

#ifdef __cplusplus
}
#endif

#endif /* FUSION_RUNTIME_H */
