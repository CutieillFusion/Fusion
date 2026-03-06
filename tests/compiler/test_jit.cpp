#include "ast.hpp"
#include "codegen.hpp"
#include "layout.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

#ifdef FUSION_HAVE_LLVM
#include <llvm/IR/LLVMContext.h>

TEST(JitTests, ExecutesPrintOnePlusTwo) {
  auto tokens = fusion::lex("print(1+2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesSub) {
  auto tokens = fusion::lex("print(5-2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesLetAndForOnly) {
  auto tokens = fusion::lex("let n = 1; for (let i = 0; i < n; i = i + 1) { }");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesMulAndDiv) {
  auto tokens = fusion::lex("print(3*4); print(10/2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesCos) {
  auto tokens = fusion::lex("extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; print(cos(0.0))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesLetPrint) {
  auto tokens = fusion::lex("let x = 1 + 2; print(x)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesLetCos) {
  auto tokens = fusion::lex("extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; let x = cos(0.0); print(x)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, MixedFloatIntAdditionProducesF64) {
  /* let z = cos(1.0); let x = z + 2; print(x) => result must be F64 (2.540302...), not truncated to 2 */
  const char* path = "/tmp/fusion_jit_mixed_add_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; let z = cos(1.0); let x = z + 2; print(x)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;

  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  double value = std::atof(buf);
  EXPECT_NEAR(value, 2.5403023058681398, 0.0001)
      << "mixed f64+i64 add should yield cos(1.0)+2 ~= 2.540302, got " << buf;
}

TEST(JitTests, IntegerAdditionStillProducesI64) {
  /* let a = 1; let b = 2; print(a + b) => must still print 3 (int+int unchanged) */
  const char* path = "/tmp/fusion_jit_int_add_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let a = 1; let b = 2; print(a + b)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;

  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_STREQ(buf, "3\n") << "int+int should still print 3";
}

TEST(JitTests, ExecutesOutParam) {
  /* Use relative path: tests run with WORKING_DIRECTORY=${CMAKE_BINARY_DIR}, .so is there */
  const char* so_path = "./fusion_phase6.so";
  std::string src = "extern lib \"";
  src += so_path;
  src += "\"; extern fn set_int_out(out: ptr[void], v: i64) -> void; let p = heap(i64); store(p, 0); set_int_out(p, 42); print(load(p)); free(as_heap(p))";
  auto tokens = fusion::lex(src);
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed: " << parse_result.error.message << " at " << parse_result.error.line << ":" << parse_result.error.column << " src=" << src;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesStructByPointer) {
  const char* so_path = "./fusion_phase6.so";
  std::string src = "struct Point { x: f64; y: f64; }; extern lib \"";
  src += so_path;
  src += "\"; extern fn point_set(p: Point, x: f64, y: f64) -> void; extern fn point_x(p: Point) -> f64; let p = heap(Point); point_set(p, 1.0, 2.0); print(point_x(p)); free(as_heap(p))";
  auto tokens = fusion::lex(src);
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed: " << parse_result.error.message << " at " << parse_result.error.line << ":" << parse_result.error.column;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesLoadField) {
  const char* so_path = "./fusion_phase6.so";
  std::string src = "struct Point { x: f64; y: f64; }; extern lib \"";
  src += so_path;
  src += "\"; extern fn point_set(p: Point, x: f64, y: f64) -> void; let p = heap(Point); point_set(p, 3.0, 4.0); print(p.x); print(p.y); free(as_heap(p))";
  auto tokens = fusion::lex(src);
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed: " << parse_result.error.message << " at " << parse_result.error.line << ":" << parse_result.error.column;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesCallThroughStructField) {
  /* structs.fusion-style: store get_func_ptr(add/mul) in Operation.func, call via op.func. */
  const char* path = "/tmp/fusion_jit_indirect_call.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct Operation { func: ptr[void]; x: f64; y: f64; }; "
      "fn add(x: f64, y: f64) -> f64 { return x + y; } "
      "fn mul(x: f64, y: f64) -> f64 { return x * y; } "
      "fn perform_operation(op: Operation) -> f64 { "
      "  let func = op.func; "
      "  let x = op.x; "
      "  let y = op.y; "
      "  return call(func, x, y); "
      "} "
      "let op_add = heap(Operation); "
      "op_add.func = get_func_ptr(add); "
      "op_add.x = 3.0; "
      "op_add.y = 4.0; "
      "let op_mul = heap(Operation); "
      "op_mul.func = get_func_ptr(mul); "
      "op_mul.x = 3.0; "
      "op_mul.y = 4.0; "
      "print(perform_operation(op_add)); "
      "print(perform_operation(op_mul)); "
      "free(as_heap(op_add)); free(as_heap(op_mul))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_TRUE(std::atof(buf) == 7.0 || std::atof(buf) == 12.0) << "first line should be 7 or 12, got " << buf;
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_TRUE(std::atof(buf) == 7.0 || std::atof(buf) == 12.0) << "second line should be 7 or 12, got " << buf;
  fclose(cap);
  unlink(path);
}

TEST(JitTests, AllocArrayHeapEscapesFunction) {
  /* value.fusion-style: array allocated in add_forward is stored in Value.prev and read in add_backward.
     Requires heap_array to be heap-allocated; with stack allocation the pointer would be dangling. */
  const char* path = "/tmp/fusion_jit_array_escape_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct Value { data: f64; grad: f64; prev: ptr[void]; children_count: i64; backward: ptr[void]; }; "
      "fn alloc_value(data: f64, prev: ptr[void], children_count: i64, backward: ptr[void]) -> ptr[void] { "
      "  let value = heap(Value); "
      "  value.data = data; "
      "  value.grad = 0.0; "
      "  value.prev = prev; "
      "  value.children_count = children_count; "
      "  value.backward = backward; "
      "  return value; "
      "} "
      "fn leaf_backward(v: ptr[void]) -> void { } "
      "fn add_backward(out: ptr[void]) -> void { "
      "  let prev = (out as Value).prev; "
      "  let a = prev[0] as Value; "
      "  let b = prev[1] as Value; "
      "  let grad = (out as Value).grad; "
      "  let a_grad = a.grad; "
      "  let b_grad = b.grad; "
      "  a.grad = a_grad + grad; "
      "  b.grad = b_grad + grad; "
      "} "
      "fn add_forward(a: ptr[void], b: ptr[void]) -> ptr[void] { "
      "  let data = (a as Value).data + (b as Value).data; "
      "  let prev = heap_array(ptr[void], 2); "
      "  prev[0] = a; "
      "  prev[1] = b; "
      "  return alloc_value(data, prev, 2, get_func_ptr(add_backward)); "
      "} "
      "let a = alloc_value(1.0, heap_array(ptr[void], 0), 0, get_func_ptr(leaf_backward)); "
      "let b = alloc_value(2.0, heap_array(ptr[void], 0), 0, get_func_ptr(leaf_backward)); "
      "(a as Value).grad = 1.0; "
      "(b as Value).grad = 2.0; "
      "let c = add_forward(a, b); "
      "(c as Value).grad = 3.0; "
      "let c_backward = (c as Value).backward; "
      "call(c_backward, c); "
      "print((a as Value).grad); "
      "print((b as Value).grad); "
      "free_array(as_array((c as Value).prev, ptr)); free(as_heap(c)); "
      "free_array(as_array((a as Value).prev, ptr)); free(as_heap(a)); "
      "free_array(as_array((b as Value).prev, ptr)); free(as_heap(b))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_DOUBLE_EQ(std::atof(buf), 4.0) << "a.grad after backward should be 4, got " << buf;
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_DOUBLE_EQ(std::atof(buf), 5.0) << "b.grad after backward should be 5, got " << buf;
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesTwoLibsCosAndPointSet) {
  /* First lib: libm (cos). Second lib: fusion_phase6.so (point_set, point_x). */
  const char* so_path = "./fusion_phase6.so";
  std::string src = "struct Point { x: f64; y: f64; }; extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; extern lib \"";
  src += so_path;
  src += "\"; extern fn point_set(p: Point, x: f64, y: f64) -> void; extern fn point_x(p: Point) -> f64; let p = heap(Point); point_set(p, 1.0, 2.0); print(cos(0.0)); print(point_x(p)); free(as_heap(p))";
  auto tokens = fusion::lex(src);
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed: " << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesInterleavedLetAndExpr) {
  /* Execution order must follow source order: print(1), then let x=2, print(x), let y=3, print(y). */
  auto tokens = fusion::lex("print(1); let x = 2; print(x); let y = 3; print(y)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesIfWithComparison) {
  /* If/elif/else with comparisons: compile and run JIT. Verifies codegen for conditionals does not crash. */
  auto tokens = fusion::lex(
    "fn sign(x: i64) -> i64 { if (x > 0) { return 1; } elif (x < 0) { return 99; } else { return 0; } } "
    "print(sign(5)); print(sign(0)); print(sign(3))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  EXPECT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesTopLevelIf) {
  /* Top-level if/else: compile and run JIT, capture stdout (fd 1) and expect "1" and "2". */
  auto tokens = fusion::lex("if (1 > 0) { print(1); } else { print(0); } print(2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  std::string stdout_capture;
  {
    int pipe_fd[2];
    ASSERT_EQ(pipe(pipe_fd), 0);
    int saved_stdout = dup(STDOUT_FILENO);
    ASSERT_GE(saved_stdout, 0);
    dup2(pipe_fd[1], STDOUT_FILENO);
    close(pipe_fd[1]);
    auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    ASSERT_TRUE(jit_result.ok) << jit_result.error;
    char buf[256];
    ssize_t n;
    while ((n = read(pipe_fd[0], buf, sizeof(buf) - 1)) > 0) {
      buf[n] = '\0';
      stdout_capture += buf;
    }
    close(pipe_fd[0]);
  }
  EXPECT_TRUE(stdout_capture.find("1") != std::string::npos) << "captured: " << stdout_capture;
  EXPECT_TRUE(stdout_capture.find("2") != std::string::npos) << "captured: " << stdout_capture;
}

TEST(JitTests, ExecutesAllocArrayAndIndex) {
  /* heap_array(i64, n), store via a[i]=v, load via a[i] and print */
  const char* path = "/tmp/fusion_jit_array_index_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let a = heap_array(i64, 3); a[0] = 10; a[1] = 20; a[2] = 30; print(a[0]); print(a[1]); print(a[2]); free_array(a)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "10\n");
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "20\n");
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "30\n");
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesHeapArrayF64Print) {
  /* heap_array(f64, n); fill with 1.5; print(x[i]) must print 1.5, not truncated to 1 */
  const char* path = "/tmp/fusion_jit_heap_array_f64_print.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let a = 1.5; let n = 3; let x = heap_array(f64, n); "
      "for (let i = 0; i < n; i = i + 1) { x[i] = a; } "
      "print(x[0]); print(x[1]); print(x[2]); print(a); print(x[0]); "
      "free_array(as_array(x, f64))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  for (int i = 0; i < 5; ++i) {
    ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr) << "line " << i;
    EXPECT_NEAR(std::atof(buf), 1.5, 0.0001) << "line " << i << " expected 1.5, got " << buf;
  }
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesForOverF64Array) {
  /* for (let i = 0; i < len(arr); i = i + 1) { print(arr[i]); } with f64 array => 1.5, 1.5, 1.5 */
  const char* path = "/tmp/fusion_jit_for_f64_arr.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let arr = heap_array(f64, 3); arr[0] = 1.5; arr[1] = 1.5; arr[2] = 1.5; "
      "for (let i = 0; i < len(arr); i = i + 1) { print(arr[i]); } "
      "free_array(as_array(arr, f64))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  for (int i = 0; i < 3; ++i) {
    ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr) << "line " << i;
    EXPECT_NEAR(std::atof(buf), 1.5, 0.0001) << "line " << i << " expected 1.5, got " << buf;
  }
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesCStyleFor) {
  /* for (let i = 0; i < 5; i = i + 1) { print(i); } print(0) => prints 0,1,2,3,4 then 0 */
  const char* path = "/tmp/fusion_jit_for_range_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("for (let i = 0; i < 5; i = i + 1) { print(i); } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  for (int expected = 0; expected < 5; ++expected) {
    ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr) << "expected line " << expected;
    EXPECT_EQ(std::atoi(buf), expected) << "line " << expected;
  }
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_EQ(std::atoi(buf), 0) << "trailing print(0)";
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesForOverArrayWithLen) {
  /* let arr = heap_array(i64, 3); arr[0]=1; ... for (let i = 0; i < len(arr); i = i + 1) { print(arr[i]); } print(0) => 1,2,3,0 */
  const char* path = "/tmp/fusion_jit_for_arr_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let arr = heap_array(i64, 3); arr[0] = 1; arr[1] = 2; arr[2] = 3; for (let i = 0; i < len(arr); i = i + 1) { print(arr[i]); } print(0); free_array(arr)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "1\n");
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "2\n");
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "3\n");
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "0\n");
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesCStyleForTwoArgs) {
  /* for (let x = 2; x < 6; x = x + 1) { print(x); } print(0) => 2,3,4,5,0 */
  const char* path = "/tmp/fusion_jit_range_two_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("for (let x = 2; x < 6; x = x + 1) { print(x); } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  for (int expected = 2; expected <= 5; ++expected) {
    ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr) << "expected " << expected;
    EXPECT_EQ(std::atoi(buf), expected) << "expected " << expected;
  }
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_EQ(std::atoi(buf), 0);
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesCStyleForF64) {
  /* for (let i = 0; i < 3; i = i + 1) { let x = i as f64; print(x); } print(0) => 0.0, 1.0, 2.0, 0 */
  const char* path = "/tmp/fusion_jit_range_f64_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("for (let i = 0; i < 3; i = i + 1) { let x = i as f64; print(x); } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  for (double expected = 0.0; expected <= 2.0; expected += 1.0) {
    ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr) << "expected " << expected;
    EXPECT_NEAR(std::atof(buf), expected, 0.0001) << "expected " << expected << " got " << buf;
  }
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_EQ(std::atoi(buf), 0);
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesStackI64) {
  const char* path = "/tmp/fusion_jit_stack_i64.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let x = stack(i64); store(x, 42); print(load(x))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 42);
}

