#include "lexer.hpp"
#include <gtest/gtest.h>

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

TEST(LexerTests, TokenizesBracketsAndFor) {
  auto tokens = fusion::lex("for (let i = 0; i < 10; i = i + 1) { print(arr[i]); }");
  ASSERT_GE(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].kind, fusion::TokenKind::KwFor);
  EXPECT_EQ(tokens[1].kind, fusion::TokenKind::LParen);
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

TEST(LexerTests, TokenizesFloatLiteral) {
  auto tokens = fusion::lex("3.14");
  ASSERT_GE(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].kind, fusion::TokenKind::FloatLiteral);
  EXPECT_NEAR(tokens[0].float_value, 3.14, 1e-9);
}

TEST(LexerTests, TokenizesStringLiteral) {
  auto tokens = fusion::lex("\"hello\"");
  ASSERT_GE(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].kind, fusion::TokenKind::StringLiteral);
  EXPECT_EQ(tokens[0].str_value, "hello");
}

TEST(LexerTests, TokenizesComparisonOps) {
  auto tokens = fusion::lex("a == b != c <= d >= e");
  size_t eqeq = 0, ne = 0, le = 0, ge = 0;
  for (const auto& t : tokens) {
    if (t.kind == fusion::TokenKind::EqEq) ++eqeq;
    if (t.kind == fusion::TokenKind::Ne) ++ne;
    if (t.kind == fusion::TokenKind::Le) ++le;
    if (t.kind == fusion::TokenKind::Ge) ++ge;
  }
  EXPECT_EQ(eqeq, 1u) << "expected one ==";
  EXPECT_EQ(ne, 1u) << "expected one !=";
  EXPECT_EQ(le, 1u) << "expected one <=";
  EXPECT_EQ(ge, 1u) << "expected one >=";
}

TEST(LexerTests, TokenizesKeywordsFnStructLet) {
  auto tokens = fusion::lex("fn f() {}; struct S {}; let x = 0;");
  size_t kw_fn = 0, kw_struct = 0, kw_let = 0;
  for (const auto& t : tokens) {
    if (t.kind == fusion::TokenKind::KwFn) ++kw_fn;
    if (t.kind == fusion::TokenKind::KwStruct) ++kw_struct;
    if (t.kind == fusion::TokenKind::KwLet) ++kw_let;
  }
  EXPECT_EQ(kw_fn, 1u) << "expected KwFn";
  EXPECT_EQ(kw_struct, 1u) << "expected KwStruct";
  EXPECT_EQ(kw_let, 1u) << "expected KwLet";
}

TEST(LexerTests, TokenizesKeywordsIfElifElse) {
  auto tokens = fusion::lex("if elif else");
  size_t kw_if = 0, kw_elif = 0, kw_else = 0;
  for (const auto& t : tokens) {
    if (t.kind == fusion::TokenKind::KwIf) ++kw_if;
    if (t.kind == fusion::TokenKind::KwElif) ++kw_elif;
    if (t.kind == fusion::TokenKind::KwElse) ++kw_else;
  }
  EXPECT_EQ(kw_if, 1u) << "expected KwIf";
  EXPECT_EQ(kw_elif, 1u) << "expected KwElif";
  EXPECT_EQ(kw_else, 1u) << "expected KwElse";
}

TEST(LexerTests, TokenizesKeywordsReturnAsOpaque) {
  auto tokens = fusion::lex("return as opaque extern");
  size_t kw_return = 0, kw_as = 0, kw_opaque = 0, kw_extern = 0;
  for (const auto& t : tokens) {
    if (t.kind == fusion::TokenKind::KwReturn) ++kw_return;
    if (t.kind == fusion::TokenKind::KwAs) ++kw_as;
    if (t.kind == fusion::TokenKind::KwOpaque) ++kw_opaque;
    if (t.kind == fusion::TokenKind::KwExtern) ++kw_extern;
  }
  EXPECT_EQ(kw_return, 1u) << "expected KwReturn";
  EXPECT_EQ(kw_as, 1u) << "expected KwAs";
  EXPECT_EQ(kw_opaque, 1u) << "expected KwOpaque";
  EXPECT_EQ(kw_extern, 1u) << "expected KwExtern";
}

TEST(LexerTests, TokenizesColonAndArrow) {
  auto tokens = fusion::lex("x: i64 -> void");
  size_t colon = 0, arrow = 0;
  for (const auto& t : tokens) {
    if (t.kind == fusion::TokenKind::Colon) ++colon;
    if (t.kind == fusion::TokenKind::Arrow) ++arrow;
  }
  EXPECT_EQ(colon, 1u) << "expected Colon";
  EXPECT_EQ(arrow, 1u) << "expected Arrow";
}

TEST(LexerTests, TokenizesAssignVsEquals) {
  auto tokens = fusion::lex("x = 1; x == 1");
  size_t assign = 0, eqeq = 0;
  for (const auto& t : tokens) {
    if (t.kind == fusion::TokenKind::Equals) ++assign;
    if (t.kind == fusion::TokenKind::EqEq) ++eqeq;
  }
  EXPECT_EQ(assign, 1u) << "expected one = (Equals)";
  EXPECT_EQ(eqeq, 1u) << "expected one == (EqEq)";
}

TEST(LexerTests, TokenizesLtAndGt) {
  auto tokens = fusion::lex("a < b > c");
  size_t lt = 0, gt = 0;
  for (const auto& t : tokens) {
    if (t.kind == fusion::TokenKind::Lt) ++lt;
    if (t.kind == fusion::TokenKind::Gt) ++gt;
  }
  EXPECT_EQ(lt, 1u) << "expected one <";
  EXPECT_EQ(gt, 1u) << "expected one >";
}

TEST(LexerTests, TokenizesTypeKeywords) {
  auto tokens = fusion::lex("i64 f64 i32 f32 void ptr");
  size_t kw_i64 = 0, kw_f64 = 0, kw_i32 = 0, kw_f32 = 0, kw_void = 0, kw_ptr = 0;
  for (const auto& t : tokens) {
    if (t.kind == fusion::TokenKind::KwI64) ++kw_i64;
    if (t.kind == fusion::TokenKind::KwF64) ++kw_f64;
    if (t.kind == fusion::TokenKind::KwI32) ++kw_i32;
    if (t.kind == fusion::TokenKind::KwF32) ++kw_f32;
    if (t.kind == fusion::TokenKind::KwVoid) ++kw_void;
    if (t.kind == fusion::TokenKind::KwPtr) ++kw_ptr;
  }
  EXPECT_EQ(kw_i64, 1u) << "expected KwI64";
  EXPECT_EQ(kw_f64, 1u) << "expected KwF64";
  EXPECT_EQ(kw_i32, 1u) << "expected KwI32";
  EXPECT_EQ(kw_f32, 1u) << "expected KwF32";
  EXPECT_EQ(kw_void, 1u) << "expected KwVoid";
  EXPECT_EQ(kw_ptr, 1u) << "expected KwPtr";
}
