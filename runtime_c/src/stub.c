#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>

void rt_init(void) {}

void rt_print_i64(int64_t value) {
  printf("%lld\n", (long long)value);
}

void rt_print_f64(double value) {
  printf("%g\n", value);
}

void rt_print_cstring(const char *s) {
  printf("%s\n", s ? s : "(null)");
}

void rt_panic(const char *msg) {
  if (msg)
    fprintf(stderr, "fusion panic: %s\n", msg);
  else
    fprintf(stderr, "fusion panic\n");
  abort();
}