TEST(JitTests, ExecutesStackF64) {
  const char* path = "/tmp/fusion_jit_stack_f64.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let x = stack(f64); store(x, 3.14); print(load_f64(x))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_NEAR(std::atof(buf), 3.14, 0.0001);
}

TEST(JitTests, ExecutesStackArray) {
  const char* path = "/tmp/fusion_jit_stack_array.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let a = stack_array(i64, 3); a[0] = 1; a[1] = 2; a[2] = 3; print(a[0]); print(a[1]); print(a[2])");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  for (int expected = 1; expected <= 3; ++expected) {
    ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr) << "expected " << expected;
    EXPECT_EQ(std::atoi(buf), expected);
  }
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesStackArrayWithLen) {
  const char* path = "/tmp/fusion_jit_stack_array_len.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let a = stack_array(i64, 5); print(len(a))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 5);
}

TEST(JitTests, ExecutesHeapAndFree) {
  auto tokens = fusion::lex("let p = heap(i64); store(p, 99); print(load(p)); free(p)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  const char* path = "/tmp/fusion_jit_heap_free.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 99);
}

TEST(JitTests, ExecutesHeapArrayAndFreeArray) {
  const char* path = "/tmp/fusion_jit_heap_array_free.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let a = heap_array(i64, 4); a[0] = 10; print(a[0]); free_array(a)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 10);
}

