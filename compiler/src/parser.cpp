#include "parser.hpp"
#include <cstddef>

namespace fusion {

static ExprPtr parse_expr(const std::vector<Token>& tokens, size_t& i);

static bool at_eof(const std::vector<Token>& tokens, size_t i) {
  return i >= tokens.size() || tokens[i].kind == TokenKind::Eof;
}

static ParseResult fail(const std::string& msg, size_t line, size_t column) {
  ParseResult r;
  r.error.message = msg;
  r.error.line = line;
  r.error.column = column;
  return r;
}

static ExprPtr parse_primary(const std::vector<Token>& tokens, size_t& i) {
  if (at_eof(tokens, i)) return nullptr;
  const Token& t = tokens[i];

  if (t.kind == TokenKind::IntLiteral) {
    int64_t value = t.int_value;
    i++;
    return Expr::make_int(value);
  }

  if (t.kind == TokenKind::Ident) {
    std::string callee = t.ident;
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LParen) {
      return nullptr;
    }
    i++;
    if (at_eof(tokens, i)) return nullptr;
    std::vector<ExprPtr> args;
    if (tokens[i].kind != TokenKind::RParen) {
      ExprPtr arg = parse_expr(tokens, i);
      if (!arg) return nullptr;
      args.push_back(std::move(arg));
    }
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
    i++;
    return Expr::make_call(std::move(callee), std::move(args));
  }

  if (t.kind == TokenKind::LParen) {
    i++;
    ExprPtr inner = parse_expr(tokens, i);
    if (!inner) return nullptr;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
    i++;
    return inner;
  }

  return nullptr;
}

static ExprPtr parse_expr(const std::vector<Token>& tokens, size_t& i) {
  ExprPtr left = parse_primary(tokens, i);
  if (!left) return nullptr;

  while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Plus) {
    i++;
    ExprPtr right = parse_primary(tokens, i);
    if (!right) return nullptr;
    left = Expr::make_binop(BinOp::Add, std::move(left), std::move(right));
  }
  return left;
}

ParseResult parse(const std::vector<Token>& tokens) {
  size_t i = 0;
  if (at_eof(tokens, i)) {
    return fail("expected expression", 1, 1);
  }
  ExprPtr expr = parse_expr(tokens, i);
  if (!expr) {
    size_t line = 1, col = 1;
    if (i < tokens.size()) {
      line = tokens[i].line;
      col = tokens[i].column;
    }
    return fail("parse error", line, col);
  }
  if (!at_eof(tokens, i) && tokens[i].kind != TokenKind::Eof) {
    return fail("unexpected token", tokens[i].line, tokens[i].column);
  }
  ParseResult r;
  r.expr = std::move(expr);
  return r;
}

}  // namespace fusion
