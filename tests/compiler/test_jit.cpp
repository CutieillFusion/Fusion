#include "ast.hpp"
#include "codegen.hpp"
#include "layout.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
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
  src += "\"; extern fn set_int_out(out: ptr, v: i64) -> void; let p = heap(i64); store(p, 0); set_int_out(p, 42); print(load(p)); free(as_heap(p))";
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
  src += "\"; extern fn point_set(p: Point, x: f64, y: f64) -> void; let p = heap(Point); point_set(p, 3.0, 4.0); print(load_field(p, Point, x)); print(load_field(p, Point, y)); free(as_heap(p))";
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
  /* structs.fusion-style: store get_func_ptr(add/mul) in Operation.func, call via load_field. */
  const char* path = "/tmp/fusion_jit_indirect_call.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct Operation { func: ptr; x: f64; y: f64; }; "
      "fn add(x: f64, y: f64) -> f64 { return x + y; } "
      "fn mul(x: f64, y: f64) -> f64 { return x * y; } "
      "fn perform_operation(op: Operation) -> f64 { "
      "  let func = load_field(op, Operation, func); "
      "  let x = load_field(op, Operation, x); "
      "  let y = load_field(op, Operation, y); "
      "  return call(func, x, y); "
      "} "
      "let op_add = heap(Operation); "
      "store_field(op_add, Operation, func, get_func_ptr(add)); "
      "store_field(op_add, Operation, x, 3.0); "
      "store_field(op_add, Operation, y, 4.0); "
      "let op_mul = heap(Operation); "
      "store_field(op_mul, Operation, func, get_func_ptr(mul)); "
      "store_field(op_mul, Operation, x, 3.0); "
      "store_field(op_mul, Operation, y, 4.0); "
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
     Requires alloc_array to be heap-allocated; with stack allocation the pointer would be dangling. */
  const char* path = "/tmp/fusion_jit_array_escape_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "struct Value { data: f64; grad: f64; prev: ptr; children_count: i64; backward: ptr; }; "
      "fn alloc_value(data: f64, prev: ptr, children_count: i64, backward: ptr) -> ptr { "
      "  let value = heap(Value); "
      "  store_field(value, Value, data, data); "
      "  store_field(value, Value, grad, 0.0); "
      "  store_field(value, Value, prev, prev); "
      "  store_field(value, Value, children_count, children_count); "
      "  store_field(value, Value, backward, backward); "
      "  return value; "
      "} "
      "fn leaf_backward(v: ptr) -> void { } "
      "fn add_backward(out: ptr) -> void { "
      "  let prev = load_field(out, Value, prev); "
      "  let a = prev[0] as ptr; "
      "  let b = prev[1] as ptr; "
      "  let grad = load_field(out, Value, grad); "
      "  let a_grad = load_field(a, Value, grad); "
      "  let b_grad = load_field(b, Value, grad); "
      "  store_field(a, Value, grad, a_grad + grad); "
      "  store_field(b, Value, grad, b_grad + grad); "
      "} "
      "fn add_forward(a: ptr, b: ptr) -> ptr { "
      "  let data = load_field(a, Value, data) + load_field(b, Value, data); "
      "  let prev = heap_array(ptr, 2); "
      "  prev[0] = a; "
      "  prev[1] = b; "
      "  return alloc_value(data, prev, 2, get_func_ptr(add_backward)); "
      "} "
      "let a = alloc_value(1.0, heap_array(ptr, 0), 0, get_func_ptr(leaf_backward)); "
      "let b = alloc_value(2.0, heap_array(ptr, 0), 0, get_func_ptr(leaf_backward)); "
      "store_field(a, Value, grad, 1.0); "
      "store_field(b, Value, grad, 2.0); "
      "let c = add_forward(a, b); "
      "store_field(c, Value, grad, 3.0); "
      "let c_backward = load_field(c, Value, backward); "
      "call(c_backward, c); "
      "print(load_field(a, Value, grad)); "
      "print(load_field(b, Value, grad)); "
      "free_array(as_array(load_field(c, Value, prev), ptr)); free(as_heap(c)); "
      "free_array(as_array(load_field(a, Value, prev), ptr)); free(as_heap(a)); "
      "free_array(as_array(load_field(b, Value, prev), ptr)); free(as_heap(b))");
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
  /* alloc_array(i64, n), store via a[i]=v, load via a[i] and print */
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
  /* let arr = alloc_array(i64, 3); arr[0]=1; ... for (let i = 0; i < len(arr); i = i + 1) { print(arr[i]); } print(0) => 1,2,3,0 */
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
      "struct Value { data: f64; grad: f64; prev: ptr; children_count: i64; backward: ptr; }; "
      "fn free_value(v: ptr) -> void { "
      "  let prev = load_field(v, Value, prev); "
      "  free_array(as_array(prev, ptr)); "
      "  free(as_heap(v)); "
      "} "
      "fn alloc_value(data: f64, prev: ptr, children_count: i64, backward: ptr) -> ptr { "
      "  let value = heap(Value); "
      "  store_field(value, Value, data, data); "
      "  store_field(value, Value, grad, 0.0); "
      "  store_field(value, Value, prev, prev); "
      "  store_field(value, Value, children_count, children_count); "
      "  store_field(value, Value, backward, backward); "
      "  return value; "
      "} "
      "fn leaf_backward(v: ptr) -> void { } "
      "let a = alloc_value(1.0, heap_array(ptr, 0), 0, get_func_ptr(leaf_backward)); "
      "store_field(a, Value, grad, 7.0); "
      "print(load_field(a, Value, grad)); "
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
      "fn f(p: ptr) -> void { free(as_heap(p)); } "
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
      "struct V { prev: ptr; }; "
      "fn free_v(v: ptr) -> void { "
      "  free_array(as_array(load_field(v, V, prev), ptr)); "
      "  free(as_heap(v)); "
      "} "
      "let p = heap_array(ptr, 0); "
      "let v = heap(V); "
      "store_field(v, V, prev, p); "
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
      "let p = heap(P); store_field(p, P, x, 1.0); store_field(p, P, y, 2.0); print(load_field(p, P, x)); free(as_heap(p))");
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
      "let p = stack(P); store_field(p, P, x, 1.0); store_field(p, P, y, 2.0); print(load_field(p, P, x)); print(load_field(p, P, y))");
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
#endif