TEST(JitTests, ExecutesFreeValueStyle) {
  /* Value struct with prev ptr; alloc_value, free_value using free_array(prev) and free(v). */
  const char* path = "/tmp/fusion_jit_free_value.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct Value { data: f64; grad: f64; prev: ptr[void]; children_count: i64; backward: ptr[void]; }; "
      "fn free_value(v: ptr[void]) -> void { "
      "  let prev = (v as Value).prev; "
      "  free_array(as_array(prev, ptr)); "
      "  free(as_heap(v)); "
      "} "
      "fn alloc_value(data: f64, prev: ptr[void], children_count: i64, backward: ptr[void]) -> ptr[void] { "
      "  let value = heap(Value); "
      "  value.data = data; "
      "  value.grad = 0.0; "
      "  value.prev = prev; "
      "  value.children_count = children_count; "
      "  value.backward = backward; "
      "  return value; "
      "} "
      "fn leaf_backward(v: ptr[void]) -> void { } "
      "let a = alloc_value(1.0, heap_array(ptr[void], 0), 0, get_func_ptr(leaf_backward)); "
      "(a as Value).grad = 7.0; "
      "print((a as Value).grad); "
      "free_value(a)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_NEAR(std::atof(buf), 7.0, 0.0001);
}

TEST(JitTests, ExecutesAsHeapForParam) {
  auto tokens = fusion::lex(
      "fn f(p: ptr[void]) -> void { free(as_heap(p)); } "
      "let x = heap(i64); f(x)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesAsArrayForLoadField) {
  auto tokens = fusion::lex(
      "struct V { prev: ptr[void]; }; "
      "fn free_v(v: ptr[void]) -> void { "
      "  free_array(as_array((v as V).prev, ptr)); "
      "  free(as_heap(v)); "
      "} "
      "let p = heap_array(ptr[void], 0); "
      "let v = heap(V); "
      "v.prev = p; "
      "free_v(v)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesHeapStruct) {
  const char* path = "/tmp/fusion_jit_heap_struct.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct P { x: f64; y: f64; }; "
      "let p = heap(P); p.x = 1.0; p.y = 2.0; print(p.x); free(as_heap(p))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_NEAR(std::atof(buf), 1.0, 0.0001);
}

TEST(JitTests, ExecutesStackStruct) {
  const char* path = "/tmp/fusion_jit_stack_struct.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct P { x: f64; y: f64; }; "
      "let p = stack(P); p.x = 1.0; p.y = 2.0; print(p.x); print(p.y)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_NEAR(std::atof(buf), 1.0, 0.0001);
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_NEAR(std::atof(buf), 2.0, 0.0001);
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesLenOnStackArray) {
  const char* path = "/tmp/fusion_jit_len_stack.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let a = stack_array(i64, 7); print(len(a))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 7);
}

TEST(JitTests, ExecutesLenOnHeapArray) {
  const char* path = "/tmp/fusion_jit_len_heap.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let a = heap_array(i64, 11); print(len(a)); free_array(a)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 11);
}

TEST(JitTests, FreeArrayF64Direct) {
  // heap_array(f64, 4); fill; free_array(as_array(a, f64)) must not leak
  auto tokens = fusion::lex(
      "let a = heap_array(f64, 4); "
      "a[0] = 1.0; a[1] = 2.0; a[2] = 3.0; a[3] = 4.0; "
      "free_array(as_array(a, f64))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, FreeMultipleF64ArraysTrainedWeightPattern) {
  // Simulates train.fusion's _trained arrays: W1(2x4=8), W2(4x1=4), b1(4), b2(1),
  // X(2), h1(4), h2(1) — all allocated and freed via as_array(_, f64).
  auto tokens = fusion::lex(
      "let W1 = heap_array(f64, 8); "
      "let W2 = heap_array(f64, 4); "
      "let b1 = heap_array(f64, 4); "
      "let b2 = heap_array(f64, 1); "
      "let X  = heap_array(f64, 2); "
      "let h1 = heap_array(f64, 4); "
      "let h2 = heap_array(f64, 1); "
      "W1[0] = 1.0; W2[0] = 2.0; b1[0] = 3.0; b2[0] = 4.0; "
      "X[0] = 5.0;  h1[0] = 6.0; h2[0] = 7.0; "
      "free_array(as_array(W1, f64)); "
      "free_array(as_array(W2, f64)); "
      "free_array(as_array(b1, f64)); "
      "free_array(as_array(b2, f64)); "
      "free_array(as_array(X,  f64)); "
      "free_array(as_array(h1, f64)); "
      "free_array(as_array(h2, f64))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}

TEST(JitTests, ExecutesEqNeq) {
  const char* path = "/tmp/fusion_jit_eqneq.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "if (2 == 2) { print(1); } else { print(0); } "
      "if (3 != 3) { print(1); } else { print(0); }");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "1\n") << "2 == 2 should be true";
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_STREQ(buf, "0\n") << "3 != 3 should be false";
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesLeGe) {
  const char* path = "/tmp/fusion_jit_lege.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "for (let i = 0; i <= 3; i = i + 1) { print(i); }");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  for (int expected = 0; expected <= 3; ++expected) {
    ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr) << "expected line for i=" << expected;
    EXPECT_EQ(std::atoi(buf), expected) << "loop body should print " << expected;
  }
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesGeDecrement) {
  const char* path = "/tmp/fusion_jit_ge_decr.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "for (let i = 3; i >= 0; i = i - 1) { print(i); }");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  for (int expected = 3; expected >= 0; --expected) {
    ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr) << "expected line for i=" << expected;
    EXPECT_EQ(std::atoi(buf), expected) << "decrement loop should print " << expected;
  }
  fclose(cap);
  unlink(path);
}

