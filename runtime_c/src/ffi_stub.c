/* Stub when libffi is not available at build time. FFI calls fail; rt_ffi_error_last() explains. */
#include "runtime.h"
#include <stddef.h>

static const char rt_ffi_stub_msg[] = "FFI not available: libffi not found at build time";

rt_ffi_sig_t *rt_ffi_sig_create(rt_ffi_type_kind_t return_kind, unsigned nargs,
                                const rt_ffi_type_kind_t *arg_kinds) {
  (void)return_kind;
  (void)nargs;
  (void)arg_kinds;
  return NULL;
}

int rt_ffi_call(rt_ffi_sig_t *sig, void *fnptr, const void *args_buf, void *ret_buf) {
  (void)sig;
  (void)fnptr;
  (void)args_buf;
  (void)ret_buf;
  return -1;
}

const char *rt_ffi_error_last(void) {
  return rt_ffi_stub_msg;
}
