#include "runtime.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

TEST(RuntimeDlTests, DlopenInvalidPath) {
  rt_lib_handle_t h = rt_dlopen("/nonexistent/path/libfoo.so");
  EXPECT_EQ(h, nullptr);
  const char* err = rt_dlerror_last();
  ASSERT_NE(err, nullptr);
  ASSERT_NE(err[0], '\0');
  EXPECT_TRUE(strstr(err, "/nonexistent") != nullptr || strstr(err, "libfoo") != nullptr ||
              strstr(err, "cannot open") != nullptr || strstr(err, "No such file") != nullptr ||
              strstr(err, "not found") != nullptr);
}

TEST(RuntimeDlTests, DlsymInvalidSymbol) {
  rt_lib_handle_t h = rt_dlopen("libm.so.6");
  if (h == nullptr) {
    GTEST_SKIP() << "libm.so.6 not loadable: " << (rt_dlerror_last() ? rt_dlerror_last() : "unknown");
  }
  void* sym = rt_dlsym(h, "NoSuchSymbol_xyz");
  EXPECT_EQ(sym, nullptr);
  const char* err = rt_dlerror_last();
  ASSERT_NE(err, nullptr);
  ASSERT_NE(err[0], '\0');
  EXPECT_TRUE(strstr(err, "NoSuchSymbol") != nullptr || strstr(err, "symbol") != nullptr);
  EXPECT_EQ(rt_dlclose(h), 0);
}

TEST(RuntimeDlTests, DlopenDlsymLibmCos) {
  rt_lib_handle_t h = rt_dlopen("libm.so.6");
  ASSERT_NE(h, nullptr) << (rt_dlerror_last() ? rt_dlerror_last() : "unknown");
  void* sym = rt_dlsym(h, "cos");
  ASSERT_NE(sym, nullptr) << (rt_dlerror_last() ? rt_dlerror_last() : "unknown");
  double (*cos_fn)(double) = reinterpret_cast<double (*)(double)>(sym);
  double val = cos_fn(0.0);
  EXPECT_NEAR(val, 1.0, 1e-9);
  EXPECT_EQ(rt_dlclose(h), 0);
}
