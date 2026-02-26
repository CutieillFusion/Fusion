#ifndef FUSION_RUNTIME_H
#define FUSION_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rt_init(void);

void rt_print_i64(int64_t value, int64_t stream);
void rt_print_f64(double value, int64_t stream);
void rt_print_cstring(const char *s, int64_t stream);

/* Read one line from stdin. Returns NUL-terminated buffer (Fusion-owned; invalid after next read_line). NULL on EOF/error. */
const char *rt_read_line(void);

/* Number to string. Returns NUL-terminated buffer (Fusion-owned; invalid after next to_str or read_line). */
const char *rt_to_str_i64(int64_t value);
const char *rt_to_str_f64(double value);

/* String to number. atoi/atof style; 0 / 0.0 on null/empty/invalid. */
int64_t rt_from_str_i64(const char *s);
double rt_from_str_f64(const char *s);

/* File I/O. Handle is opaque ptr; NULL = invalid. */
void *rt_open(const char *path, const char *mode);
void rt_close(void *handle);
const char *rt_read_line_file(void *handle);
void rt_write_file_i64(void *handle, int64_t value);
void rt_write_file_f64(void *handle, double value);
void rt_write_file_ptr(void *handle, const char *s);
int64_t rt_eof_file(void *handle);
int64_t rt_line_count_file(void *handle);

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
