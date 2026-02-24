#include "lexer.hpp"
#include <cctype>

namespace fusion {

std::vector<Token> lex(const std::string& source) {
  std::vector<Token> tokens;
  size_t i = 0;
  size_t line = 1;
  size_t column = 1;

  auto advance = [&]() {
    if (i < source.size()) {
      if (source[i] == '\n') {
        line++;
        column = 1;
      } else {
        column++;
      }
      i++;
    }
  };

  while (i < source.size()) {
    while (i < source.size() && (source[i] == ' ' || source[i] == '\t' || source[i] == '\n' || source[i] == '\r')) {
      advance();
    }
    if (i >= source.size()) break;

    size_t start_line = line;
    size_t start_col = column;

    if (std::isdigit(static_cast<unsigned char>(source[i]))) {
      int64_t value = 0;
      while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
        value = value * 10 + static_cast<int64_t>(source[i] - '0');
        advance();
      }
      Token t;
      t.kind = TokenKind::IntLiteral;
      t.int_value = value;
      t.line = start_line;
      t.column = start_col;
      tokens.push_back(std::move(t));
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(source[i])) || source[i] == '_') {
      std::string ident;
      while (i < source.size() &&
             (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) {
        ident += source[i];
        advance();
      }
      Token t;
      t.kind = TokenKind::Ident;
      t.ident = std::move(ident);
      t.line = start_line;
      t.column = start_col;
      tokens.push_back(std::move(t));
      continue;
    }

    if (source[i] == '(') {
      tokens.push_back({TokenKind::LParen, 0, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == ')') {
      tokens.push_back({TokenKind::RParen, 0, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == '+') {
      tokens.push_back({TokenKind::Plus, 0, {}, start_line, start_col});
      advance();
      continue;
    }

    advance();
  }

  tokens.push_back({TokenKind::Eof, 0, {}, line, column});
  return tokens;
}

}  // namespace fusion
