#include "runtime.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

TEST(RuntimeBasicTests, Init) {
  rt_init();
  rt_shutdown();
}

TEST(RuntimeBasicTests, PrintI64) {
  const char* path = "/tmp/fusion_rt_print_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  rt_print_cstring(rt_to_str_i64(3), 0);
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));

  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  memset(buf, 0, sizeof(buf));
  size_t nread = fread(buf, 1, sizeof(buf) - 1, cap);
  ASSERT_GT(nread, 0u);
  fclose(cap);
  unlink(path);
  EXPECT_STREQ(buf, "3");
}
