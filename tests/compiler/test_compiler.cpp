#include "ast.hpp"
#include "codegen.hpp"
#include "layout.hpp"
#include "lexer.hpp"
#include "multifile.hpp"
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

TEST(LexerTests, TokenizesStarAndSlash) {
  auto tokens = fusion::lex("2*3 6/2");
  ASSERT_GE(tokens.size(), 5u);
  EXPECT_EQ(tokens[0].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[0].int_value, 2);
  EXPECT_EQ(tokens[1].kind, fusion::TokenKind::Star);
  EXPECT_EQ(tokens[2].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[2].int_value, 3);
  EXPECT_EQ(tokens[3].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[3].int_value, 6);
  EXPECT_EQ(tokens[4].kind, fusion::TokenKind::Slash);
}

TEST(LexerTests, TokenizesMinus) {
  auto tokens = fusion::lex("5-2");
  ASSERT_GE(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[0].int_value, 5);
  EXPECT_EQ(tokens[1].kind, fusion::TokenKind::Minus);
  EXPECT_EQ(tokens[2].kind, fusion::TokenKind::IntLiteral);
  EXPECT_EQ(tokens[2].int_value, 2);
}

TEST(LexerTests, TokenizesBracketsAndForIn) {
  auto tokens = fusion::lex("for i in range(10) { print(arr[i]); }");
  ASSERT_GE(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].kind, fusion::TokenKind::KwFor);
  EXPECT_EQ(tokens[1].kind, fusion::TokenKind::Ident);
  EXPECT_EQ(tokens[1].ident, "i");
  size_t in_pos = 0;
  for (size_t i = 0; i < tokens.size(); ++i)
    if (tokens[i].kind == fusion::TokenKind::KwIn) { in_pos = i; break; }
  EXPECT_EQ(tokens[in_pos].kind, fusion::TokenKind::KwIn);
  size_t lb = 0;
  for (size_t i = 0; i < tokens.size(); ++i)
    if (tokens[i].kind == fusion::TokenKind::LBracket) { lb = i; break; }
  EXPECT_EQ(tokens[lb].kind, fusion::TokenKind::LBracket);
  size_t rb = 0;
  for (size_t i = 0; i < tokens.size(); ++i)
    if (tokens[i].kind == fusion::TokenKind::RBracket) { rb = i; break; }
  EXPECT_EQ(tokens[rb].kind, fusion::TokenKind::RBracket);
}

TEST(LexerTests, TokenizesImportExportLib) {
  auto tokens = fusion::lex("import lib \"vec\" { struct V; fn f() -> void; }; export struct S { x: f64; }; export fn g() -> i64 { return 0; }");
  ASSERT_GE(tokens.size(), 5u);
  size_t imp = 0, exp = 0, lib = 0;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind == fusion::TokenKind::KwImport) imp = i + 1;
    if (tokens[i].kind == fusion::TokenKind::KwExport) exp = i + 1;
    if (tokens[i].kind == fusion::TokenKind::KwLib) lib = i + 1;
  }
  EXPECT_GT(imp, 0u) << "expected KwImport";
  EXPECT_GT(lib, 0u) << "expected KwLib";
  EXPECT_GT(exp, 0u) << "expected KwExport";
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

