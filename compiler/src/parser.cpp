#include "parser.hpp"
#include <cstddef>

namespace fusion {

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

static FfiType token_to_ffi_type(TokenKind k) {
  switch (k) {
    case TokenKind::KwVoid: return FfiType::Void;
    case TokenKind::KwI32: return FfiType::I32;
    case TokenKind::KwI64: return FfiType::I64;
    case TokenKind::KwF32: return FfiType::F32;
    case TokenKind::KwF64: return FfiType::F64;
    case TokenKind::KwPtr: return FfiType::Ptr;
    case TokenKind::KwCstring: return FfiType::Cstring;
    default: return FfiType::Void;  // invalid, caller checks
  }
}

static bool is_type_keyword(TokenKind k) {
  return k == TokenKind::KwVoid || k == TokenKind::KwI32 || k == TokenKind::KwI64 ||
         k == TokenKind::KwF32 || k == TokenKind::KwF64 || k == TokenKind::KwPtr ||
         k == TokenKind::KwCstring;
}

static ExprPtr parse_expr(const std::vector<Token>& tokens, size_t& i);

static ExprPtr parse_primary(const std::vector<Token>& tokens, size_t& i) {
  if (at_eof(tokens, i)) return nullptr;
  const Token& t = tokens[i];

  if (t.kind == TokenKind::IntLiteral) {
    int64_t value = t.int_value;
    i++;
    return Expr::make_int(value);
  }

  if (t.kind == TokenKind::FloatLiteral) {
    double value = t.float_value;
    i++;
    return Expr::make_float(value);
  }

  if (t.kind == TokenKind::StringLiteral) {
    std::string value = t.str_value;
    i++;
    return Expr::make_string(std::move(value));
  }

  if (t.kind == TokenKind::Ident) {
    std::string name = t.ident;
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LParen) {
      return Expr::make_var_ref(std::move(name));
    }
    i++;
    std::vector<ExprPtr> args;
    if (!at_eof(tokens, i) && tokens[i].kind != TokenKind::RParen) {
      ExprPtr arg = parse_expr(tokens, i);
      if (!arg) return nullptr;
      args.push_back(std::move(arg));
      while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Comma) {
        i++;
        ExprPtr arg2 = parse_expr(tokens, i);
        if (!arg2) return nullptr;
        args.push_back(std::move(arg2));
      }
    }
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
    i++;
    return Expr::make_call(std::move(name), std::move(args));
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

static bool parse_extern_lib(const std::vector<Token>& tokens, size_t& i, Program& prog) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwExtern) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwLib) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::StringLiteral) return false;
  ExternLib lib;
  lib.path = tokens[i].str_value;
  i++;
  if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwAs) {
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
    lib.name = tokens[i].ident;
    i++;
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return false;
  i++;
  prog.libs.push_back(std::move(lib));
  return true;
}

static bool parse_extern_fn(const std::vector<Token>& tokens, size_t& i, Program& prog) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwExtern) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwFn) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
  ExternFn ext;
  ext.name = tokens[i].ident;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LParen) return false;
  i++;
  if (!at_eof(tokens, i) && tokens[i].kind != TokenKind::RParen) {
    if (tokens[i].kind != TokenKind::Ident) return false;
    std::string pname = tokens[i].ident;
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Colon) return false;
    i++;
    if (at_eof(tokens, i) || !is_type_keyword(tokens[i].kind)) return false;
    FfiType pty = token_to_ffi_type(tokens[i].kind);
    i++;
    ext.params.push_back({std::move(pname), pty});
    while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Comma) {
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
      std::string pname2 = tokens[i].ident;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Colon) return false;
      i++;
      if (at_eof(tokens, i) || !is_type_keyword(tokens[i].kind)) return false;
      FfiType pty2 = token_to_ffi_type(tokens[i].kind);
      i++;
      ext.params.push_back({std::move(pname2), pty2});
    }
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Arrow) return false;
  i++;
  if (at_eof(tokens, i) || !is_type_keyword(tokens[i].kind)) return false;
  ext.return_type = token_to_ffi_type(tokens[i].kind);
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return false;
  i++;
  prog.extern_fns.push_back(std::move(ext));
  return true;
}

static bool parse_let_binding(const std::vector<Token>& tokens, size_t& i, Program& prog) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwLet) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
  std::string name = tokens[i].ident;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Equals) return false;
  i++;
  ExprPtr init = parse_expr(tokens, i);
  if (!init) return false;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return false;
  i++;
  LetBinding binding;
  binding.name = std::move(name);
  binding.init = std::move(init);
  prog.bindings.push_back(std::move(binding));
  return true;
}

ParseResult parse(const std::vector<Token>& tokens) {
  size_t i = 0;
  auto prog = std::make_unique<Program>();

  while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwExtern) {
    size_t save = i;
    if (parse_extern_lib(tokens, i, *prog)) continue;
    i = save;
    if (parse_extern_fn(tokens, i, *prog)) continue;
    break;
  }

  while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwLet) {
    if (!parse_let_binding(tokens, i, *prog)) {
      size_t line = 1, col = 1;
      if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
      return fail("invalid let binding", line, col);
    }
  }

  if (at_eof(tokens, i)) {
    return fail("expected expression", tokens.empty() ? 1u : tokens.back().line, tokens.empty() ? 1u : tokens.back().column);
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
  prog->root_expr = std::move(expr);
  ParseResult r;
  r.program = std::move(prog);
  return r;
}

}  // namespace fusion
