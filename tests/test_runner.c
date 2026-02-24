#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "runtime.h"

/* Return 1 if real FFI is available, 0 if stub (libffi not found at build time). */
static int ffi_available(void) {
  rt_ffi_sig_t *sig = rt_ffi_sig_create(RT_FFI_I64, 0, NULL);
  if (sig == NULL) {
    const char *err = rt_ffi_error_last();
    if (err && strstr(err, "not available") != NULL)
      return 0;
    return 0; /* other error also means we can't use FFI */
  }
  return 1;
}

static int test_rt_init(void) {
  rt_init();
  return 0;
}

static int test_rt_print_i64(void) {
  const char *path = "/tmp/fusion_rt_print_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  if (saved_fd < 0)
    return -1;
  if (!freopen(path, "w", stdout)) {
    close(saved_fd);
    return -1;
  }
  rt_print_i64(3);
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  if (!freopen("/dev/fd/1", "w", stdout))
    return -1;

  FILE *cap = fopen(path, "r");
  if (!cap)
    return -1;
  char buf[32];
  if (!fgets(buf, sizeof(buf), cap)) {
    fclose(cap);
    unlink(path);
    return -1;
  }
  fclose(cap);
  unlink(path);
  if (strcmp(buf, "3\n") != 0) {
    printf("FAIL: rt_print_i64(3) produced '%s' (expected '3\\n')\n", buf);
    return -1;
  }
  return 0;
}

static int test_rt_dlopen_invalid_path(void) {
  rt_lib_handle_t h = rt_dlopen("/nonexistent/path/libfoo.so");
  if (h != NULL) {
    printf("FAIL: rt_dlopen(invalid path) should return NULL\n");
    return -1;
  }
  const char *err = rt_dlerror_last();
  if (err == NULL || err[0] == '\0') {
    printf("FAIL: rt_dlerror_last() should be set after rt_dlopen failure\n");
    return -1;
  }
  if (strstr(err, "/nonexistent") == NULL && strstr(err, "libfoo") == NULL &&
      strstr(err, "cannot open") == NULL && strstr(err, "No such file") == NULL &&
      strstr(err, "not found") == NULL) {
    printf("FAIL: error message should contain path or 'cannot open' style context: %s\n", err);
    return -1;
  }
  return 0;
}

static int test_rt_dlsym_invalid_symbol(void) {
  rt_lib_handle_t h = rt_dlopen("libm.so.6");
  if (h == NULL) {
    printf("SKIP: libm.so.6 not loadable: %s\n", rt_dlerror_last() ? rt_dlerror_last() : "unknown");
    return 0;
  }
  void *sym = rt_dlsym(h, "NoSuchSymbol_xyz");
  if (sym != NULL) {
    rt_dlclose(h);
    printf("FAIL: rt_dlsym(NoSuchSymbol_xyz) should return NULL\n");
    return -1;
  }
  const char *err = rt_dlerror_last();
  if (err == NULL || err[0] == '\0') {
    rt_dlclose(h);
    printf("FAIL: rt_dlerror_last() should be set after rt_dlsym failure\n");
    return -1;
  }
  if (strstr(err, "NoSuchSymbol") == NULL && strstr(err, "symbol") == NULL) {
    printf("FAIL: error message should contain symbol context: %s\n", err);
    rt_dlclose(h);
    return -1;
  }
  if (rt_dlclose(h) != 0) {
    printf("FAIL: rt_dlclose failed\n");
    return -1;
  }
  return 0;
}

static int test_rt_dlopen_dlsym_libm_cos(void) {
  rt_lib_handle_t h = rt_dlopen("libm.so.6");
  if (h == NULL) {
    printf("FAIL: rt_dlopen(libm.so.6) failed: %s\n", rt_dlerror_last() ? rt_dlerror_last() : "unknown");
    return -1;
  }
  void *sym = rt_dlsym(h, "cos");
  if (sym == NULL) {
    rt_dlclose(h);
    printf("FAIL: rt_dlsym(cos) failed: %s\n", rt_dlerror_last() ? rt_dlerror_last() : "unknown");
    return -1;
  }
  double (*cos_fn)(double) = (double (*)(double))sym;
  double val = cos_fn(0.0);
  if (fabs(val - 1.0) > 1e-9) {
    rt_dlclose(h);
    printf("FAIL: cos(0.0) = %g, expected 1.0\n", val);
    return -1;
  }
  if (rt_dlclose(h) != 0) {
    printf("FAIL: rt_dlclose failed: %s\n", rt_dlerror_last() ? rt_dlerror_last() : "unknown");
    return -1;
  }
  return 0;
}

