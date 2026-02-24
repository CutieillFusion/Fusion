#include "runtime.h"
#include <ffi.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RT_FFI_ERRBUF_SIZE 256
#define RT_FFI_MAX_ARGS    32
#define RT_FFI_SLOT_SIZE   8
#define RT_FFI_CACHE_MAX  64

static char rt_ffi_error_buf[RT_FFI_ERRBUF_SIZE];

static void set_ffi_error(const char *msg) {
  size_t n = strlen(msg);
  if (n >= RT_FFI_ERRBUF_SIZE)
    n = RT_FFI_ERRBUF_SIZE - 1;
  memcpy(rt_ffi_error_buf, msg, n);
  rt_ffi_error_buf[n] = '\0';
}

static ffi_type *kind_to_ffi_type(rt_ffi_type_kind_t kind) {
  switch (kind) {
    case RT_FFI_VOID:    return &ffi_type_void;
    case RT_FFI_I32:     return &ffi_type_sint32;
    case RT_FFI_I64:     return &ffi_type_sint64;
    case RT_FFI_F32:     return &ffi_type_float;
    case RT_FFI_F64:     return &ffi_type_double;
    case RT_FFI_PTR:
    case RT_FFI_CSTRING: return &ffi_type_pointer;
    default:             return NULL;
  }
}

static int kind_valid(rt_ffi_type_kind_t kind) {
  return kind >= RT_FFI_VOID && kind <= RT_FFI_CSTRING;
}

struct rt_ffi_sig {
  ffi_cif cif;
  rt_ffi_type_kind_t return_kind;
  unsigned nargs;
  rt_ffi_type_kind_t arg_kinds[RT_FFI_MAX_ARGS];
  ffi_type *arg_types[RT_FFI_MAX_ARGS];
};

static int sig_matches(const struct rt_ffi_sig *s, rt_ffi_type_kind_t return_kind,
                       unsigned nargs, const rt_ffi_type_kind_t *arg_kinds) {
  if (s->return_kind != return_kind || s->nargs != nargs)
    return 0;
  for (unsigned i = 0; i < nargs; i++)
    if (s->arg_kinds[i] != arg_kinds[i])
      return 0;
  return 1;
}

static struct rt_ffi_sig *sig_cache[RT_FFI_CACHE_MAX];
static unsigned sig_cache_count;

rt_ffi_sig_t *rt_ffi_sig_create(rt_ffi_type_kind_t return_kind, unsigned nargs,
                                const rt_ffi_type_kind_t *arg_kinds) {
  rt_ffi_error_buf[0] = '\0';

  if (!kind_valid(return_kind)) {
    set_ffi_error("rt_ffi_sig_create: unsupported return type");
    return NULL;
  }
  if (nargs > RT_FFI_MAX_ARGS) {
    set_ffi_error("rt_ffi_sig_create: too many arguments");
    return NULL;
  }
  if (arg_kinds == NULL && nargs > 0) {
    set_ffi_error("rt_ffi_sig_create: null arg_kinds");
    return NULL;
  }
  for (unsigned i = 0; i < nargs; i++) {
    if (!kind_valid(arg_kinds[i])) {
      set_ffi_error("rt_ffi_sig_create: unsupported argument type");
      return NULL;
    }
  }

  for (unsigned i = 0; i < sig_cache_count; i++) {
    if (sig_matches(sig_cache[i], return_kind, nargs, arg_kinds))
      return (rt_ffi_sig_t *)sig_cache[i];
  }

  if (sig_cache_count >= RT_FFI_CACHE_MAX) {
    set_ffi_error("rt_ffi_sig_create: signature cache full");
    return NULL;
  }

  static struct rt_ffi_sig sig_storage[RT_FFI_CACHE_MAX];
  struct rt_ffi_sig *s = &sig_storage[sig_cache_count];
  s->return_kind = return_kind;
  s->nargs = nargs;
  for (unsigned i = 0; i < nargs; i++) {
    s->arg_kinds[i] = arg_kinds[i];
    s->arg_types[i] = kind_to_ffi_type(arg_kinds[i]);
  }

  ffi_type *rtype = kind_to_ffi_type(return_kind);
  ffi_status status = ffi_prep_cif(&s->cif, FFI_DEFAULT_ABI, nargs, rtype,
                                  nargs > 0 ? s->arg_types : NULL);
  if (status != FFI_OK) {
    set_ffi_error("rt_ffi_sig_create: ffi_prep_cif failed");
    return NULL;
  }

  sig_cache[sig_cache_count++] = s;
  return (rt_ffi_sig_t *)s;
}

int rt_ffi_call(rt_ffi_sig_t *sig, void *fnptr, const void *args_buf, void *ret_buf) {
  rt_ffi_error_buf[0] = '\0';

  if (sig == NULL) {
    set_ffi_error("rt_ffi_call: null signature");
    return -1;
  }
  if (fnptr == NULL) {
    set_ffi_error("rt_ffi_call: null function pointer");
    return -1;
  }

  struct rt_ffi_sig *s = (struct rt_ffi_sig *)sig;
  unsigned nargs = s->nargs;

  if (nargs > 0 && args_buf == NULL) {
    set_ffi_error("rt_ffi_call: null args_buf");
    return -1;
  }

  uint8_t ret_storage[RT_FFI_SLOT_SIZE];
  void *rvalue = (s->return_kind == RT_FFI_VOID) ? ret_storage : ret_buf;
  if (rvalue == NULL && s->return_kind != RT_FFI_VOID) {
    set_ffi_error("rt_ffi_call: null ret_buf for non-void return");
    return -1;
  }

  void *avalues[RT_FFI_MAX_ARGS];
  const char *p = (const char *)args_buf;
  for (unsigned i = 0; i < nargs; i++)
    avalues[i] = (void *)(p + i * RT_FFI_SLOT_SIZE);

  ffi_call(&s->cif, FFI_FN(fnptr), rvalue, avalues);
  return 0;
}

const char *rt_ffi_error_last(void) {
  return rt_ffi_error_buf[0] ? rt_ffi_error_buf : NULL;
}
