#include "ast.hpp"
#include "codegen.hpp"
#include "layout.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <unistd.h>

#ifdef FUSION_HAVE_LLVM
#include <llvm/IR/LLVMContext.h>
#endif

// --- LexerTests ---
TEST(LexerTests, TokenizesPrintOnePlusTwo) {
  auto tokens = fusion::lex("print(1+2)");
  ASSERT_GE(tokens.size(), 7u);
  EXPECT_EQ(tokens[0].kind, fusion::TokenKind::Ident);
  EXPECT_EQ(tokens[0].ident, "print");
  EXPECT_EQ(tokens[1].kind, fusion::TokenKind::LParen);
  EXPECT_EQ(tokens[2].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[2].int_value, 1);
  EXPECT_EQ(tokens[3].kind, fusion::TokenKind::Plus);
  EXPECT_EQ(tokens[4].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[4].int_value, 2);
  EXPECT_EQ(tokens[5].kind, fusion::TokenKind::RParen);
}

TEST(LexerTests, TokenizesSpaces) {
  auto tokens = fusion::lex("1 + 2");
  ASSERT_GE(tokens.size(), 4u);
  EXPECT_EQ(tokens[0].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[0].int_value, 1);
  EXPECT_EQ(tokens[1].kind, fusion::TokenKind::Plus);
  EXPECT_EQ(tokens[2].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[2].int_value, 2);
}

