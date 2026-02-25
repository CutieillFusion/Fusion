#include "runtime.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>

static bool ffi_available() {
  rt_ffi_sig_t* sig = rt_ffi_sig_create(RT_FFI_I64, 0, nullptr);
  if (sig == nullptr) {
    const char* err = rt_ffi_error_last();
    return err && strstr(err, "not available") != nullptr ? false : false;
  }
  return true;
}

TEST(RuntimeFfiTests, FfiNegative) {
  rt_ffi_sig_t* sig = rt_ffi_sig_create(RT_FFI_I64, 0, nullptr);
  if (sig == nullptr) {
    const char* err = rt_ffi_error_last();
    if (err && strstr(err, "not available") != nullptr)
      GTEST_SKIP() << "FFI not available (stub)";
    FAIL() << "rt_ffi_sig_create failed: " << (err ? err : "unknown");
  }
  int64_t ret_buf = 0;
  EXPECT_NE(rt_ffi_call(nullptr, (void*)strlen, nullptr, &ret_buf), 0);
  EXPECT_TRUE(rt_ffi_error_last() && strstr(rt_ffi_error_last(), "null signature"));

  EXPECT_NE(rt_ffi_call(sig, nullptr, nullptr, &ret_buf), 0);
  EXPECT_TRUE(rt_ffi_error_last() && strstr(rt_ffi_error_last(), "null function pointer"));

  EXPECT_NE(rt_ffi_call(sig, (void*)strlen, nullptr, nullptr), 0);
  EXPECT_TRUE(rt_ffi_error_last() && strstr(rt_ffi_error_last(), "null ret_buf"));

  rt_ffi_type_kind_t arg_kinds[] = {RT_FFI_PTR};
  rt_ffi_sig_t* sig1 = rt_ffi_sig_create(RT_FFI_I64, 1, arg_kinds);
  ASSERT_NE(sig1, nullptr);
  EXPECT_NE(rt_ffi_call(sig1, (void*)strlen, nullptr, &ret_buf), 0);
  EXPECT_TRUE(rt_ffi_error_last() && strstr(rt_ffi_error_last(), "null args_buf"));
}

TEST(RuntimeFfiTests, CallStrlen) {
  if (!ffi_available())
    GTEST_SKIP() << "FFI not available";
  rt_ffi_type_kind_t arg_kinds[] = {RT_FFI_PTR};
  rt_ffi_sig_t* sig = rt_ffi_sig_create(RT_FFI_I64, 1, arg_kinds);
  ASSERT_NE(sig, nullptr) << (rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
  const char* str = "hello";
  uint64_t args_buf[1];
  args_buf[0] = reinterpret_cast<uintptr_t>(str);
  int64_t ret_val;
  EXPECT_EQ(rt_ffi_call(sig, (void*)strlen, args_buf, &ret_val), 0) << (rt_ffi_error_last() ? rt_ffi_error_last() : "");
  EXPECT_EQ(ret_val, 5);
}

TEST(RuntimeFfiTests, CallCos) {
  if (!ffi_available())
    GTEST_SKIP() << "FFI not available";
  rt_ffi_type_kind_t arg_kinds[] = {RT_FFI_F64};
  rt_ffi_sig_t* sig = rt_ffi_sig_create(RT_FFI_F64, 1, arg_kinds);
  ASSERT_NE(sig, nullptr) << (rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
  double arg = 0.0;
  uint64_t args_buf[1];
  memcpy(args_buf, &arg, sizeof(double));
  double ret_val;
  EXPECT_EQ(rt_ffi_call(sig, (void*)cos, args_buf, &ret_val), 0) << (rt_ffi_error_last() ? rt_ffi_error_last() : "");
  EXPECT_NEAR(ret_val, 1.0, 1e-9);
}

TEST(RuntimeFfiTests, CallPuts) {
  if (!ffi_available())
    GTEST_SKIP() << "FFI not available";
  const char* path = "/tmp/fusion_ffi_puts_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  rt_ffi_type_kind_t arg_kinds[] = {RT_FFI_PTR};
  rt_ffi_sig_t* sig = rt_ffi_sig_create(RT_FFI_I32, 1, arg_kinds);
  ASSERT_NE(sig, nullptr) << (rt_ffi_error_last() ? rt_ffi_error_last() : "unknown");
  const char* str = "fusion_puts_test";
  uint64_t args_buf[1];
  args_buf[0] = reinterpret_cast<uintptr_t>(str);
  uint64_t ret_buf;
  EXPECT_EQ(rt_ffi_call(sig, (void*)puts, args_buf, &ret_buf), 0) << (rt_ffi_error_last() ? rt_ffi_error_last() : "");
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));

  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_STREQ(buf, "fusion_puts_test\n");
  EXPECT_GE(static_cast<int32_t>(ret_buf & 0xFFFFFFFFu), 0);
}

TEST(RuntimeFfiTests, Phase6LayoutCrossCheck) {
  const char* so_path = std::getenv("FUSION_PHASE6_SO");
  if (!so_path) so_path = "./fusion_phase6.so";
  void* handle = dlopen(so_path, RTLD_NOW);
  if (!handle) GTEST_SKIP() << "dlopen failed: " << dlerror();
  auto sizeof_fn = (size_t(*)())dlsym(handle, "fusion_test_sizeof_Point");
  auto offsetof_x = (size_t(*)())dlsym(handle, "fusion_test_offsetof_Point_x");
  auto offsetof_y = (size_t(*)())dlsym(handle, "fusion_test_offsetof_Point_y");
  if (!sizeof_fn || !offsetof_x || !offsetof_y) {
    dlclose(handle);
    GTEST_SKIP() << "layout symbols not found";
  }
  EXPECT_EQ(sizeof_fn(), 16u) << "C sizeof(Point) should be 16";
  EXPECT_EQ(offsetof_x(), 0u) << "offsetof(Point, x) should be 0";
  EXPECT_EQ(offsetof_y(), 8u) << "offsetof(Point, y) should be 8";
  dlclose(handle);
}
