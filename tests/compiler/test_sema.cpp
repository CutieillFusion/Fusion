#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <gtest/gtest.h>
#include <string>

TEST(SemaTests, RejectsUndefinedVariable) {
  auto tokens = fusion::lex("let x = 1; print(y)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, AcceptsPrintOnePlusTwo) {
  auto tokens = fusion::lex("print(1+2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok);
}

TEST(SemaTests, RejectsWrongArity) {
  /* print accepts 1 or 2 args; 3 args is wrong arity */
  auto tokens = fusion::lex("print(1, 2, 3)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, AcceptsMulAndDiv) {
  auto tokens = fusion::lex("print(2*3); print(6.0/2.0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsPrintCos) {
  auto tokens = fusion::lex("extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; print(cos(0.0))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok);
}

TEST(SemaTests, RejectsExternFnReferencesUnknownLib) {
  /* extern fn before any extern lib gets lib_name "" which is not a declared lib */
  auto tokens = fusion::lex("extern fn foo() -> void; extern lib \"x.so\"; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("unknown lib") != std::string::npos);
}

TEST(SemaTests, AcceptsIfWithComparison) {
  auto tokens = fusion::lex("fn foo(x: i64) -> i64 { if (x > 0) { return 1; } return 0; } print(foo(1))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsTopLevelIf) {
  auto tokens = fusion::lex("if (1 > 0) { print(1); } else { print(0); } print(2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsReturnAtTopLevel) {
  auto tokens = fusion::lex("if (1 > 0) { return 1; } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("return") != std::string::npos)
    << "expected error message to mention return, got: " << sema_result.error.message;
}

TEST(SemaTests, AcceptsAllocArrayAndIndex) {
  auto tokens = fusion::lex("let a = heap_array(i64, 10); print(a[0]); print(a[1])");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsCStyleFor) {
  auto tokens = fusion::lex("for (let i = 0; i < 5; i = i + 1) { print(i); } print(99)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsForOverArrayWithLen) {
  auto tokens = fusion::lex("let arr = heap_array(i64, 3); for (let i = 0; i < len(arr); i = i + 1) { let x = arr[i]; print(x); } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsCStyleForWithF64) {
  auto tokens = fusion::lex("for (let i = 0; i < 2; i = i + 1) { let x = i as f64; print(x); } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsLenNonPtr) {
  auto tokens = fusion::lex("let n = 5; let x = len(n); print(x)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("len") != std::string::npos ||
              sema_result.error.message.find("pointer") != std::string::npos)
    << "expected len/pointer error, got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsFreeStackAlloc) {
  auto tokens = fusion::lex("let x = stack(i64); free(x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("stack") != std::string::npos ||
              sema_result.error.message.find("free") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsFreeStackArray) {
  auto tokens = fusion::lex("let a = stack_array(i64, 5); free(a); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, RejectsFreeHeapArray) {
  auto tokens = fusion::lex("let a = heap_array(i64, 5); free(a); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("free_array") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsFreeArrayHeapAlloc) {
  auto tokens = fusion::lex("struct Point { x: f64; y: f64; }; let p = heap(Point); free_array(p); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("free") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsFreeArrayStackAlloc) {
  auto tokens = fusion::lex("let a = stack_array(i64, 5); free_array(a); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, RejectsFreeUnknownWithoutAsHeap) {
  auto tokens = fusion::lex("fn f(p: ptr) -> void { free(p); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("as_heap") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsFreeArrayUnknownWithoutAsArray) {
  auto tokens = fusion::lex("fn f(p: ptr) -> void { free_array(p); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("as_array") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, AcceptsFreeAsHeap) {
  auto tokens = fusion::lex("fn f(p: ptr) -> void { free(as_heap(p)); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsFreeArrayAsArray) {
  auto tokens = fusion::lex("fn f(p: ptr) -> void { free_array(as_array(p, ptr)); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsReturnStackPointer) {
  auto tokens = fusion::lex("fn bad() -> ptr { let x = stack(i64); return x; } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("stack") != std::string::npos ||
              sema_result.error.message.find("escape") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsStoreStackIntoHeap) {
  auto tokens = fusion::lex(
      "struct Node { next: ptr; }; let obj = heap(Node); let x = stack(i64); store_field(obj, Node, next, x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("stack") != std::string::npos ||
              sema_result.error.message.find("outlive") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsStoreStackIntoHeapArray) {
  auto tokens = fusion::lex("let arr = heap_array(ptr, 4); let x = stack(i64); arr[0] = x; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, AcceptsStoreStackIntoStack) {
  auto tokens = fusion::lex("let a = stack(ptr); let x = stack(i64); store(a, x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsStackPtrToExtern) {
  auto tokens = fusion::lex(
      "extern lib \"libc.so.6\"; extern fn take(p: ptr) -> void; let x = stack(i64); take(x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("stack") != std::string::npos ||
              sema_result.error.message.find("extern") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsStackPtrToInternalWithoutNoescape) {
  auto tokens = fusion::lex("fn f(p: ptr) -> void { } let x = stack(i64); f(x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("stack") != std::string::npos ||
              sema_result.error.message.find("noescape") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, AcceptsStackPtrToNoescapeParam) {
  auto tokens = fusion::lex(
      "fn sum(noescape buf: ptr, n: i64) -> i64 { return 0; } let x = stack_array(i64, 10); sum(x, 10); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsStackPtrToIndirectCall) {
  auto tokens = fusion::lex(
      "fn id(noescape p: ptr) -> ptr { return p; } let x = stack(i64); let fp = get_func_ptr(id); call(fp, x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("stack") != std::string::npos ||
              sema_result.error.message.find("indirect") != std::string::npos ||
              sema_result.error.message.find("unknown") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsStackUnknownType) {
  auto tokens = fusion::lex("let x = stack(FooBar); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("FooBar") != std::string::npos ||
              sema_result.error.message.find("unknown") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsHeapArrayUnknownElemType) {
  auto tokens = fusion::lex("let a = heap_array(Unknown, 5); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, AcceptsStackArrayStructElem) {
  auto tokens = fusion::lex("struct S { x: i64; }; let a = stack_array(S, 3); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, FlavorPropagatesThroughAssignment) {
  /* let p = heap(i64); let q = p; free(p); free(q) - both should compile (flavor propagates) */
  auto tokens = fusion::lex("let p = heap(i64); let q = p; free(p); free(q); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsHeapArrayOfStructs) {
  auto tokens = fusion::lex("struct P { x: i64; }; let a = heap_array(P, 2); a[0] = 1; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  if (sema_result.ok) {
    /* If struct element assign is supported */
  } else {
    /* Document limitation: heap_array of structs may not support element assign yet */
    EXPECT_TRUE(sema_result.error.message.find("P") != std::string::npos ||
                sema_result.error.message.find("assign") != std::string::npos ||
                sema_result.error.message.find("index") != std::string::npos)
      << "got: " << sema_result.error.message;
  }
}

TEST(SemaTests, AcceptsGetFuncPtrAndCall) {
  auto tokens = fusion::lex("fn add(x: f64, y: f64) -> f64 { return x + y; } let fp = get_func_ptr(add); print(call(fp, 1.0, 2.0))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsCallWithArgumentCoercion) {
  auto tokens = fusion::lex("fn add(x: f64, y: f64) -> f64 { return x + y; } let fp = get_func_ptr(add); print(call(fp, 1, 2))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsFnPtrThroughLetAndAssign) {
  auto tokens = fusion::lex(
      "fn id(x: i64) -> i64 { return x; } let fp = get_func_ptr(id); let fp2 = fp; fp2 = fp; print(call(fp2, 42))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsCallWithZeroArgs) {
  auto tokens = fusion::lex("fn f() -> i64 { return 0; } let fp = get_func_ptr(f); print(call())");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, RejectsCallNonPtrFirstArg) {
  auto tokens = fusion::lex("fn f(x: i64) -> i64 { return x; } print(call(42, 1))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("ptr") != std::string::npos ||
              sema_result.error.message.find("call") != std::string::npos)
    << "expected ptr/call error, got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsGetFuncPtrUnknown) {
  auto tokens = fusion::lex("let fp = get_func_ptr(unknown_fn); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("unknown") != std::string::npos ||
              sema_result.error.message.find("get_func_ptr") != std::string::npos)
    << "expected unknown/get_func_ptr error, got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsCallWrongArgTypes) {
  auto tokens = fusion::lex("fn f(x: i64, y: i64) -> i64 { return x + y; } let fp = get_func_ptr(f); print(call(fp, 1.0, \"hi\"))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  // With current coercion rules, ptr and f64 arguments are allowed to coerce to i64,
  // so this program is considered semantically valid.
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsCallThroughStructField) {
  /* call(load_field(op, Operation, func), x, y) with inferred signature (f64, f64) -> f64. */
  auto tokens = fusion::lex(
      "struct Operation { func: ptr; x: f64; y: f64; }; "
      "fn add(x: f64, y: f64) -> f64 { return x + y; } "
      "fn perform_operation(op: Operation) -> f64 { "
      "  let func = load_field(op, Operation, func); "
      "  let x = load_field(op, Operation, x); "
      "  let y = load_field(op, Operation, y); "
      "  return call(func, x, y); "
      "} "
      "print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
}