TEST(JitTests, ExecutesPrintString) {
  const char* path = "/tmp/fusion_jit_print_str.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("print(\"hello\")");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_STREQ(buf, "hello\n") << "print(\"hello\") should output hello";
}

TEST(JitTests, ExecutesToStrI64) {
  const char* path = "/tmp/fusion_jit_to_str_i64.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("print(to_str(42))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_STREQ(buf, "42\n") << "to_str(42) should produce \"42\"";
}

TEST(JitTests, ExecutesToStrF64) {
  const char* path = "/tmp/fusion_jit_to_str_f64.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("print(to_str(3.14))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_NEAR(std::atof(buf), 3.14, 0.001) << "to_str(3.14) should convert to string near 3.14";
}

TEST(JitTests, ExecutesStringConcat) {
  const char* path = "/tmp/fusion_jit_str_concat.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let s = \"a\" + \"b\"; print(s); free(as_heap(s))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_STREQ(buf, "ab\n") << "string + string should concatenate";
}

TEST(JitTests, ExecutesToStrConcat) {
  /* to_str uses a single static buffer; left must be copied before right is evaluated. */
  const char* path = "/tmp/fusion_jit_to_str_concat.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let s = to_str(100) + to_str(2); print(s); free(as_heap(s))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_STREQ(buf, "1002\n") << "to_str(100) + to_str(2) should be 1002 not 22";
}

