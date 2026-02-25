#ifndef FUSION_LEXER_HPP
#define FUSION_LEXER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace fusion {

enum class TokenKind {
  Eof,
  IntLiteral,
  FloatLiteral,
  StringLiteral,
  Ident,
  LParen,
  RParen,
  LCurly,  // {
  RCurly,  // }
  Plus,
  Comma,
  Semicolon,
  Colon,
  Equals,  // =
  Arrow,   // ->
  // Keywords (recognized as ident then mapped in parser, or distinct kinds)
  KwExtern,
  KwLib,
  KwFn,
  KwF64,
  KwF32,
  KwI64,
  KwI32,
  KwU64,
  KwU32,
  KwVoid,
  KwPtr,
  KwCstring,
  KwAs,
  KwLet,
  KwReturn,
  KwOpaque,
  KwStruct,
};

struct Token {
  TokenKind kind = TokenKind::Eof;
  int64_t int_value = 0;
  double float_value = 0.0;
  std::string str_value;  // for StringLiteral content (unescaped)
  std::string ident;
  size_t line = 0;
  size_t column = 0;
};

std::vector<Token> lex(const std::string& source);

}  // namespace fusion

#endif
