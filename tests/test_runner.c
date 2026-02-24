#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "runtime.h"

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