TEST(JitTests, ExecutesFromStrI64) {
  const char* path = "/tmp/fusion_jit_from_str_i64.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let s = to_str(123); let n = from_str(s, i64); print(n)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 123) << "from_str(to_str(123), i64) should return 123";
}

TEST(JitTests, ExecutesRecursiveFib) {
  const char* path = "/tmp/fusion_jit_fib.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "fn fib(n: i64) -> i64 { "
      "  if (n <= 1) { return n; } "
      "  return fib(n - 1) + fib(n - 2); "
      "} "
      "print(fib(10))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 55) << "fib(10) should be 55";
}

TEST(JitTests, ExecutesLoadPtr) {
  const char* path = "/tmp/fusion_jit_load_ptr.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let p = heap(ptr[void]); "
      "let q = heap(i64); "
      "store(p, q); "
      "let r = load_ptr(p); "
      "store(r, 77); "
      "print(load(q)); "
      "free(as_heap(p)); free(as_heap(q))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 77) << "load_ptr + store roundtrip should yield 77";
}

TEST(JitTests, ExecutesLoadI32) {
  /* heap(i64) gives 8 bytes; store(p, 300) writes 300 as i64; load_i32(p) reads low 4 bytes as i32 */
  const char* path = "/tmp/fusion_jit_load_i32.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let p = heap(i64); store(p, 300); print(load_i32(p)); free(as_heap(p))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 300) << "load_i32 on slot storing 300 should yield 300";
}

TEST(JitTests, ExecutesAddrOf) {
  const char* path = "/tmp/fusion_jit_addr_of.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let x = 5; "
      "let p = addr_of(x); "
      "store(p, 77); "
      "print(load(p))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 77) << "store through addr_of should update local variable";
}

TEST(JitTests, ExecutesCastF64ToI64) {
  const char* path = "/tmp/fusion_jit_cast_f64_i64.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let x = 3.9; let y = x as i64; print(y)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 3) << "3.9 as i64 should truncate to 3";
}

TEST(JitTests, ExecutesCastI64ToI32) {
  /* i64 -> i32 truncation: 300 fits in i32, then extend back to i64 for print */
  const char* path = "/tmp/fusion_jit_cast_i64_i32.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("let x = 300; let y = x as i32; print(y as i64)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 300) << "300 as i32 as i64 should round-trip to 300";
}

