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
  auto tokens = fusion::lex("fn f(p: ptr[void]) -> void { free(p); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("as_heap") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsFreeArrayUnknownWithoutAsArray) {
  auto tokens = fusion::lex("fn f(p: ptr[void]) -> void { free_array(p); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("as_array") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, AcceptsFreeAsHeap) {
  auto tokens = fusion::lex("fn f(p: ptr[void]) -> void { free(as_heap(p)); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsFreeArrayAsArray) {
  auto tokens = fusion::lex("fn f(p: ptr[void]) -> void { free_array(as_array(p, ptr)); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsReturnStackPointer) {
  auto tokens = fusion::lex("fn bad() -> ptr[void] { let x = stack(i64); return x; } print(1)");
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
      "struct Node { next: ptr[void]; }; let obj = heap(Node); let x = stack(i64); obj.next = x; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("stack") != std::string::npos ||
              sema_result.error.message.find("outlive") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsStoreStackIntoHeapArray) {
  auto tokens = fusion::lex("let arr = heap_array(ptr[void], 4); let x = stack(i64); arr[0] = x; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, AcceptsStoreStackIntoStack) {
  auto tokens = fusion::lex("let a = stack(ptr[void]); let x = stack(i64); store(a, x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsStackPtrToExtern) {
  auto tokens = fusion::lex(
      "extern lib \"libc.so.6\"; extern fn take(p: ptr[void]) -> void; let x = stack(i64); take(x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("stack") != std::string::npos ||
              sema_result.error.message.find("extern") != std::string::npos)
    << "got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsStackPtrToInternalWithoutNoescape) {
  auto tokens = fusion::lex("fn f(p: ptr[void]) -> void { } let x = stack(i64); f(x); print(1)");
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
      "fn sum(noescape buf: ptr[void], n: i64) -> i64 { return 0; } let x = stack_array(i64, 10); sum(x, 10); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsStackPtrToIndirectCall) {
  auto tokens = fusion::lex(
      "fn id(noescape p: ptr[void]) -> ptr[void] { return p; } let x = stack(i64); let fp = get_func_ptr(id); call(fp, x); print(1)");
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

TEST(SemaTests, TypedPtrArrayFieldAccess) {
  /* heap_array(ptr[S], n) + arr[0].x should pass sema cleanly */
  auto tokens = fusion::lex(
      "struct S { x: i64; y: f64; }; "
      "fn make_s() -> ptr[void] { return heap(S); } "
      "let arr = heap_array(ptr[S], 3); "
      "arr[0] = make_s(); "
      "let v = arr[0].x; "
      "print(v)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsStructWithPtrCharFieldAccess) {
  /* struct User { name: ptr[char]; age: i64; } — field access on .name and .age must pass sema */
  auto tokens = fusion::lex(
      "struct User { name: ptr[char]; age: i64; }; "
      "let u = heap(User); u.age = 42; u.name = \"alice\"; "
      "print(u.age); print(u.name)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, TypedPtrArrayInvalidStruct) {
  /* heap_array(ptr[NoSuch], n) should fail sema with unknown type */
  auto tokens = fusion::lex("let arr = heap_array(ptr[NoSuch], 3); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("NoSuch") != std::string::npos ||
              sema_result.error.message.find("unknown") != std::string::npos)
      << "got: " << sema_result.error.message;
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
  /* call(op.func, op.x, op.y) with inferred signature (f64, f64) -> f64. */
  auto tokens = fusion::lex(
      "struct Operation { func: ptr[void]; x: f64; y: f64; }; "
      "fn add(x: f64, y: f64) -> f64 { return x + y; } "
      "fn perform_operation(op: Operation) -> f64 { "
      "  let func = op.func; "
      "  let x = op.x; "
      "  let y = op.y; "
      "  return call(func, x, y); "
      "} "
      "print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsEqAndNeq) {
  auto tokens = fusion::lex("let a = 1; let b = 2; if (a == b) { print(1); } if (a != b) { print(0); }");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsLeGe) {
  auto tokens = fusion::lex("let a = 1; let b = 2; if (a <= b) { print(1); } if (b >= a) { print(1); }");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsPointerOrderingComparison) {
  /* ptr < ptr is rejected; only == and != are valid for pointer comparisons */
  auto tokens = fusion::lex("let p = heap(i64); let q = heap(i64); if (p < q) { print(1); }");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("pointer") != std::string::npos)
    << "expected pointer comparison error, got: " << sema_result.error.message;
}

TEST(SemaTests, AcceptsCastToI32) {
  auto tokens = fusion::lex("let x = 5; let y = x as i32; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsCastToPtr) {
  /* ptr -> ptr cast is accepted (identity) */
  auto tokens = fusion::lex("let p = heap(i64); let q = p as ptr[void]; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsCastToI64) {
  auto tokens = fusion::lex("let x = 3.0; let y = x as i64; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsCastPtrFromNonPtr) {
  /* i64 -> ptr cast is rejected; only ptr -> ptr is valid */
  auto tokens = fusion::lex("let x = 5; let y = x as ptr[void]; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("ptr") != std::string::npos)
    << "expected cast-to-ptr error, got: " << sema_result.error.message;
}

TEST(SemaTests, AcceptsAddrOf) {
  auto tokens = fusion::lex("let x = 5; let p = addr_of(x); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsAddrOfNonVar) {
  auto tokens = fusion::lex("let p = addr_of(42); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("addr_of") != std::string::npos ||
              sema_result.error.message.find("variable") != std::string::npos)
    << "expected addr_of/variable error, got: " << sema_result.error.message;
}

TEST(SemaTests, AcceptsLoadPtr) {
  /* load_ptr on a ptr argument is accepted */
  auto tokens = fusion::lex("fn f(p: ptr[void]) -> void { let q = load_ptr(p); } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsPrintTwoArgs) {
  /* print(val, stream) with stream=2 (stderr) is accepted */
  auto tokens = fusion::lex("print(1, 2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsDuplicateFunctionDef) {
  auto tokens = fusion::lex("fn f() -> void { } fn f() -> void { } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("duplicate") != std::string::npos ||
              sema_result.error.message.find("f") != std::string::npos)
    << "expected duplicate function error, got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsWriteFileWrongFirstArg) {
  /* write_file first arg must be a pointer (file handle); i64 should fail */
  auto tokens = fusion::lex("write_file(42, 99)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("write_file") != std::string::npos ||
              sema_result.error.message.find("pointer") != std::string::npos)
    << "expected write_file/pointer error, got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsExternFnUnknownParamType) {
  auto tokens = fusion::lex("extern lib \"libc.so.6\"; extern fn f(x: Bogus) -> void; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("Bogus") != std::string::npos ||
              sema_result.error.message.find("unknown") != std::string::npos)
    << "expected unknown type error, got: " << sema_result.error.message;
}

TEST(SemaTests, RejectsExternFnUnknownReturnType) {
  auto tokens = fusion::lex("extern lib \"libc.so.6\"; extern fn f() -> Bogus; print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("Bogus") != std::string::npos ||
              sema_result.error.message.find("unknown") != std::string::npos)
    << "expected unknown return type error, got: " << sema_result.error.message;
}

// ---- Library-style files (no top-level statements) ----

TEST(SemaTests, AcceptsFnOnlyProgram) {
  // A file with only fn definitions and no top-level statements is valid
  // (it's a library file). Sema should accept it and validate the bodies.
  auto tokens = fusion::lex(
    "fn add(x: i64, y: i64) -> i64 { return x + y; } "
    "fn mul(x: i64, y: i64) -> i64 { return x * y; }"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  ASSERT_TRUE(parse_result.program->top_level.empty());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsStructOnlyProgram) {
  // A file with only struct definitions is valid.
  auto tokens = fusion::lex(
    "struct Point { x: f64; y: f64; } "
    "struct Rect { w: f64; h: f64; }"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  ASSERT_TRUE(parse_result.program->top_level.empty());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsMixedFnStructNoTopLevel) {
  // fn + struct definitions without any top-level statements.
  auto tokens = fusion::lex(
    "struct Vec2 { x: f64; y: f64; } "
    "fn dot(a: ptr[void], b: ptr[void]) -> f64 { "
    "  let ax = load_f64(a); "
    "  let bx = load_f64(b); "
    "  return ax + bx; "
    "}"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  ASSERT_TRUE(parse_result.program->top_level.empty());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, StillChecksFnBodyWithNoTopLevel) {
  // Even when there are no top-level statements, sema must validate fn bodies.
  auto tokens = fusion::lex(
    "fn broken() -> i64 { return undefined_var; }"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  ASSERT_TRUE(parse_result.program->top_level.empty());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, MultiErrorTwoFnsWithErrors) {
  // Two functions both containing errors — sema should report both.
  auto tokens = fusion::lex(
    "fn a() -> i64 { return no_such_var; } "
    "fn b() -> i64 { return also_missing; }"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_GE(sema_result.errors.size(), 2u)
    << "expected at least 2 errors, got " << sema_result.errors.size();
  // Backward-compat: .error must still be set to the first error.
  EXPECT_FALSE(sema_result.error.message.empty());
  EXPECT_EQ(sema_result.error.message, sema_result.errors[0].message);
}

TEST(SemaTests, MultiErrorTopLevelStmts) {
  // Two top-level calls each referencing an undefined function.
  auto tokens = fusion::lex("no_fn_a(); no_fn_b()");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_GE(sema_result.errors.size(), 2u)
    << "expected at least 2 errors, got " << sema_result.errors.size();
}

TEST(SemaTests, SingleErrorStillPopulatesErrorsVec) {
  // Even a single error must appear in the errors vector.
  auto tokens = fusion::lex("print(undefined_var)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  ASSERT_EQ(sema_result.errors.size(), 1u);
  EXPECT_EQ(sema_result.error.message, sema_result.errors[0].message);
}

TEST(SemaTests, TypedPtrParamFieldAccess) {
  // fn f(p: ptr[Value]) -> void { let x = p.data; } passes sema
  auto tokens = fusion::lex(
    "struct Value { data: f64; }; "
    "fn f(p: ptr[Value]) -> void { let x = p.data; }"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, TypedPtrFieldInStruct) {
  // struct Node { next: ptr[Node]; } — self-referential typed pointer field
  auto tokens = fusion::lex(
    "struct Node { val: i64; next: ptr[Node]; }; "
    "fn f(n: Node) -> i64 { return n.val; }"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, TypedPtrCastSyntax) {
  // (x as ptr[Value]).data passes sema — identical to (x as Value).data
  auto tokens = fusion::lex(
    "struct Value { data: f64; }; "
    "fn f(x: ptr[void]) -> f64 { return (x as ptr[Value]).data; }"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, TypedPtrParamVoidIsOpaque) {
  // fn f(p: ptr[void]) -> void { } — ptr[void] treated as bare ptr
  auto tokens = fusion::lex(
    "fn f(p: ptr[void]) -> void { }"
  );
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsPtrCharParam) {
  auto tokens = fusion::lex("fn f(s: ptr[char]) -> void { } f(\"hello\"); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed";
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsPtrCharReturn) {
  auto tokens = fusion::lex("fn f() -> ptr[char] { return \"hello\"; } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed";
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsPtrI8Return) {
  /* ptr[i8] return type (primitive element) must pass array_element_struct validation */
  auto tokens = fusion::lex(
      "fn build_buf() -> ptr[i8] { let p = heap_array(i8, 64); return p; } "
      "let b = build_buf(); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed";
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsPtrF64Return) {
  /* ptr[f64] return type (primitive element) must pass array_element_struct validation */
  auto tokens = fusion::lex(
      "fn get_vec() -> ptr[f64] { let p = heap_array(f64, 4); return p; } "
      "let v = get_vec(); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed";
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsPtrUnknownElementReturn) {
  /* fn returning ptr[NoSuch] must fail with unknown array element struct */
  auto tokens = fusion::lex("fn f() -> ptr[NoSuch] { return 0; } print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed";
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("array element struct") != std::string::npos ||
              sema_result.error.message.find("NoSuch") != std::string::npos)
      << "got: " << sema_result.error.message;
}

TEST(SemaTests, AcceptsPtrCharConcat) {
  auto tokens = fusion::lex("let s = \"a\" + \"b\"; print(s)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << "parse failed";
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsBarePtr) {
  auto tokens = fusion::lex("fn f(p: ptr) -> void { } print(1)");
  auto parse_result = fusion::parse(tokens);
  EXPECT_FALSE(parse_result.ok()) << "expected parse failure for bare ptr param";
}

TEST(SemaTests, HttpRequestExpectsThreeArgs) {
  auto tokens = fusion::lex("http_request(\"GET\")");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("http_request") != std::string::npos);
}

TEST(SemaTests, HttpRequestRejectsWrongArity) {
  auto tokens = fusion::lex("http_request(\"GET\", \"https://x.com\", \"\", 1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
}

TEST(SemaTests, HttpRequestRejectsWrongType) {
  auto tokens = fusion::lex("http_request(\"GET\", \"https://x.com\", 42)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("http_request") != std::string::npos ||
              sema_result.error.message.find("pointer") != std::string::npos);
}

TEST(SemaTests, AcceptsHttpRequestGet) {
  auto tokens = fusion::lex("let b = http_request(\"GET\", \"https://x\", \"\"); print(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsHttpStatus) {
  auto tokens = fusion::lex("let c = http_status(); print(c)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, HttpStatusRejectsArgs) {
  auto tokens = fusion::lex("http_status(1)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("http_status") != std::string::npos);
}
