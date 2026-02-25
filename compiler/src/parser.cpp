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

static bool parse_opaque_decl(const std::vector<Token>& tokens, size_t& i, Program& prog) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwOpaque) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
  prog.opaque_types.push_back(tokens[i].ident);
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return false;
  i++;
  return true;
}

static bool parse_struct_def(const std::vector<Token>& tokens, size_t& i, Program& prog) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwStruct) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
  StructDef def;
  def.name = tokens[i].ident;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LCurly) return false;
  i++;
  while (!at_eof(tokens, i) && tokens[i].kind != TokenKind::RCurly) {
    if (tokens[i].kind != TokenKind::Ident) return false;
    std::string fname = tokens[i].ident;
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Colon) return false;
    i++;
    if (at_eof(tokens, i) || !is_type_keyword(tokens[i].kind)) return false;
    def.fields.push_back({std::move(fname), token_to_ffi_type(tokens[i].kind)});
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return false;
    i++;
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RCurly) return false;
  i++;
  if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Semicolon) i++;
  prog.struct_defs.push_back(std::move(def));
  return true;
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
    if (name == "alloc") {
      if (at_eof(tokens, i)) return nullptr;
      std::string type_name;
      if (tokens[i].kind == TokenKind::Ident) {
        type_name = tokens[i].ident;
      } else if (is_type_keyword(tokens[i].kind)) {
        switch (tokens[i].kind) {
          case TokenKind::KwI32: type_name = "i32"; break;
          case TokenKind::KwI64: type_name = "i64"; break;
          case TokenKind::KwF32: type_name = "f32"; break;
          case TokenKind::KwF64: type_name = "f64"; break;
          case TokenKind::KwPtr: type_name = "ptr"; break;
          case TokenKind::KwCstring: type_name = "cstring"; break;
          default: return nullptr;
        }
      } else {
        return nullptr;
      }
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_alloc(std::move(type_name));
    }
    if (name == "alloc_bytes") {
      ExprPtr size_expr = parse_expr(tokens, i);
      if (!size_expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_alloc_bytes(std::move(size_expr));
    }
    if (name == "addr_of") {
      ExprPtr inner = parse_expr(tokens, i);
      if (!inner || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_addr_of(std::move(inner));
    }
    if (name == "load") {
      ExprPtr inner = parse_expr(tokens, i);
      if (!inner || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_load(std::move(inner));
    }
    if (name == "load_f64") {
      ExprPtr inner = parse_expr(tokens, i);
      if (!inner || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_load_f64(std::move(inner));
    }
    if (name == "load_i32") {
      ExprPtr inner = parse_expr(tokens, i);
      if (!inner || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_load_i32(std::move(inner));
    }
    if (name == "load_ptr") {
      ExprPtr inner = parse_expr(tokens, i);
      if (!inner || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_load_ptr(std::move(inner));
    }
    if (name == "store") {
      ExprPtr ptr_expr = parse_expr(tokens, i);
      if (!ptr_expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::Comma) return nullptr;
      i++;
      ExprPtr val_expr = parse_expr(tokens, i);
      if (!val_expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_store(std::move(ptr_expr), std::move(val_expr));
    }
    if (name == "load_field") {
      ExprPtr ptr_expr = parse_expr(tokens, i);
      if (!ptr_expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::Comma) return nullptr;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return nullptr;
      std::string struct_name = tokens[i].ident;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Comma) return nullptr;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return nullptr;
      std::string field_name = tokens[i].ident;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_load_field(std::move(ptr_expr), std::move(struct_name), std::move(field_name));
    }
    if (name == "store_field") {
      ExprPtr ptr_expr = parse_expr(tokens, i);
      if (!ptr_expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::Comma) return nullptr;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return nullptr;
      std::string struct_name = tokens[i].ident;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Comma) return nullptr;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return nullptr;
      std::string field_name = tokens[i].ident;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Comma) return nullptr;
      i++;
      ExprPtr val_expr = parse_expr(tokens, i);
      if (!val_expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_store_field(std::move(ptr_expr), std::move(struct_name), std::move(field_name), std::move(val_expr));
    }
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

  if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwAs) {
    i++;
    if (at_eof(tokens, i)) return nullptr;
    std::string type_name;
    if (tokens[i].kind == TokenKind::KwPtr) type_name = "ptr";
    else if (tokens[i].kind == TokenKind::KwCstring) type_name = "cstring";
    else return nullptr;
    i++;
    left = Expr::make_cast(std::move(left), std::move(type_name));
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
  if (lib.name.empty())
    lib.name = "__lib" + std::to_string(prog.libs.size());
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
    if (at_eof(tokens, i)) return false;
    if (is_type_keyword(tokens[i].kind)) {
      ext.params.push_back({std::move(pname), token_to_ffi_type(tokens[i].kind)});
      ext.param_type_names.push_back("");
    } else if (tokens[i].kind == TokenKind::Ident) {
      ext.params.push_back({std::move(pname), FfiType::Ptr});
      ext.param_type_names.push_back(tokens[i].ident);
    } else {
      return false;
    }
    i++;
    while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Comma) {
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
      std::string pname2 = tokens[i].ident;
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Colon) return false;
      i++;
      if (at_eof(tokens, i)) return false;
      if (is_type_keyword(tokens[i].kind)) {
        ext.params.push_back({std::move(pname2), token_to_ffi_type(tokens[i].kind)});
        ext.param_type_names.push_back("");
      } else if (tokens[i].kind == TokenKind::Ident) {
        ext.params.push_back({std::move(pname2), FfiType::Ptr});
        ext.param_type_names.push_back(tokens[i].ident);
      } else {
        return false;
      }
      i++;
    }
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Arrow) return false;
  i++;
  if (at_eof(tokens, i)) return false;
  if (is_type_keyword(tokens[i].kind)) {
    ext.return_type = token_to_ffi_type(tokens[i].kind);
    ext.return_type_name.clear();
  } else if (tokens[i].kind == TokenKind::Ident) {
    ext.return_type = FfiType::Ptr;
    ext.return_type_name = tokens[i].ident;
  } else {
    return false;
  }
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
  prog.top_level.push_back(std::move(binding));
  return true;
}

ParseResult parse(const std::vector<Token>& tokens) {
  size_t i = 0;
  auto prog = std::make_unique<Program>();

  while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwOpaque) {
    if (!parse_opaque_decl(tokens, i, *prog)) {
      size_t line = 1, col = 1;
      if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
      return fail("invalid opaque declaration", line, col);
    }
  }

  while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwStruct) {
    if (!parse_struct_def(tokens, i, *prog)) {
      size_t line = 1, col = 1;
      if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
      return fail("invalid struct definition", line, col);
    }
  }

  std::string current_lib_name;
  while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwExtern) {
    size_t save = i;
    if (parse_extern_lib(tokens, i, *prog)) {
      current_lib_name = prog->libs.back().name;
      continue;
    }
    i = save;
    if (parse_extern_fn(tokens, i, *prog)) {
      prog->extern_fns.back().lib_name = current_lib_name;
      continue;
    }
    break;
  }

  /* Ordered list of let bindings and expressions (at least one expression required). */
  bool has_expr = false;
  while (!at_eof(tokens, i) && tokens[i].kind != TokenKind::Eof) {
    if (tokens[i].kind == TokenKind::KwLet) {
      if (!parse_let_binding(tokens, i, *prog)) {
        size_t line = 1, col = 1;
        if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
        return fail("invalid let binding", line, col);
      }
      continue;
    }
    ExprPtr expr = parse_expr(tokens, i);
    if (!expr) {
      size_t line = 1, col = 1;
      if (i < tokens.size()) {
        line = tokens[i].line;
        col = tokens[i].column;
      }
      if (has_expr)
        return fail("parse error", line, col);
      return fail("expected expression or let binding", line, col);
    }
    prog->top_level.push_back(std::move(expr));
    has_expr = true;
    if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Semicolon) {
      i++;
    }
  }
  if (!has_expr) {
    return fail("expected expression", tokens.empty() ? 1u : tokens.back().line, tokens.empty() ? 1u : tokens.back().column);
  }
  ParseResult r;
  r.program = std::move(prog);
  return r;
}

}  // namespace fusion
