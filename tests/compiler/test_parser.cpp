#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <gtest/gtest.h>
#include <variant>

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
  auto tokens = fusion::lex("let n = 1; for (let i = 0; i < n; i = i + 1) { }");
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
      "export struct Point { x: f64; y: f64; }; export fn zero() -> Point { let p = heap(Point); store_field(p, Point, x, 0.0); store_field(p, Point, y, 0.0); return p; } print(1)");
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

TEST(ParserTests, ParsesStackAlloc) {
  auto tokens = fusion::lex("let x = stack(i64); print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 1u);
  const fusion::LetBinding* binding = std::get_if<fusion::LetBinding>(&result.program->top_level[0]);
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->name, "x");
  ASSERT_TRUE(binding->init);
  EXPECT_EQ(binding->init->kind, fusion::Expr::Kind::StackAlloc);
  EXPECT_EQ(binding->init->var_name, "i64");
}

TEST(ParserTests, ParsesHeapAlloc) {
  auto tokens = fusion::lex("struct Point { x: f64; y: f64; }; let x = heap(Point); print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 2u);
  const fusion::LetBinding* binding = std::get_if<fusion::LetBinding>(&result.program->top_level[0]);
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->name, "x");
  ASSERT_TRUE(binding->init);
  EXPECT_EQ(binding->init->kind, fusion::Expr::Kind::HeapAlloc);
  EXPECT_EQ(binding->init->var_name, "Point");
}

TEST(ParserTests, ParsesStackArray) {
  auto tokens = fusion::lex("let a = stack_array(i64, 10); print(a[0])");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 2u);
  const fusion::LetBinding* binding = std::get_if<fusion::LetBinding>(&result.program->top_level[0]);
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->name, "a");
  ASSERT_TRUE(binding->init);
  EXPECT_EQ(binding->init->kind, fusion::Expr::Kind::StackArray);
  EXPECT_EQ(binding->init->var_name, "i64");
  ASSERT_TRUE(binding->init->left);
  EXPECT_EQ(binding->init->left->kind, fusion::Expr::Kind::IntLiteral);
  EXPECT_EQ(binding->init->left->int_value, 10);
}

