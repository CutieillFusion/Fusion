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