static int test_ffi_strlen(void) {
  if (!ffi_available()) {
    printf("SKIP: test_ffi_strlen (FFI not available)\n");
    return 0;
  }
  rt_ffi_type_kind_t arg_kinds[] = {RT_FFI_CSTRING};
  rt_ffi_sig_t *sig = rt_ffi_sig_create(RT_FFI_I64, 1, arg_kinds);
  if (sig == NULL) {
    printf("FAIL: test_ffi_strlen rt_ffi_sig_create: %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
    return -1;
  }
  const char *str = "hello";
  uint64_t args_buf[1];
  args_buf[0] = (uintptr_t)str;
  int64_t ret_val;
  if (rt_ffi_call(sig, (void *)strlen, args_buf, &ret_val) != 0) {
    printf("FAIL: test_ffi_strlen rt_ffi_call: %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
    return -1;
  }
  if (ret_val != 5) {
    printf("FAIL: test_ffi_strlen strlen(\"hello\") = %lld, expected 5\n", (long long)ret_val);
    return -1;
  }
  return 0;
}

static int test_ffi_cos(void) {
  if (!ffi_available()) {
    printf("SKIP: test_ffi_cos (FFI not available)\n");
    return 0;
  }
  rt_ffi_type_kind_t arg_kinds[] = {RT_FFI_F64};
  rt_ffi_sig_t *sig = rt_ffi_sig_create(RT_FFI_F64, 1, arg_kinds);
  if (sig == NULL) {
    printf("FAIL: test_ffi_cos rt_ffi_sig_create: %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
    return -1;
  }
  double arg = 0.0;
  uint64_t args_buf[1];
  memcpy(args_buf, &arg, sizeof(double));
  double ret_val;
  if (rt_ffi_call(sig, (void *)cos, args_buf, &ret_val) != 0) {
    printf("FAIL: test_ffi_cos rt_ffi_call: %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
    return -1;
  }
  if (fabs(ret_val - 1.0) > 1e-9) {
    printf("FAIL: test_ffi_cos cos(0.0) = %g, expected 1.0\n", ret_val);
    return -1;
  }
  return 0;
}

static int test_ffi_puts(void) {
  if (!ffi_available()) {
    printf("SKIP: test_ffi_puts (FFI not available)\n");
    return 0;
  }
  const char *path = "/tmp/fusion_ffi_puts_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  if (saved_fd < 0)
    return -1;
  if (!freopen(path, "w", stdout)) {
    close(saved_fd);
    return -1;
  }
  rt_ffi_type_kind_t arg_kinds[] = {RT_FFI_CSTRING};
  rt_ffi_sig_t *sig = rt_ffi_sig_create(RT_FFI_I32, 1, arg_kinds);
  if (sig == NULL) {
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);
    freopen("/dev/fd/1", "w", stdout);
    printf("FAIL: test_ffi_puts rt_ffi_sig_create: %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
    return -1;
  }
  const char *str = "fusion_puts_test";
  uint64_t args_buf[1];
  args_buf[0] = (uintptr_t)str;
  uint64_t ret_buf;
  if (rt_ffi_call(sig, (void *)puts, args_buf, &ret_buf) != 0) {
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);
    freopen("/dev/fd/1", "w", stdout);
    printf("FAIL: test_ffi_puts rt_ffi_call: %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
    return -1;
  }
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  if (!freopen("/dev/fd/1", "w", stdout)) {
    unlink(path);
    return -1;
  }
  FILE *cap = fopen(path, "r");
  if (!cap) {
    unlink(path);
    printf("FAIL: test_ffi_puts could not read capture file\n");
    return -1;
  }
  char buf[64];
  if (!fgets(buf, sizeof(buf), cap)) {
    fclose(cap);
    unlink(path);
    printf("FAIL: test_ffi_puts empty capture\n");
    return -1;
  }
  fclose(cap);
  unlink(path);
  if (strcmp(buf, "fusion_puts_test\n") != 0) {
    printf("FAIL: test_ffi_puts produced '%s' (expected 'fusion_puts_test\\n')\n", buf);
    return -1;
  }
  if ((int32_t)(ret_buf & 0xFFFFFFFFu) < 0) {
    printf("FAIL: test_ffi_puts puts return value should be non-negative\n");
    return -1;
  }
  return 0;
}

static int test_ffi_negative(void) {
  /* When FFI is stubbed: sig_create returns NULL and error mentions unavailability. */
  rt_ffi_sig_t *sig = rt_ffi_sig_create(RT_FFI_I64, 0, NULL);
  if (sig == NULL) {
    const char *err = rt_ffi_error_last();
    if (err && strstr(err, "not available") != NULL)
      return 0;
    printf("FAIL: test_ffi_negative sig_create failed without 'not available': %s\n", err ? err : "unknown");
    return -1;
  }
  /* Real FFI: run null/invalid-argument checks. */
  int64_t ret_buf = 0;
  if (rt_ffi_call(NULL, (void *)strlen, NULL, &ret_buf) == 0) {
    printf("FAIL: test_ffi_negative rt_ffi_call(NULL sig) should return -1\n");
    return -1;
  }
  if (!rt_ffi_error_last() || strstr(rt_ffi_error_last(), "null signature") == NULL) {
    printf("FAIL: test_ffi_negative expected error 'null signature': %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "null");
    return -1;
  }
  if (rt_ffi_call(sig, NULL, NULL, &ret_buf) == 0) {
    printf("FAIL: test_ffi_negative rt_ffi_call(NULL fnptr) should return -1\n");
    return -1;
  }
  if (!rt_ffi_error_last() || strstr(rt_ffi_error_last(), "null function pointer") == NULL) {
    printf("FAIL: test_ffi_negative expected error 'null function pointer': %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "null");
    return -1;
  }
  if (rt_ffi_call(sig, (void *)strlen, NULL, NULL) == 0) {
    printf("FAIL: test_ffi_negative rt_ffi_call(NULL ret_buf for non-void) should return -1\n");
    return -1;
  }
  if (!rt_ffi_error_last() || strstr(rt_ffi_error_last(), "null ret_buf") == NULL) {
    printf("FAIL: test_ffi_negative expected error 'null ret_buf': %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "null");
    return -1;
  }
  rt_ffi_type_kind_t arg_kinds[] = {RT_FFI_CSTRING};
  rt_ffi_sig_t *sig1 = rt_ffi_sig_create(RT_FFI_I64, 1, arg_kinds);
  if (sig1 == NULL) {
    printf("FAIL: test_ffi_negative sig_create(I64,1,CSTRING): %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
    return -1;
  }
  if (rt_ffi_call(sig1, (void *)strlen, NULL, &ret_buf) == 0) {
    printf("FAIL: test_ffi_negative rt_ffi_call(NULL args_buf when nargs>0) should return -1\n");
    return -1;
  }
  if (!rt_ffi_error_last() || strstr(rt_ffi_error_last(), "null args_buf") == NULL) {
    printf("FAIL: test_ffi_negative expected error 'null args_buf': %s\n", rt_ffi_error_last() ? rt_ffi_error_last() : "null");
    return -1;
  }
  return 0;
}

static int run_tests(void) {
  int failed = 0;
  if (test_rt_init() != 0) {
    printf("FAIL: test_rt_init\n");
    failed = 1;
  } else {
    printf("PASS: test_rt_init\n");
  }
  if (test_rt_print_i64() != 0) {
    printf("FAIL: test_rt_print_i64\n");
    failed = 1;
  } else {
    printf("PASS: test_rt_print_i64\n");
  }
  if (test_rt_dlopen_invalid_path() != 0) {
    printf("FAIL: test_rt_dlopen_invalid_path\n");
    failed = 1;
  } else {
    printf("PASS: test_rt_dlopen_invalid_path\n");
  }
  if (test_rt_dlsym_invalid_symbol() != 0) {
    printf("FAIL: test_rt_dlsym_invalid_symbol\n");
    failed = 1;
  } else {
    printf("PASS: test_rt_dlsym_invalid_symbol\n");
  }
  if (test_rt_dlopen_dlsym_libm_cos() != 0) {
    printf("FAIL: test_rt_dlopen_dlsym_libm_cos\n");
    failed = 1;
  } else {
    printf("PASS: test_rt_dlopen_dlsym_libm_cos\n");
  }
  if (test_ffi_negative() != 0) {
    printf("FAIL: test_ffi_negative\n");
    failed = 1;
  } else {
    printf("PASS: test_ffi_negative\n");
  }
  if (test_ffi_strlen() != 0) {
    printf("FAIL: test_ffi_strlen\n");
    failed = 1;
  } else {
    printf("PASS: test_ffi_strlen\n");
  }
  if (test_ffi_cos() != 0) {
    printf("FAIL: test_ffi_cos\n");
    failed = 1;
  } else {
    printf("PASS: test_ffi_cos\n");
  }
  if (test_ffi_puts() != 0) {
    printf("FAIL: test_ffi_puts\n");
    failed = 1;
  } else {
    printf("PASS: test_ffi_puts\n");
  }
  return failed;
}

int main(void) {
  int failed = run_tests();
  if (failed) {
    printf("Some tests failed.\n");
    return 1;
  }
  printf("All tests passed.\n");
  return 0;
}