TEST(JitTests, ExecutesStructI64Field) {
  const char* path = "/tmp/fusion_jit_struct_i64.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct N { n: i64; }; "
      "let obj = heap(N); "
      "obj.n = 42; "
      "print(obj.n); "
      "free(as_heap(obj))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 42) << "obj.n = 42 / print(obj.n) for i64 field should yield 42";
}

TEST(JitTests, ExecutesPrintTwoArgs) {
  /* print(val, stream=2) writes to stderr; just verify jit_result.ok (no crash) */
  auto tokens = fusion::lex("print(42, 2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  EXPECT_TRUE(jit_result.ok) << "print(42, 2) to stderr should not crash: " << jit_result.error;
}

TEST(JitTests, ExecutesWriteFile) {
  const char* tmp = "/tmp/fusion_jit_write_file_test.txt";
  unlink(tmp);
  auto tokens = fusion::lex(
      std::string("let fh = open(\"") + tmp + "\", \"w\"); "
      "write_file(fh, 99); "
      "close(fh)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* f = fopen(tmp, "r");
  ASSERT_NE(f, nullptr) << "temp file should exist after write_file";
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), f), nullptr);
  fclose(f);
  unlink(tmp);
  EXPECT_EQ(std::atoi(buf), 99) << "write_file(fh, 99) should write 99 to file";
}