// --- ParserTests ---
TEST(ParserTests, ParsesPrintOnePlusTwo) {
  auto tokens = fusion::lex("print(1+2)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 1u);
  fusion::Expr* root = result.root_expr();
  ASSERT_TRUE(root);
  EXPECT_EQ(root->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(root->callee, "print");
  EXPECT_EQ(root->args.size(), 1u);
  auto* arg = root->args[0].get();
  ASSERT_TRUE(arg);
  EXPECT_EQ(arg->kind, fusion::Expr::Kind::BinaryOp);
  EXPECT_EQ(arg->bin_op, fusion::BinOp::Add);
  ASSERT_TRUE(arg->left);
  EXPECT_EQ(arg->left->kind, fusion::Expr::Kind::IntLiteral);
  EXPECT_EQ(arg->left->int_value, 1);
  ASSERT_TRUE(arg->right);
  EXPECT_EQ(arg->right->kind, fusion::Expr::Kind::IntLiteral);
  EXPECT_EQ(arg->right->int_value, 2);
}

TEST(ParserTests, RejectsInvalidPrint) {
  auto tokens = fusion::lex("print(1+");
  auto result = fusion::parse(tokens);
  EXPECT_FALSE(result.ok());
}

TEST(ParserTests, ParsesLetOneBindingVarRef) {
  auto tokens = fusion::lex("let x = 1; x");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<fusion::LetBinding>(result.program->top_level[0]));
  EXPECT_EQ(std::get<fusion::LetBinding>(result.program->top_level[0]).name, "x");
  ASSERT_TRUE(std::holds_alternative<fusion::ExprPtr>(result.program->top_level[1]));
  EXPECT_EQ(std::get<fusion::ExprPtr>(result.program->top_level[1])->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(std::get<fusion::ExprPtr>(result.program->top_level[1])->var_name, "x");
}

TEST(ParserTests, ParsesLetTwoBindingsSum) {
  auto tokens = fusion::lex("let a = 1; let b = 2; a + b");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 3u);
  ASSERT_TRUE(std::holds_alternative<fusion::LetBinding>(result.program->top_level[0]));
  EXPECT_EQ(std::get<fusion::LetBinding>(result.program->top_level[0]).name, "a");
  ASSERT_TRUE(std::holds_alternative<fusion::LetBinding>(result.program->top_level[1]));
  EXPECT_EQ(std::get<fusion::LetBinding>(result.program->top_level[1]).name, "b");
  ASSERT_TRUE(std::holds_alternative<fusion::ExprPtr>(result.program->top_level[2]));
  fusion::Expr* root = std::get<fusion::ExprPtr>(result.program->top_level[2]).get();
  EXPECT_EQ(root->kind, fusion::Expr::Kind::BinaryOp);
  ASSERT_TRUE(root->left);
  EXPECT_EQ(root->left->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(root->left->var_name, "a");
  ASSERT_TRUE(root->right);
  EXPECT_EQ(root->right->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(root->right->var_name, "b");
}

TEST(ParserTests, RejectsLetNoTrailingExpr) {
  auto tokens = fusion::lex("let x = 1");
  auto result = fusion::parse(tokens);
  EXPECT_FALSE(result.ok());
}

TEST(ParserTests, ParsesInterleavedLetAndExpr) {
  auto tokens = fusion::lex("let a = 1; print(a); let b = 2; print(b)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 4u);
  ASSERT_TRUE(std::holds_alternative<fusion::LetBinding>(result.program->top_level[0]));
  EXPECT_EQ(std::get<fusion::LetBinding>(result.program->top_level[0]).name, "a");
  ASSERT_TRUE(std::holds_alternative<fusion::ExprPtr>(result.program->top_level[1]));
  EXPECT_EQ(std::get<fusion::ExprPtr>(result.program->top_level[1])->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(std::get<fusion::ExprPtr>(result.program->top_level[1])->callee, "print");
  ASSERT_TRUE(std::holds_alternative<fusion::LetBinding>(result.program->top_level[2]));
  EXPECT_EQ(std::get<fusion::LetBinding>(result.program->top_level[2]).name, "b");
  ASSERT_TRUE(std::holds_alternative<fusion::ExprPtr>(result.program->top_level[3]));
  EXPECT_EQ(std::get<fusion::ExprPtr>(result.program->top_level[3])->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(std::get<fusion::ExprPtr>(result.program->top_level[3])->callee, "print");
}

TEST(ParserTests, ParsesExternCos) {
  auto tokens = fusion::lex("extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; print(cos(0.0))");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  EXPECT_EQ(result.program->libs.size(), 1u);
  EXPECT_EQ(result.program->libs[0].path, "libm.so.6");
  EXPECT_EQ(result.program->libs[0].name, "__lib0");
  EXPECT_EQ(result.program->extern_fns.size(), 1u);
  EXPECT_EQ(result.program->extern_fns[0].lib_name, "__lib0");
  EXPECT_EQ(result.program->extern_fns[0].params[0].second, fusion::FfiType::F64);
  EXPECT_EQ(result.program->extern_fns[0].return_type, fusion::FfiType::F64);
  ASSERT_EQ(result.program->top_level.size(), 1u);
  EXPECT_EQ(std::get<fusion::ExprPtr>(result.program->top_level[0])->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(std::get<fusion::ExprPtr>(result.program->top_level[0])->callee, "print");
}

TEST(ParserTests, ParsesTwoLibsTwoExternFnsBoundToCorrectLib) {
  auto tokens = fusion::lex(
    "extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; "
    "extern lib \"other.so\"; extern fn bar() -> void; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  EXPECT_EQ(result.program->libs.size(), 2u);
  EXPECT_EQ(result.program->libs[0].path, "libm.so.6");
  EXPECT_EQ(result.program->libs[0].name, "__lib0");
  EXPECT_EQ(result.program->libs[1].path, "other.so");
  EXPECT_EQ(result.program->libs[1].name, "__lib1");
  EXPECT_EQ(result.program->extern_fns.size(), 2u);
  EXPECT_EQ(result.program->extern_fns[0].name, "cos");
  EXPECT_EQ(result.program->extern_fns[0].lib_name, "__lib0");
  EXPECT_EQ(result.program->extern_fns[1].name, "bar");
  EXPECT_EQ(result.program->extern_fns[1].lib_name, "__lib1");
}

TEST(ParserTests, ParsesExternLibBlock) {
  auto tokens = fusion::lex(
    "extern lib \"x.so\" { fn foo() -> void; fn bar() -> i64; }; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  EXPECT_EQ(result.program->libs.size(), 1u);
  EXPECT_EQ(result.program->libs[0].path, "x.so");
  EXPECT_EQ(result.program->extern_fns.size(), 2u);
  EXPECT_EQ(result.program->extern_fns[0].name, "foo");
  EXPECT_EQ(result.program->extern_fns[0].return_type, fusion::FfiType::Void);
  EXPECT_EQ(result.program->extern_fns[0].lib_name, result.program->libs[0].name);
  EXPECT_EQ(result.program->extern_fns[1].name, "bar");
  EXPECT_EQ(result.program->extern_fns[1].return_type, fusion::FfiType::I64);
  EXPECT_EQ(result.program->extern_fns[1].lib_name, result.program->libs[0].name);
}

TEST(ParserTests, ParsesOpaqueAndExternFn) {
  auto tokens = fusion::lex("opaque cudaStream_t; extern lib \"x.so\"; extern fn foo(s: cudaStream_t) -> void; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  EXPECT_EQ(result.program->opaque_types.size(), 1u);
  EXPECT_EQ(result.program->opaque_types[0], "cudaStream_t");
  EXPECT_EQ(result.program->extern_fns.size(), 1u);
  EXPECT_EQ(result.program->extern_fns[0].params.size(), 1u);
  EXPECT_EQ(result.program->extern_fns[0].params[0].second, fusion::FfiType::Ptr);
  EXPECT_EQ(result.program->extern_fns[0].return_type, fusion::FfiType::Void);
}

TEST(ParserTests, ParsesStructPoint) {
  auto tokens = fusion::lex("struct Point { x: f64; y: f64; }; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  EXPECT_EQ(result.program->struct_defs.size(), 1u);
  EXPECT_EQ(result.program->struct_defs[0].name, "Point");
  EXPECT_EQ(result.program->struct_defs[0].fields.size(), 2u);
  EXPECT_EQ(result.program->struct_defs[0].fields[0].first, "x");
  EXPECT_EQ(result.program->struct_defs[0].fields[0].second, fusion::FfiType::F64);
  EXPECT_EQ(result.program->struct_defs[0].fields[1].first, "y");
  EXPECT_EQ(result.program->struct_defs[0].fields[1].second, fusion::FfiType::F64);
}

TEST(ParserTests, ParsesIfWithComparison) {
  auto tokens = fusion::lex("fn foo(x: i64) -> i64 { if (x > 0) { return 1; } return 0; } print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->user_fns.size(), 1u);
  const fusion::FnDef& fn = result.program->user_fns[0];
  EXPECT_EQ(fn.name, "foo");
  ASSERT_GE(fn.body.size(), 2u);
  ASSERT_EQ(fn.body[0]->kind, fusion::Stmt::Kind::If);
  EXPECT_TRUE(fn.body[0]->cond);
  EXPECT_EQ(fn.body[0]->cond->kind, fusion::Expr::Kind::Compare);
  EXPECT_EQ(fn.body[0]->cond->compare_op, fusion::CompareOp::Gt);
  ASSERT_EQ(fn.body[0]->then_body.size(), 1u);
  EXPECT_EQ(fn.body[0]->then_body[0]->kind, fusion::Stmt::Kind::Return);
}

TEST(ParserTests, ParsesIfElifElse) {
  auto tokens = fusion::lex(
    "fn foo(x: i64) -> i64 { if (x > 0) { return 1; } elif (x < 0) { return 99; } else { return 0; } } print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->user_fns.size(), 1u);
  const fusion::FnDef& fn = result.program->user_fns[0];
  ASSERT_EQ(fn.body.size(), 1u);
  ASSERT_EQ(fn.body[0]->kind, fusion::Stmt::Kind::If);
  EXPECT_EQ(fn.body[0]->cond->compare_op, fusion::CompareOp::Gt);
  ASSERT_EQ(fn.body[0]->else_body.size(), 1u);
  EXPECT_EQ(fn.body[0]->else_body[0]->kind, fusion::Stmt::Kind::If);
  EXPECT_EQ(fn.body[0]->else_body[0]->cond->compare_op, fusion::CompareOp::Lt);
  ASSERT_EQ(fn.body[0]->else_body[0]->else_body.size(), 1u);
  EXPECT_EQ(fn.body[0]->else_body[0]->else_body[0]->kind, fusion::Stmt::Kind::Return);
}

TEST(ParserTests, ParsesComparisonOperators) {
  auto tokens = fusion::lex("fn f(a: i64, b: i64) -> i64 { if (a == b) { return 0; } if (a != b) { return 1; } if (a <= b) { return 2; } if (a >= b) { return 3; } return 4; } print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->user_fns.size(), 1u);
  const fusion::FnDef& fn = result.program->user_fns[0];
  ASSERT_GE(fn.body.size(), 4u);
  EXPECT_EQ(fn.body[0]->cond->compare_op, fusion::CompareOp::Eq);
  EXPECT_EQ(fn.body[1]->cond->compare_op, fusion::CompareOp::Ne);
  EXPECT_EQ(fn.body[2]->cond->compare_op, fusion::CompareOp::Le);
  EXPECT_EQ(fn.body[3]->cond->compare_op, fusion::CompareOp::Ge);
}

TEST(ParserTests, ParsesTopLevelIf) {
  auto tokens = fusion::lex("if (1 > 0) { print(1); } else { print(0); } print(2)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 2u);
  const fusion::StmtPtr* if_stmt = std::get_if<fusion::StmtPtr>(&result.program->top_level[0]);
  ASSERT_NE(if_stmt, nullptr);
  ASSERT_TRUE(*if_stmt);
  EXPECT_EQ((*if_stmt)->kind, fusion::Stmt::Kind::If);
  EXPECT_TRUE((*if_stmt)->cond);
  EXPECT_EQ((*if_stmt)->cond->kind, fusion::Expr::Kind::Compare);
  EXPECT_EQ((*if_stmt)->cond->compare_op, fusion::CompareOp::Gt);
  ASSERT_EQ((*if_stmt)->then_body.size(), 1u);
  ASSERT_EQ((*if_stmt)->else_body.size(), 1u);
  fusion::Expr* root = result.root_expr();
  ASSERT_TRUE(root);
  EXPECT_EQ(root->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(root->callee, "print");
  ASSERT_EQ(root->args.size(), 1u);
  ASSERT_TRUE(root->args[0]);
  EXPECT_EQ(root->args[0]->kind, fusion::Expr::Kind::IntLiteral);
  EXPECT_EQ(root->args[0]->int_value, 2);
}

// --- LayoutTests ---
TEST(LayoutTests, PointSizeAlignmentOffsets) {
  fusion::StructDef point;
  point.name = "Point";
  point.fields = {{"x", fusion::FfiType::F64}, {"y", fusion::FfiType::F64}};
  fusion::StructLayout layout = fusion::compute_layout(point);
  EXPECT_EQ(layout.size, 16u);
  EXPECT_EQ(layout.alignment, 8u);
  ASSERT_EQ(layout.fields.size(), 2u);
  EXPECT_EQ(layout.fields[0].first, "x");
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
  EXPECT_EQ(layout.fields[0].second.type, fusion::FfiType::F64);
  EXPECT_EQ(layout.fields[1].first, "y");
  EXPECT_EQ(layout.fields[1].second.offset, 8u);
  EXPECT_EQ(layout.fields[1].second.type, fusion::FfiType::F64);
}

// --- SemaTests ---
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
  auto tokens = fusion::lex("print(1,2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
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

#ifdef FUSION_HAVE_LLVM
// --- CodegenTests ---
TEST(CodegenTests, EmitsModuleWithFusionMain) {
  auto tokens = fusion::lex("print(1+2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  EXPECT_NE(module->getFunction("fusion_main"), nullptr);
  EXPECT_NE(module->getFunction("rt_print_i64"), nullptr);
}

// --- JitTests ---
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
  src += "\"; extern fn set_int_out(out: ptr, v: i64) -> void; let p = alloc(i64); store(p, 0); set_int_out(p, 42); print(load(p))";
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
  src += "\"; extern fn point_set(p: Point, x: f64, y: f64) -> void; extern fn point_x(p: Point) -> f64; let p = alloc(Point); point_set(p, 1.0, 2.0); print(point_x(p))";
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
  src += "\"; extern fn point_set(p: Point, x: f64, y: f64) -> void; let p = alloc(Point); point_set(p, 3.0, 4.0); print(load_field(p, Point, x)); print(load_field(p, Point, y))";
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

TEST(JitTests, ExecutesTwoLibsCosAndPointSet) {
  /* First lib: libm (cos). Second lib: fusion_phase6.so (point_set, point_x). */
  const char* so_path = "./fusion_phase6.so";
  std::string src = "struct Point { x: f64; y: f64; }; extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; extern lib \"";
  src += so_path;
  src += "\"; extern fn point_set(p: Point, x: f64, y: f64) -> void; extern fn point_x(p: Point) -> f64; let p = alloc(Point); point_set(p, 1.0, 2.0); print(cos(0.0)); print(point_x(p))";
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
#endif
