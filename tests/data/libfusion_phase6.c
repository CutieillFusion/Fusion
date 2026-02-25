/* Phase 6 test .so: struct, out-param, pointer-to-pointer. */
#include <stddef.h>
#include <stdint.h>

typedef struct Point {
  double x;
  double y;
} Point;

void point_set(Point* p, double x, double y) {
  if (p) {
    p->x = x;
    p->y = y;
  }
}

double point_x(Point* p) {
  return p ? p->x : 0.0;
}

double point_y(Point* p) {
  return p ? p->y : 0.0;
}

void set_int_out(int64_t* out, int64_t v) {
  if (out) *out = v;
}

void set_ptr_out(void** out, void* v) {
  if (out) *out = v;
}

/* Layout cross-check for compiler unit tests (C sizeof/offsetof). */
size_t fusion_test_sizeof_Point(void) {
  return sizeof(Point);
}

size_t fusion_test_offsetof_Point_x(void) {
  return offsetof(Point, x);
}

size_t fusion_test_offsetof_Point_y(void) {
  return offsetof(Point, y);
}