TEST(JitTests, ExecutesFromStrF64) {
  /* from_str(to_str(3.14), f64) should recover 3.14; cast to i64 = 3 for deterministic print */
  const char* path = "/tmp/fusion_jit_from_str_f64.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let s = to_str(3.14); let n = from_str(s, f64); let i = n as i64; print(i)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  EXPECT_EQ(std::atoi(buf), 3) << "from_str(to_str(3.14), f64) truncated to i64 should be 3";
}

TEST(JitTests, ExecutesReadLineFile) {
  const char* tmp = "/tmp/fusion_jit_read_line_in.txt";
  /* Write a known line from C */
  { FILE* f = fopen(tmp, "w"); ASSERT_NE(f, nullptr); fprintf(f, "hello_line\n"); fclose(f); }
  const char* out_path = "/tmp/fusion_jit_read_line_out.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(out_path, "w", stdout));
  auto tokens = fusion::lex(
      std::string("let f = open(\"") + tmp + "\", \"r\"); "
      "let line = read_line_file(f); "
      "print(line); "
      "close(f)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  unlink(tmp);
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(out_path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(out_path);
  /* read_line_file strips trailing newline; print re-adds it */
  EXPECT_STREQ(buf, "hello_line\n") << "read_line_file + print should output the line";
}

TEST(JitTests, ExecutesHttpRequestGet) {
  /* Requires libcurl and network. GET example.com, check status 200 and non-null body. */
  const char* out_path = "/tmp/fusion_jit_http_out.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(out_path, "w", stdout));
  auto tokens = fusion::lex(
      "let body = http_request(\"GET\", \"https://example.com\", \"\"); "
      "let code = http_status(); "
      "print(code); "
      "print(\"ok\");");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  /* best-effort restore of stdout FILE*; fd already restored by dup2 */
  if (!freopen("/dev/fd/1", "w", stdout)) { (void)0; }
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(out_path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  if (strcmp(buf, "200\n") != 0) {
    fclose(cap);
    unlink(out_path);
    GTEST_SKIP() << "http_request to example.com failed (no network or non-200); skipping";
  }
  EXPECT_STREQ(buf, "200\n") << "http_status() should be 200 for example.com";
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_TRUE(std::string(buf).find("ok") != std::string::npos) << "expected ok line";
  fclose(cap);
  unlink(out_path);
}

TEST(JitTests, ExecutesLineCountFile) {
  const char* tmp = "/tmp/fusion_jit_line_count_in.txt";
  /* Write 3 lines from C */
  { FILE* f = fopen(tmp, "w"); ASSERT_NE(f, nullptr); fprintf(f, "a\nb\nc\n"); fclose(f); }
  const char* out_path = "/tmp/fusion_jit_line_count_out.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(out_path, "w", stdout));
  auto tokens = fusion::lex(
      std::string("let f = open(\"") + tmp + "\", \"r\"); "
      "let n = line_count_file(f); "
      "close(f); "
      "print(n)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  unlink(tmp);
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(out_path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(out_path);
  EXPECT_EQ(std::atoi(buf), 3) << "line_count_file should count 3 newlines in 3-line file";
}

TEST(JitTests, ExecutesWriteBytesReadBytes) {
  /* Write 8 bytes (i64 = 54321) to a binary file, read back, and verify via print */
  const char* tmp = "/tmp/fusion_jit_wb_rb.bin";
  unlink(tmp);
  const char* out_path = "/tmp/fusion_jit_wb_rb_out.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(out_path, "w", stdout));
  auto tokens = fusion::lex(
      std::string(
      "let buf = heap_array(i64, 1); "
      "buf[0] = 54321; "
      "let fh = open(\"") + tmp + "\", \"w\"); "
      "write_bytes(fh, buf, 8); "
      "close(fh); "
      "let buf2 = heap_array(i64, 1); "
      "let fh2 = open(\"" + tmp + "\", \"r\"); "
      "read_bytes(fh2, buf2, 8); "
      "close(fh2); "
      "print(buf2[0]); "
      "free_array(as_array(buf, i64)); "
      "free_array(as_array(buf2, i64))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  unlink(tmp);
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(out_path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(out_path);
  EXPECT_EQ(std::atoi(buf), 54321) << "write_bytes/read_bytes roundtrip should recover 54321";
}

TEST(JitTests, TypedPtrArrayFieldRead) {
  /* heap_array(ptr[S], n); store ptr into arr[0]; read arr[0].x without a cast */
  const char* path = "/tmp/fusion_jit_typed_ptr_array_field_read.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct S { x: i64; y: f64; }; "
      "fn make_s(v: i64) -> ptr[void] { "
      "  let s = heap(S); "
      "  s.x = v; "
      "  s.y = 0.0; "
      "  return s; "
      "} "
      "let arr = heap_array(ptr[S], 2); "
      "arr[0] = make_s(42); "
      "arr[1] = make_s(99); "
      "let val0 = arr[0].x; "
      "let val1 = arr[1].x; "
      "print(val0); "
      "print(val1); "
      "free(as_heap(arr[0])); "
      "free(as_heap(arr[1])); "
      "free_array(as_array(arr, ptr))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char line[32];
  ASSERT_NE(fgets(line, sizeof(line), cap), nullptr);
  EXPECT_EQ(std::atoi(line), 42) << "arr[0].x should be 42";
  ASSERT_NE(fgets(line, sizeof(line), cap), nullptr);
  EXPECT_EQ(std::atoi(line), 99) << "arr[1].x should be 99";
  fclose(cap);
  unlink(path);
}

TEST(JitTests, TypedPtrParamFieldAccess) {
  /* fn get_data(v: ptr[Value]) -> f64 { return v.data; } — ptr[T] param enables field access */
  const char* path = "/tmp/fusion_jit_typed_ptr_param_field.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct Value { data: f64; }; "
      "fn get_data(v: ptr[Value]) -> f64 { return v.data; } "
      "let val = heap(Value); "
      "val.data = 3.14; "
      "print(get_data(val)); "
      "free(as_heap(val))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[64];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  fclose(cap);
  unlink(path);
  double result = std::atof(buf);
  EXPECT_NEAR(result, 3.14, 0.001) << "get_data should return v.data = 3.14";
}

TEST(JitTests, CastIndexIntoBarePtrArrayThenFieldAccess) {
  /* Mirrors the micrograd train.fusion pattern:
   *   let arr = heap_array(ptr, n);   // bare ptr array
   *   let data = (arr[0] as Value).data;
   * Previously failed sema with "cannot determine struct type of base expression" */
  const char* path = "/tmp/fusion_jit_cast_index_field.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct Value { data: f64; grad: f64; }; "
      "let arr = heap_array(ptr[void], 2); "
      "let v0 = heap(Value); "
      "v0.data = 7.5; "
      "v0.grad = 0.0; "
      "let v1 = heap(Value); "
      "v1.data = 2.5; "
      "v1.grad = 0.0; "
      "arr[0] = v0; "
      "arr[1] = v1; "
      "let d0 = (arr[0] as Value).data; "
      "let d1 = (arr[1] as Value).data; "
      "print(d0); "
      "print(d1); "
      "free(as_heap(v0)); "
      "free(as_heap(v1)); "
      "free_array(as_array(arr, ptr))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char line[32];
  ASSERT_NE(fgets(line, sizeof(line), cap), nullptr);
  EXPECT_NEAR(std::atof(line), 7.5, 0.001) << "(arr[0] as Value).data should be 7.5";
  ASSERT_NE(fgets(line, sizeof(line), cap), nullptr);
  EXPECT_NEAR(std::atof(line), 2.5, 0.001) << "(arr[1] as Value).data should be 2.5";
  fclose(cap);
  unlink(path);
}

TEST(JitTests, LetBindingFromCastFieldAccess) {
  /* let w = arr[i] as Value; w.data and w.grad are accessible */
  const char* path = "/tmp/fusion_jit_let_cast_field.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct Value { data: f64; grad: f64; }; "
      "let arr = heap_array(ptr[void], 1); "
      "let v = heap(Value); "
      "v.data = 3.0; "
      "v.grad = 9.0; "
      "arr[0] = v; "
      "let w = arr[0] as Value; "
      "let sum = w.data + w.grad; "
      "print(sum); "
      "free(as_heap(v)); "
      "free_array(as_array(arr, ptr))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  fflush(stdout);
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
  ASSERT_TRUE(freopen("/dev/fd/1", "w", stdout));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
  FILE* cap = fopen(path, "r");
  ASSERT_NE(cap, nullptr);
  char buf[32];
  ASSERT_NE(fgets(buf, sizeof(buf), cap), nullptr);
  EXPECT_NEAR(std::atof(buf), 12.0, 0.001) << "w.data + w.grad should be 12.0";
  fclose(cap);
  unlink(path);
}
#endif
