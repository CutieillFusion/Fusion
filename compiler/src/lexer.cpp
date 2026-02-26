#include "lexer.hpp"
#include <cctype>
#include <cmath>

namespace fusion {

static TokenKind keyword_from_ident(const std::string& ident) {
  if (ident == "extern") return TokenKind::KwExtern;
  if (ident == "lib") return TokenKind::KwLib;
  if (ident == "fn") return TokenKind::KwFn;
  if (ident == "f64") return TokenKind::KwF64;
  if (ident == "f32") return TokenKind::KwF32;
  if (ident == "i64") return TokenKind::KwI64;
  if (ident == "i32") return TokenKind::KwI32;
  if (ident == "u64") return TokenKind::KwU64;
  if (ident == "u32") return TokenKind::KwU32;
  if (ident == "void") return TokenKind::KwVoid;
  if (ident == "ptr") return TokenKind::KwPtr;
  if (ident == "cstring") return TokenKind::KwCstring;
  if (ident == "as") return TokenKind::KwAs;
  if (ident == "let") return TokenKind::KwLet;
  if (ident == "return") return TokenKind::KwReturn;
  if (ident == "opaque") return TokenKind::KwOpaque;
  if (ident == "struct") return TokenKind::KwStruct;
  if (ident == "if") return TokenKind::KwIf;
  if (ident == "else") return TokenKind::KwElse;
  if (ident == "elif") return TokenKind::KwElif;
  return TokenKind::Ident;
}

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

    if (source[i] == '#') {
      while (i < source.size() && source[i] != '\n') advance();
      continue;
    }

    size_t start_line = line;
    size_t start_col = column;

    if (source[i] == '"') {
      advance();
      std::string str_val;
      while (i < source.size() && source[i] != '"') {
        if (source[i] == '\\') {
          advance();
          if (i >= source.size()) break;
          if (source[i] == 'n') str_val += '\n';
          else if (source[i] == 't') str_val += '\t';
          else if (source[i] == '"') str_val += '"';
          else if (source[i] == '\\') str_val += '\\';
          else str_val += source[i];
          advance();
        } else {
          str_val += source[i];
          advance();
        }
      }
      if (i < source.size()) advance();
      Token t;
      t.kind = TokenKind::StringLiteral;
      t.str_value = std::move(str_val);
      t.line = start_line;
      t.column = start_col;
      tokens.push_back(std::move(t));
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(source[i]))) {
      int64_t int_val = 0;
      while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
        int_val = int_val * 10 + static_cast<int64_t>(source[i] - '0');
        advance();
      }
      double float_val = static_cast<double>(int_val);
      if (i < source.size() && source[i] == '.') {
        advance();
        double frac = 0.0;
        double place = 0.1;
        while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
          frac += (source[i] - '0') * place;
          place *= 0.1;
          advance();
        }
        float_val = int_val + frac;
        Token t;
        t.kind = TokenKind::FloatLiteral;
        t.float_value = float_val;
        t.line = start_line;
        t.column = start_col;
        tokens.push_back(std::move(t));
      } else {
        Token t;
        t.kind = TokenKind::IntLiteral;
        t.int_value = int_val;
        t.line = start_line;
        t.column = start_col;
        tokens.push_back(std::move(t));
      }
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
      t.kind = keyword_from_ident(ident);
      t.ident = std::move(ident);
      t.line = start_line;
      t.column = start_col;
      tokens.push_back(std::move(t));
      continue;
    }

    if (source[i] == '-' && i + 1 < source.size() && source[i + 1] == '>') {
      tokens.push_back({TokenKind::Arrow, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      advance();
      continue;
    }

    if (source[i] == '(') {
      tokens.push_back({TokenKind::LParen, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == ')') {
      tokens.push_back({TokenKind::RParen, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == '{') {
      tokens.push_back({TokenKind::LCurly, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == '}') {
      tokens.push_back({TokenKind::RCurly, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == '+') {
      tokens.push_back({TokenKind::Plus, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == ',') {
      tokens.push_back({TokenKind::Comma, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == ';') {
      tokens.push_back({TokenKind::Semicolon, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == ':') {
      tokens.push_back({TokenKind::Colon, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == '=' && i + 1 < source.size() && source[i + 1] == '=') {
      tokens.push_back({TokenKind::EqEq, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      advance();
      continue;
    }
    if (source[i] == '=') {
      tokens.push_back({TokenKind::Equals, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == '!' && i + 1 < source.size() && source[i + 1] == '=') {
      tokens.push_back({TokenKind::Ne, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      advance();
      continue;
    }
    if (source[i] == '<' && i + 1 < source.size() && source[i + 1] == '=') {
      tokens.push_back({TokenKind::Le, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      advance();
      continue;
    }
    if (source[i] == '<') {
      tokens.push_back({TokenKind::Lt, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }
    if (source[i] == '>' && i + 1 < source.size() && source[i + 1] == '=') {
      tokens.push_back({TokenKind::Ge, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      advance();
      continue;
    }
    if (source[i] == '>') {
      tokens.push_back({TokenKind::Gt, 0, 0.0, {}, {}, start_line, start_col});
      advance();
      continue;
    }

    advance();
  }

  tokens.push_back({TokenKind::Eof, 0, 0.0, {}, {}, line, column});
  return tokens;
}

}  // namespace fusion
