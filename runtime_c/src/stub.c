#include "runtime.h"
#include <stdio.h>

void rt_init(void) {}

void rt_print_i64(int64_t value) {
  printf("%lld\n", (long long)value);
}