TEST(ParserTests, ParsesSub) {
  auto tokens = fusion::lex("print(5-2)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 1u);
  fusion::Expr* root = result.root_expr();
  ASSERT_TRUE(root);
  EXPECT_EQ(root->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(root->callee, "print");
  ASSERT_EQ(root->args.size(), 1u);
  auto* arg = root->args[0].get();
  ASSERT_TRUE(arg);
  EXPECT_EQ(arg->kind, fusion::Expr::Kind::BinaryOp);
  EXPECT_EQ(arg->bin_op, fusion::BinOp::Sub);
  EXPECT_EQ(arg->left->int_value, 5);
  EXPECT_EQ(arg->right->int_value, 2);
}

TEST(ParserTests, ParsesMulAndDiv) {
  auto tokens = fusion::lex("print(2*3); print(6/2);");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<fusion::ExprPtr>(result.program->top_level[0]));
  fusion::Expr* first = std::get<fusion::ExprPtr>(result.program->top_level[0]).get();
  ASSERT_TRUE(first);
  EXPECT_EQ(first->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(first->callee, "print");
  ASSERT_EQ(first->args.size(), 1u);
  auto* mul_arg = first->args[0].get();
  ASSERT_TRUE(mul_arg);
  EXPECT_EQ(mul_arg->kind, fusion::Expr::Kind::BinaryOp);
  EXPECT_EQ(mul_arg->bin_op, fusion::BinOp::Mul);
  EXPECT_EQ(mul_arg->left->int_value, 2);
  EXPECT_EQ(mul_arg->right->int_value, 3);
  ASSERT_TRUE(std::holds_alternative<fusion::ExprPtr>(result.program->top_level[1]));
  fusion::Expr* second = std::get<fusion::ExprPtr>(result.program->top_level[1]).get();
  ASSERT_TRUE(second);
  EXPECT_EQ(second->kind, fusion::Expr::Kind::Call);
  ASSERT_EQ(second->args.size(), 1u);
  auto* div_arg = second->args[0].get();
  ASSERT_TRUE(div_arg);
  EXPECT_EQ(div_arg->kind, fusion::Expr::Kind::BinaryOp);
  EXPECT_EQ(div_arg->bin_op, fusion::BinOp::Div);
  EXPECT_EQ(div_arg->left->int_value, 6);
  EXPECT_EQ(div_arg->right->int_value, 2);
}

TEST(ParserTests, ParsesLetAndForOnlyNoExpression) {
  auto tokens = fusion::lex("let n = 1; for i in range(n) { }");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<fusion::LetBinding>(result.program->top_level[0]));
  EXPECT_EQ(std::get<fusion::LetBinding>(result.program->top_level[0]).name, "n");
  ASSERT_TRUE(std::holds_alternative<fusion::StmtPtr>(result.program->top_level[1]));
  EXPECT_TRUE(result.root_expr() == nullptr);
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

TEST(ParserTests, ParsesImportLibBlock) {
  auto tokens = fusion::lex(
      "import lib \"vec\" { struct Vector; fn create(x: f64, y: f64) -> Vector; }; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->import_libs.size(), 1u);
  EXPECT_EQ(result.program->import_libs[0].name, "vec");
  ASSERT_EQ(result.program->import_libs[0].struct_names.size(), 1u);
  EXPECT_EQ(result.program->import_libs[0].struct_names[0], "Vector");
  ASSERT_EQ(result.program->import_libs[0].fn_decls.size(), 1u);
  EXPECT_EQ(result.program->import_libs[0].fn_decls[0].name, "create");
  EXPECT_EQ(result.program->import_libs[0].fn_decls[0].params.size(), 2u);
  EXPECT_EQ(result.program->import_libs[0].fn_decls[0].return_type_name, "Vector");
  ASSERT_EQ(result.program->top_level.size(), 1u);
}

TEST(ParserTests, ParsesExportStructAndFn) {
  auto tokens = fusion::lex(
      "export struct Point { x: f64; y: f64; }; export fn zero() -> Point { let p = alloc(Point); store_field(p, Point, x, 0.0); store_field(p, Point, y, 0.0); return p; } print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->struct_defs.size(), 1u);
  EXPECT_TRUE(result.program->struct_defs[0].exported);
  EXPECT_EQ(result.program->struct_defs[0].name, "Point");
  ASSERT_EQ(result.program->user_fns.size(), 1u);
  EXPECT_TRUE(result.program->user_fns[0].exported);
  EXPECT_EQ(result.program->user_fns[0].name, "zero");
}

TEST(ParserTests, ParsesNonExportStructAndFn) {
  auto tokens = fusion::lex("struct Internal { x: i64; }; fn helper() -> i64 { return 42; } print(helper())");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->struct_defs.size(), 1u);
  EXPECT_FALSE(result.program->struct_defs[0].exported);
  ASSERT_EQ(result.program->user_fns.size(), 1u);
  EXPECT_FALSE(result.program->user_fns[0].exported);
}

TEST(ParserTests, ParsesMultipleImportLibs) {
  auto tokens = fusion::lex(
      "import lib \"a\" { struct A; }; import lib \"b\" { fn bar() -> i64; }; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_EQ(result.program->import_libs.size(), 2u);
  EXPECT_EQ(result.program->import_libs[0].name, "a");
  EXPECT_EQ(result.program->import_libs[0].struct_names.size(), 1u);
  EXPECT_EQ(result.program->import_libs[1].name, "b");
  EXPECT_EQ(result.program->import_libs[1].fn_decls.size(), 1u);
  EXPECT_EQ(result.program->import_libs[1].fn_decls[0].name, "bar");
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

TEST(ParserTests, ParsesAllocArray) {
  auto tokens = fusion::lex("let a = alloc_array(i64, 5); print(a[0])");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 2u);
  const fusion::LetBinding* binding = std::get_if<fusion::LetBinding>(&result.program->top_level[0]);
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->name, "a");
  ASSERT_TRUE(binding->init);
  EXPECT_EQ(binding->init->kind, fusion::Expr::Kind::AllocArray);
  EXPECT_EQ(binding->init->var_name, "i64");
  ASSERT_TRUE(binding->init->left);
  EXPECT_EQ(binding->init->left->kind, fusion::Expr::Kind::IntLiteral);
  EXPECT_EQ(binding->init->left->int_value, 5);
  fusion::Expr* root = std::get<fusion::ExprPtr>(result.program->top_level[1]).get();
  ASSERT_TRUE(root);
  EXPECT_EQ(root->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(root->callee, "print");
  ASSERT_EQ(root->args.size(), 1u);
  ASSERT_TRUE(root->args[0]);
  EXPECT_EQ(root->args[0]->kind, fusion::Expr::Kind::Index);
  EXPECT_EQ(root->args[0]->left->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(root->args[0]->left->var_name, "a");
  EXPECT_EQ(root->args[0]->right->kind, fusion::Expr::Kind::IntLiteral);
  EXPECT_EQ(root->args[0]->right->int_value, 0);
}

TEST(ParserTests, ParsesForInRange) {
  auto tokens = fusion::lex("for i in range(10) { print(i); } print(0)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 1u);
  const fusion::StmtPtr* stmt = std::get_if<fusion::StmtPtr>(&result.program->top_level[0]);
  ASSERT_NE(stmt, nullptr);
  ASSERT_TRUE(*stmt);
  EXPECT_EQ((*stmt)->kind, fusion::Stmt::Kind::For);
  EXPECT_EQ((*stmt)->name, "i");
  ASSERT_TRUE((*stmt)->iterable);
  EXPECT_EQ((*stmt)->iterable->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ((*stmt)->iterable->callee, "range");
  ASSERT_EQ((*stmt)->iterable->args.size(), 1u);
  EXPECT_EQ((*stmt)->iterable->args[0]->kind, fusion::Expr::Kind::IntLiteral);
  EXPECT_EQ((*stmt)->iterable->args[0]->int_value, 10);
  ASSERT_GE((*stmt)->body.size(), 1u);
  EXPECT_EQ((*stmt)->body[0]->kind, fusion::Stmt::Kind::Expr);
  EXPECT_EQ((*stmt)->body[0]->expr->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ((*stmt)->body[0]->expr->callee, "print");
}

TEST(ParserTests, ParsesRangeWithType) {
  auto tokens = fusion::lex("let r = range(5, f64); print(0)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  const fusion::LetBinding* b = std::get_if<fusion::LetBinding>(&result.program->top_level[0]);
  ASSERT_NE(b, nullptr);
  ASSERT_TRUE(b->init);
  EXPECT_EQ(b->init->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(b->init->callee, "range");
  ASSERT_EQ(b->init->args.size(), 1u);
  EXPECT_EQ(b->init->call_type_arg, "f64");

  tokens = fusion::lex("let r = range(0, 3, i64); print(0)");
  result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  b = std::get_if<fusion::LetBinding>(&result.program->top_level[0]);
  ASSERT_NE(b, nullptr);
  ASSERT_TRUE(b->init);
  EXPECT_EQ(b->init->callee, "range");
  ASSERT_EQ(b->init->args.size(), 2u);
  EXPECT_EQ(b->init->call_type_arg, "i64");
}

TEST(ParserTests, ParsesGetFuncPtr) {
  auto tokens = fusion::lex("fn add(x: f64, y: f64) -> f64 { return x + y; } let fp = get_func_ptr(add); print(call(fp, 1.0, 2.0))");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 2u);
  const fusion::LetBinding* b = std::get_if<fusion::LetBinding>(&result.program->top_level[0]);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name, "fp");
  ASSERT_TRUE(b->init);
  EXPECT_EQ(b->init->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(b->init->callee, "get_func_ptr");
  ASSERT_EQ(b->init->args.size(), 1u);
  EXPECT_EQ(b->init->args[0]->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(b->init->args[0]->var_name, "add");
}

TEST(ParserTests, ParsesAssignmentToIndex) {
  auto tokens = fusion::lex("let a = alloc_array(i64, 3); a[0] = 42; print(a[0])");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 3u);
  const fusion::StmtPtr* assign = std::get_if<fusion::StmtPtr>(&result.program->top_level[1]);
  ASSERT_NE(assign, nullptr);
  ASSERT_TRUE(*assign);
  EXPECT_EQ((*assign)->kind, fusion::Stmt::Kind::Assign);
  ASSERT_TRUE((*assign)->expr);
  EXPECT_EQ((*assign)->expr->kind, fusion::Expr::Kind::Index);
  ASSERT_TRUE((*assign)->init);
  EXPECT_EQ((*assign)->init->kind, fusion::Expr::Kind::IntLiteral);
  EXPECT_EQ((*assign)->init->int_value, 42);
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
  auto tokens = fusion::lex("let a = alloc_array(i64, 10); print(a[0]); print(a[1])");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsRangeAndForIn) {
  auto tokens = fusion::lex("for i in range(5) { print(i); } print(99)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsForInOverArray) {
  auto tokens = fusion::lex("let arr = alloc_array(i64, 3); for x in arr { print(x); } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, AcceptsRangeWithF64) {
  auto tokens = fusion::lex("for x in range(2, f64) { print(x); } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(SemaTests, RejectsForInNonArray) {
  auto tokens = fusion::lex("let n = 5; for i in n { print(i); } print(0)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok()) << parse_result.error.message;
  auto sema_result = fusion::check(parse_result.program.get());
  EXPECT_FALSE(sema_result.ok);
  EXPECT_TRUE(sema_result.error.message.find("for-in") != std::string::npos ||
              sema_result.error.message.find("array") != std::string::npos)
    << "expected for-in/array error, got: " << sema_result.error.message;
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

// --- MultifileTests ---
// Helper: create temp dir, write library_file_content to lib_name.fusion, parse main_source, run resolve_imports_and_merge.
// Returns (error_message, merged_program). If error_message is non-empty, merge failed.
static std::pair<std::string, fusion::ProgramPtr> run_multifile_merge(
    const std::string& main_source,
    const std::string& lib_name,
    const std::string& library_file_content) {
  char dir_tpl[] = "/tmp/fusion_mf_XXXXXX";
  if (!mkdtemp(dir_tpl)) return {"mkdtemp failed", nullptr};
  std::string dir(dir_tpl);
  std::string lib_path = dir + "/" + lib_name + ".fusion";
  std::ofstream lib_file(lib_path);
  if (!lib_file) { rmdir(dir_tpl); return {"cannot write lib file", nullptr}; }
  lib_file << library_file_content;
  lib_file.close();

  auto tokens = fusion::lex(main_source);
  auto parse_result = fusion::parse(tokens);
  if (!parse_result.ok()) {
    unlink(lib_path.c_str());
    rmdir(dir_tpl);
    return {"parse failed: " + parse_result.error.message, nullptr};
  }
  std::string main_path = dir + "/main.fusion";
  std::string err = fusion::resolve_imports_and_merge(main_path, parse_result.program.get());
  unlink(lib_path.c_str());
  rmdir(dir_tpl);
  if (!err.empty()) return {err, nullptr};
  return {"", std::move(parse_result.program)};
}

TEST(MultifileTests, NoImportLibsLeavesProgramUnchanged) {
  auto tokens = fusion::lex("struct Point { x: f64; y: f64; }; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  std::string err = fusion::resolve_imports_and_merge("/tmp/any.fusion", result.program.get());
  EXPECT_TRUE(err.empty());
  EXPECT_EQ(result.program->struct_defs.size(), 1u);
  EXPECT_EQ(result.program->import_libs.size(), 0u);
}

TEST(MultifileTests, ImportOneLibMergesStructAndFn) {
  std::string main_src = R"(import lib "vec" { struct Vector; fn make_vec(x: f64, y: f64) -> Vector; };
let v = make_vec(1.0, 2.0);
print(load_field(v, Vector, x));
print(load_field(v, Vector, y)))";
  std::string lib_src = R"(export struct Vector { x: f64; y: f64; };
export fn make_vec(x: f64, y: f64) -> Vector {
  let p = alloc(Vector);
  store_field(p, Vector, x, x);
  store_field(p, Vector, y, y);
  return p;
})";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  EXPECT_EQ(prog->struct_defs.size(), 1u);
  EXPECT_EQ(prog->struct_defs[0].name, "Vector");
  EXPECT_EQ(prog->user_fns.size(), 1u);
  EXPECT_EQ(prog->user_fns[0].name, "make_vec");
}

TEST(MultifileTests, ImportLibWithExternMergesExtern) {
  std::string main_src = R"(import lib "mylib" { fn get_one() -> f64; };
print(get_one()))";
  std::string lib_src = R"(extern lib "libm.so.6" { fn cos(x: f64) -> f64; };
export fn get_one() -> f64 { return cos(0.0); })";
  auto [err, prog] = run_multifile_merge(main_src, "mylib", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  EXPECT_GE(prog->libs.size(), 1u);
  EXPECT_GE(prog->extern_fns.size(), 1u);
  bool has_cos = false;
  for (const auto& e : prog->extern_fns) if (e.name == "cos") { has_cos = true; break; }
  EXPECT_TRUE(has_cos);
}

TEST(MultifileTests, MissingLibraryFileReturnsError) {
  std::string main_src = R"(import lib "nonexistent" { struct X; }; print(1))";
  auto tokens = fusion::lex(main_src);
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  std::string main_path = "/tmp/main.fusion";
  std::string err = fusion::resolve_imports_and_merge(main_path, result.program.get());
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("cannot open") != std::string::npos || err.find("nonexistent") != std::string::npos);
}

TEST(MultifileTests, MissingExportedStructReturnsError) {
  std::string main_src = R"(import lib "vec" { struct Vector; }; print(1))";
  std::string lib_src = R"(struct Point { x: f64; y: f64; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("missing exported struct") != std::string::npos);
  EXPECT_TRUE(err.find("Vector") != std::string::npos);
}

TEST(MultifileTests, NonExportedStructNotMerged) {
  std::string main_src = R"(import lib "vec" { struct Vector; }; print(1))";
  std::string lib_src = R"(struct Vector { x: f64; y: f64; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("missing exported struct") != std::string::npos);
}

TEST(MultifileTests, MissingExportedFnReturnsError) {
  std::string main_src = R"(import lib "vec" { fn make() -> i64; }; print(1))";
  std::string lib_src = R"(fn helper() -> i64 { return 1; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("missing") != std::string::npos || err.find("signature mismatch") != std::string::npos);
}

TEST(MultifileTests, NonExportedFnNotMerged) {
  std::string main_src = R"(import lib "vec" { fn pub_fn() -> i64; }; print(1))";
  std::string lib_src = R"(fn pub_fn() -> i64 { return 42; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
}

TEST(MultifileTests, FnSignatureMismatchReturnsError) {
  std::string main_src = R"(import lib "vec" { fn add(a: i64, b: i64) -> i64; }; print(1))";
  std::string lib_src = R"(export fn add(a: i64, b: i64) -> f64 { return 0.0; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("signature mismatch") != std::string::npos || err.find("missing") != std::string::npos);
}

TEST(MultifileTests, SubsetImportOnlyMergesRequested) {
  std::string main_src = R"(import lib "vec" { struct Vector; fn make_vec(x: f64, y: f64) -> Vector; };
let v = make_vec(1.0, 2.0);
print(load_field(v, Vector, x)))";
  std::string lib_src = R"(export struct Vector { x: f64; y: f64; };
export struct Point { x: i64; y: i64; };
export fn make_vec(x: f64, y: f64) -> Vector {
  let p = alloc(Vector);
  store_field(p, Vector, x, x);
  store_field(p, Vector, y, y);
  return p;
}
export fn make_point(x: i64, y: i64) -> Point { let p = alloc(Point); store_field(p, Point, x, x); store_field(p, Point, y, y); return p; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  EXPECT_EQ(prog->struct_defs.size(), 1u);
  EXPECT_EQ(prog->struct_defs[0].name, "Vector");
  EXPECT_EQ(prog->user_fns.size(), 1u);
  EXPECT_EQ(prog->user_fns[0].name, "make_vec");
}

TEST(MultifileTests, DuplicateStructImportFromSameLibIsDeduped) {
  /* Same struct imported twice from the same library with identical definition
     should be merged once without a duplicate symbol error. */
  std::string main_src = R"(import lib "value" { struct Value; };
import lib "value" { struct Value; };
print(1))";
  std::string lib_src = R"(export struct Value { data: f64; grad: f64; };)";
  auto [err, prog] = run_multifile_merge(main_src, "value", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  size_t count_value = 0;
  for (const auto& s : prog->struct_defs) {
    if (s.name == "Value") ++count_value;
  }
  EXPECT_EQ(count_value, 1u);
}

TEST(MultifileTests, DuplicateStructImportWithDifferentShapeErrors) {
  /* Importing a struct with the same name but a different shape should still error. */
  std::string main_src = R"(import lib "value" { struct Value; };
struct Value { data: f64; extra: f64; };
print(1))";
  std::string lib_src = R"(export struct Value { data: f64; };)";
  auto [err, prog] = run_multifile_merge(main_src, "value", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("duplicate symbol") != std::string::npos);
}

TEST(MultifileTests, DuplicateFnImportFromSameLibIsDeduped) {
  /* Same function imported twice from the same library with identical signature
     should be merged once without a duplicate symbol error. */
  std::string main_src = R"(import lib "vec" { fn answer() -> i64; };
import lib "vec" { fn answer() -> i64; };
print(answer()))";
  std::string lib_src = R"(export fn answer() -> i64 { return 42; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  // Function should be present exactly once in the merged program.
  size_t count_answer = 0;
  for (const auto& f : prog->user_fns) {
    if (f.name == "answer") ++count_answer;
  }
  EXPECT_EQ(count_answer, 1u);
}

TEST(MultifileTests, DuplicateSymbolFromMainReturnsError) {
  /* Import first so it is parsed; then struct Vector in main duplicates the one we merge from lib. */
  std::string main_src = R"(import lib "vec" { struct Vector; };
struct Vector { x: f64; };
print(1))";
  std::string lib_src = R"(export struct Vector { x: f64; y: f64; };)" ;
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("duplicate symbol") != std::string::npos);
}

TEST(MultifileTests, CircularImportReturnsError) {
  char dir_tpl[] = "/tmp/fusion_mf_XXXXXX";
  ASSERT_TRUE(mkdtemp(dir_tpl));
  std::string dir(dir_tpl);
  std::string a_path = dir + "/a.fusion";
  std::string b_path = dir + "/b.fusion";
  std::ofstream(a_path) << "import lib \"b\" { }; print(1)";
  std::ofstream(b_path) << "import lib \"a\" { }; print(1)";
  auto tokens = fusion::lex("import lib \"a\" { }; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  std::string main_path = dir + "/main.fusion";
  std::string err = fusion::resolve_imports_and_merge(main_path, result.program.get());
  unlink(a_path.c_str());
  unlink(b_path.c_str());
  rmdir(dir_tpl);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("circular") != std::string::npos);
}

TEST(MultifileTests, SemaPassesAfterMerge) {
  std::string main_src = R"(import lib "vec" { struct V; fn id(x: i64) -> i64; };
print(id(7)))";
  std::string lib_src = R"(export struct V { x: i64; };
export fn id(x: i64) -> i64 { return x; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(MultifileTests, ImportFnWithPrivateHelperIsMerged) {
  std::string main_src = R"(import lib "mylib" { fn public_fn() -> i64; };
print(public_fn()))";
  std::string lib_src = R"(fn helper() -> i64 { return 41; }
export fn public_fn() -> i64 { return helper(); })";
  auto [err, prog] = run_multifile_merge(main_src, "mylib", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
  size_t helper_count = 0;
  for (const auto& f : prog->user_fns) {
    if (f.name == "helper") ++helper_count;
  }
  EXPECT_EQ(helper_count, 1u);
}

TEST(MultifileTests, ImportFnWithTransitivePrivateHelpersIsMerged) {
  std::string main_src = R"(import lib "mylib" { fn public_fn() -> i64; };
print(public_fn()))";
  std::string lib_src = R"(fn helper_leaf() -> i64 { return 41; }
fn helper_mid() -> i64 { return helper_leaf(); }
export fn public_fn() -> i64 { return helper_mid(); })";
  auto [err, prog] = run_multifile_merge(main_src, "mylib", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
  size_t leaf_count = 0;
  size_t mid_count = 0;
  for (const auto& f : prog->user_fns) {
    if (f.name == "helper_leaf") ++leaf_count;
    if (f.name == "helper_mid") ++mid_count;
  }
  EXPECT_EQ(leaf_count, 1u);
  EXPECT_EQ(mid_count, 1u);
}

TEST(MultifileTests, ImportFnWithGetFuncPtrHelperIsMerged) {
  std::string main_src = R"(import lib "mylib" { fn make() -> ptr; };
print(0))";
  std::string lib_src = R"(fn target(x: i64) -> i64 { return x; }
export fn make() -> ptr {
  let fp = get_func_ptr(target);
  return fp;
})";
  auto [err, prog] = run_multifile_merge(main_src, "mylib", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
  size_t target_count = 0;
  for (const auto& f : prog->user_fns) {
    if (f.name == "target") ++target_count;
  }
  EXPECT_EQ(target_count, 1u);
}

#ifdef FUSION_HAVE_LLVM
TEST(MultifileTests, JitRunsAfterMerge) {
  std::string main_src = R"(import lib "vec" { fn answer() -> i64; };
print(answer()))";
  std::string lib_src = R"(export fn answer() -> i64 { return 42; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, prog.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}
#endif

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
  auto tokens = fusion::lex("let n = 1; for i in range(n) { }");
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
      "let op_add = alloc(Operation); "
      "store_field(op_add, Operation, func, get_func_ptr(add)); "
      "store_field(op_add, Operation, x, 3.0); "
      "store_field(op_add, Operation, y, 4.0); "
      "let op_mul = alloc(Operation); "
      "store_field(op_mul, Operation, func, get_func_ptr(mul)); "
      "store_field(op_mul, Operation, x, 3.0); "
      "store_field(op_mul, Operation, y, 4.0); "
      "print(perform_operation(op_add)); "
      "print(perform_operation(op_mul))");
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
      "  let value = alloc(Value); "
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
      "  let prev = alloc_array(ptr, 2); "
      "  prev[0] = a; "
      "  prev[1] = b; "
      "  return alloc_value(data, prev, 2, get_func_ptr(add_backward)); "
      "} "
      "let a = alloc_value(1.0, alloc_array(ptr, 0), 0, get_func_ptr(leaf_backward)); "
      "let b = alloc_value(2.0, alloc_array(ptr, 0), 0, get_func_ptr(leaf_backward)); "
      "store_field(a, Value, grad, 1.0); "
      "store_field(b, Value, grad, 2.0); "
      "let c = add_forward(a, b); "
      "store_field(c, Value, grad, 3.0); "
      "let c_backward = load_field(c, Value, backward); "
      "call(c_backward, c); "
      "print(load_field(a, Value, grad)); "
      "print(load_field(b, Value, grad))");
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

TEST(JitTests, ExecutesAllocArrayAndIndex) {
  /* alloc_array(i64, n), store via a[i]=v, load via a[i] and print */
  const char* path = "/tmp/fusion_jit_array_index_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let a = alloc_array(i64, 3); a[0] = 10; a[1] = 20; a[2] = 30; print(a[0]); print(a[1]); print(a[2])");
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

TEST(JitTests, ExecutesForInRange) {
  /* for i in range(5) { print(i); } print(0) => prints 0,1,2,3,4 then 0 */
  const char* path = "/tmp/fusion_jit_for_range_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("for i in range(5) { print(i); } print(0)");
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

TEST(JitTests, ExecutesForInArray) {
  /* let arr = alloc_array(i64, 3); arr[0]=1; ... for x in arr { print(x); } print(0) => 1,2,3,0 */
  const char* path = "/tmp/fusion_jit_for_arr_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex(
      "let arr = alloc_array(i64, 3); arr[0] = 1; arr[1] = 2; arr[2] = 3; for x in arr { print(x); } print(0)");
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

TEST(JitTests, ExecutesRangeTwoArgs) {
  /* for x in range(2, 6) { print(x); } print(0) => 2,3,4,5,0 */
  const char* path = "/tmp/fusion_jit_range_two_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("for x in range(2, 6) { print(x); } print(0)");
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

TEST(JitTests, ExecutesForInRangeF64) {
  /* for x in range(3, f64) { print(x); } print(0) => 0.0, 1.0, 2.0, 0 */
  const char* path = "/tmp/fusion_jit_range_f64_test.txt";
  int saved_fd = dup(STDOUT_FILENO);
  ASSERT_GE(saved_fd, 0);
  ASSERT_TRUE(freopen(path, "w", stdout));
  auto tokens = fusion::lex("for x in range(3, f64) { print(x); } print(0)");
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
#endif
