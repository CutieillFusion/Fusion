#include <stdio.h>
#include <stdlib.h>
#include "runtime.h"

static int test_rt_init(void) {
  rt_init();
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