TEST(ParserTests, ParsesFree) {
  auto tokens = fusion::lex("let p = heap(i64); free(p); print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 2u);
  const fusion::ExprPtr* expr_ptr = std::get_if<fusion::ExprPtr>(&result.program->top_level[1]);
  ASSERT_NE(expr_ptr, nullptr);
  ASSERT_TRUE(*expr_ptr);
  EXPECT_EQ((*expr_ptr)->kind, fusion::Expr::Kind::Free);
  ASSERT_TRUE((*expr_ptr)->left);
  EXPECT_EQ((*expr_ptr)->left->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ((*expr_ptr)->left->var_name, "p");
}

TEST(ParserTests, ParsesFreeArray) {
  auto tokens = fusion::lex("let arr = heap_array(i64, 5); free_array(arr); print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 2u);
  const fusion::ExprPtr* expr_ptr = std::get_if<fusion::ExprPtr>(&result.program->top_level[1]);
  ASSERT_NE(expr_ptr, nullptr);
  ASSERT_TRUE(*expr_ptr);
  EXPECT_EQ((*expr_ptr)->kind, fusion::Expr::Kind::FreeArray);
  ASSERT_TRUE((*expr_ptr)->left);
  EXPECT_EQ((*expr_ptr)->left->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ((*expr_ptr)->left->var_name, "arr");
}

TEST(ParserTests, ParsesAsHeap) {
  auto tokens = fusion::lex("fn f(p: ptr) -> void { free(as_heap(p)); } print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->user_fns.size(), 1u);
  const fusion::FnDef& def = result.program->user_fns[0];
  ASSERT_GE(def.body.size(), 1u);
  ASSERT_TRUE(def.body[0]->expr);
  EXPECT_EQ(def.body[0]->expr->kind, fusion::Expr::Kind::Free);
  ASSERT_TRUE(def.body[0]->expr->left);
  EXPECT_EQ(def.body[0]->expr->left->kind, fusion::Expr::Kind::AsHeap);
  ASSERT_TRUE(def.body[0]->expr->left->left);
  EXPECT_EQ(def.body[0]->expr->left->left->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(def.body[0]->expr->left->left->var_name, "p");
}

TEST(ParserTests, ParsesAsArray) {
  auto tokens = fusion::lex("fn f(p: ptr) -> void { free_array(as_array(p, ptr)); } print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->user_fns.size(), 1u);
  const fusion::FnDef& def = result.program->user_fns[0];
  ASSERT_GE(def.body.size(), 1u);
  ASSERT_TRUE(def.body[0]->expr);
  EXPECT_EQ(def.body[0]->expr->kind, fusion::Expr::Kind::FreeArray);
  ASSERT_TRUE(def.body[0]->expr->left);
  EXPECT_EQ(def.body[0]->expr->left->kind, fusion::Expr::Kind::AsArray);
  EXPECT_EQ(def.body[0]->expr->left->var_name, "ptr");
  ASSERT_TRUE(def.body[0]->expr->left->left);
  EXPECT_EQ(def.body[0]->expr->left->left->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(def.body[0]->expr->left->left->var_name, "p");
}

TEST(ParserTests, ParsesNoescapeParam) {
  auto tokens = fusion::lex("fn sum(noescape buf: ptr, n: i64) -> i64 { return 0; } print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->user_fns.size(), 1u);
  const fusion::FnDef& def = result.program->user_fns[0];
  ASSERT_EQ(def.params.size(), 2u);
  ASSERT_EQ(def.param_noescape.size(), 2u);
  EXPECT_TRUE(def.param_noescape[0]) << "first param should be noescape";
  EXPECT_FALSE(def.param_noescape[1]) << "second param should not be noescape";
}

TEST(ParserTests, ParsesStackArrayI8) {
  auto tokens = fusion::lex("let n = 100; let buf = stack_array(i8, n); print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 2u);
  const fusion::LetBinding* binding = std::get_if<fusion::LetBinding>(&result.program->top_level[1]);
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->name, "buf");
  ASSERT_TRUE(binding->init);
  EXPECT_EQ(binding->init->kind, fusion::Expr::Kind::StackArray);
  EXPECT_EQ(binding->init->var_name, "i8");
  ASSERT_TRUE(binding->init->left);
  EXPECT_EQ(binding->init->left->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(binding->init->left->var_name, "n");
}

TEST(ParserTests, ParsesAllocArray) {
  auto tokens = fusion::lex("let a = heap_array(i64, 5); print(a[0])");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_EQ(result.program->top_level.size(), 2u);
  const fusion::LetBinding* binding = std::get_if<fusion::LetBinding>(&result.program->top_level[0]);
  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->name, "a");
  ASSERT_TRUE(binding->init);
  EXPECT_EQ(binding->init->kind, fusion::Expr::Kind::HeapArray);
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

TEST(ParserTests, ParsesCStyleFor) {
  auto tokens = fusion::lex("for (let i = 0; i < 10; i = i + 1) { print(i); } print(0)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 1u);
  const fusion::StmtPtr* stmt = std::get_if<fusion::StmtPtr>(&result.program->top_level[0]);
  ASSERT_NE(stmt, nullptr);
  ASSERT_TRUE(*stmt);
  EXPECT_EQ((*stmt)->kind, fusion::Stmt::Kind::For);
  ASSERT_TRUE((*stmt)->for_init);
  EXPECT_EQ((*stmt)->for_init->kind, fusion::Stmt::Kind::Let);
  EXPECT_EQ((*stmt)->for_init->name, "i");
  ASSERT_TRUE((*stmt)->cond);
  EXPECT_EQ((*stmt)->cond->kind, fusion::Expr::Kind::Compare);
  ASSERT_TRUE((*stmt)->for_update);
  EXPECT_EQ((*stmt)->for_update->kind, fusion::Stmt::Kind::Assign);
  ASSERT_GE((*stmt)->body.size(), 1u);
  EXPECT_EQ((*stmt)->body[0]->kind, fusion::Stmt::Kind::Expr);
  EXPECT_EQ((*stmt)->body[0]->expr->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ((*stmt)->body[0]->expr->callee, "print");
}

TEST(ParserTests, ParsesLen) {
  auto tokens = fusion::lex("let arr = heap_array(i64, 5); let n = len(arr); print(n)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok()) << result.error.message;
  ASSERT_TRUE(result.program);
  ASSERT_GE(result.program->top_level.size(), 2u);
  const fusion::LetBinding* b = std::get_if<fusion::LetBinding>(&result.program->top_level[1]);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name, "n");
  ASSERT_TRUE(b->init);
  EXPECT_EQ(b->init->kind, fusion::Expr::Kind::Call);
  EXPECT_EQ(b->init->callee, "len");
  ASSERT_EQ(b->init->args.size(), 1u);
  EXPECT_EQ(b->init->args[0]->kind, fusion::Expr::Kind::VarRef);
  EXPECT_EQ(b->init->args[0]->var_name, "arr");
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
  auto tokens = fusion::lex("let a = heap_array(i64, 3); a[0] = 42; print(a[0])");
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
