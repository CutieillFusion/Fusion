#ifndef FUSION_RUNTIME_H
#define FUSION_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rt_init(void);

void rt_print_i64(int64_t value);

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
 *   i32/i64/u32/u64/ptr/cstring -> one 8-byte slot; f32 -> 4 bytes in low half (or 8 for double); f64 -> 8 bytes. Order = arg order.
 * ret_buf: caller-allocated, 8 bytes for integer/pointer/double return; unused for void.
 */
typedef enum {
  RT_FFI_VOID,
  RT_FFI_I32,
  RT_FFI_I64,
  RT_FFI_F32,
  RT_FFI_F64,
  RT_FFI_PTR,
  RT_FFI_CSTRING
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
