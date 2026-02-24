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
