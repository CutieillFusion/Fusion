#ifndef FUSION_LEXER_HPP
#define FUSION_LEXER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace fusion {

enum class TokenKind {
  Eof,
  IntLiteral,
  Ident,
  LParen,
  RParen,
  Plus,
};

struct Token {
  TokenKind kind = TokenKind::Eof;
  int64_t int_value = 0;
  std::string ident;
  size_t line = 0;
  size_t column = 0;
};

std::vector<Token> lex(const std::string& source);

}  // namespace fusion

#endif
