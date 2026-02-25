#include "ast.hpp"
#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>

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
  ASSERT_FALSE(result.program->stmts.empty());
  fusion::Expr* root = result.program->stmts.back().get();
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
  EXPECT_EQ(result.program->bindings.size(), 1u);
  EXPECT_EQ(result.program->bindings[0].name, "x");
  ASSERT_FALSE(result.program->stmts.empty());
  EXPECT_EQ(result.program->stmts.back()->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(result.program->stmts.back()->var_name, "x");
}

TEST(ParserTests, ParsesLetTwoBindingsSum) {
  auto tokens = fusion::lex("let a = 1; let b = 2; a + b");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  EXPECT_EQ(result.program->bindings.size(), 2u);
  EXPECT_EQ(result.program->bindings[0].name, "a");
  EXPECT_EQ(result.program->bindings[1].name, "b");
  ASSERT_FALSE(result.program->stmts.empty());
  EXPECT_EQ(result.program->stmts.back()->kind, fusion::Expr::Kind::BinaryOp);
  ASSERT_TRUE(result.program->stmts.back()->left);
  EXPECT_EQ(result.program->stmts.back()->left->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(result.program->stmts.back()->left->var_name, "a");
  ASSERT_TRUE(result.program->stmts.back()->right);
  EXPECT_EQ(result.program->stmts.back()->right->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(result.program->stmts.back()->right->var_name, "b");
}

TEST(ParserTests, RejectsLetNoTrailingExpr) {
  auto tokens = fusion::lex("let x = 1");
  auto result = fusion::parse(tokens);
  EXPECT_FALSE(result.ok());
}

TEST(ParserTests, ParsesExternCos) {
  auto tokens = fusion::lex("extern lib \"libm.so.6\"; extern fn cos(x: f64) -> f64; print(cos(0.0))");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  EXPECT_EQ(result.program->libs.size(), 1u);
  EXPECT_EQ(result.program->libs[0].path, "libm.so.6");
  EXPECT_EQ(result.program->extern_fns.size(), 1u);
  EXPECT_EQ(result.program->extern_fns[0].name, "cos");
  EXPECT_EQ(result.program->extern_fns[0].params.size(), 1u);
  EXPECT_EQ(result.program->extern_fns[0].params[0].first, "x");
  EXPECT_EQ(result.program->extern_fns[0].params[0].second, fusion::FfiType::F64);
  EXPECT_EQ(result.program->extern_fns[0].return_type, fusion::FfiType::F64);
  ASSERT_FALSE(result.program->stmts.empty());
  EXPECT_EQ(result.program->stmts.back()->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(result.program->stmts.back()->callee, "print");
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
#endif
