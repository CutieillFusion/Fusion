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
    default: return FfiType::Void;  // invalid, caller checks
  }
}

static bool is_type_keyword(TokenKind k) {
  return k == TokenKind::KwVoid || k == TokenKind::KwI32 || k == TokenKind::KwI64 ||
         k == TokenKind::KwF32 || k == TokenKind::KwF64 || k == TokenKind::KwPtr;
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
  if (at_eof(tokens, i)) return false;
  bool exported = false;
  if (tokens[i].kind == TokenKind::KwExport) {
    exported = true;
    i++;
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwStruct) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
  StructDef def;
  def.name = tokens[i].ident;
  def.exported = exported;
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
static bool parse_block(const std::vector<Token>& tokens, size_t& i, std::vector<StmtPtr>& out);

static bool is_comparison(TokenKind k) {
  return k == TokenKind::EqEq || k == TokenKind::Ne || k == TokenKind::Lt ||
         k == TokenKind::Gt || k == TokenKind::Le || k == TokenKind::Ge;
}

static CompareOp token_to_compare_op(TokenKind k) {
  switch (k) {
    case TokenKind::EqEq: return CompareOp::Eq;
    case TokenKind::Ne:   return CompareOp::Ne;
    case TokenKind::Lt:   return CompareOp::Lt;
    case TokenKind::Gt:   return CompareOp::Gt;
    case TokenKind::Le:   return CompareOp::Le;
    case TokenKind::Ge:   return CompareOp::Ge;
    default: return CompareOp::Eq;
  }
}

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
    if (name == "alloc_array") {
      if (at_eof(tokens, i)) return nullptr;
      std::string elem_type;
      if (tokens[i].kind == TokenKind::Ident) {
        elem_type = tokens[i].ident;
      } else if (is_type_keyword(tokens[i].kind)) {
        switch (tokens[i].kind) {
          case TokenKind::KwI32: elem_type = "i32"; break;
          case TokenKind::KwI64: elem_type = "i64"; break;
          case TokenKind::KwF32: elem_type = "f32"; break;
          case TokenKind::KwF64: elem_type = "f64"; break;
          case TokenKind::KwPtr: elem_type = "ptr"; break;
          default: return nullptr;
        }
      } else {
        return nullptr;
      }
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Comma) return nullptr;
      i++;
      ExprPtr count_expr = parse_expr(tokens, i);
      if (!count_expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_alloc_array(std::move(elem_type), std::move(count_expr));
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
    if (name == "range") {
      std::vector<ExprPtr> args;
      ExprPtr first = parse_expr(tokens, i);
      if (!first) return nullptr;
      args.push_back(std::move(first));
      std::string call_type_arg;
      if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Comma) {
        i++;
        if (!at_eof(tokens, i)) {
          if (tokens[i].kind == TokenKind::KwI64) { call_type_arg = "i64"; i++; }
          else if (tokens[i].kind == TokenKind::KwI32) { call_type_arg = "i32"; i++; }
          else if (tokens[i].kind == TokenKind::KwF64) { call_type_arg = "f64"; i++; }
          else if (tokens[i].kind == TokenKind::KwF32) { call_type_arg = "f32"; i++; }
          else {
            ExprPtr second = parse_expr(tokens, i);
            if (!second) return nullptr;
            args.push_back(std::move(second));
            if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Comma) {
              i++;
              if (!at_eof(tokens, i)) {
                if (tokens[i].kind == TokenKind::KwI64) { call_type_arg = "i64"; i++; }
                else if (tokens[i].kind == TokenKind::KwI32) { call_type_arg = "i32"; i++; }
                else if (tokens[i].kind == TokenKind::KwF64) { call_type_arg = "f64"; i++; }
                else if (tokens[i].kind == TokenKind::KwF32) { call_type_arg = "f32"; i++; }
              }
            }
          }
        }
      }
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      return Expr::make_call("range", std::move(args), std::move(call_type_arg));
    }
    if (name == "from_str") {
      ExprPtr s_expr = parse_expr(tokens, i);
      if (!s_expr) return nullptr;
      std::string call_type_arg;
      if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Comma) {
        i++;
        if (!at_eof(tokens, i)) {
          if (tokens[i].kind == TokenKind::KwI64) { call_type_arg = "i64"; i++; }
          else if (tokens[i].kind == TokenKind::KwF64) { call_type_arg = "f64"; i++; }
        }
      }
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
      i++;
      std::vector<ExprPtr> args;
      args.push_back(std::move(s_expr));
      return Expr::make_call("from_str", std::move(args), std::move(call_type_arg));
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

/* Postfix: primary followed by [ expr ]* (subscript). */
static ExprPtr parse_postfix(const std::vector<Token>& tokens, size_t& i, ExprPtr base) {
  while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::LBracket) {
    i++;
    ExprPtr index_expr = parse_expr(tokens, i);
    if (!index_expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::RBracket) return nullptr;
    i++;
    base = Expr::make_index(std::move(base), std::move(index_expr));
  }
  return base;
}

/* Multiplicative level: primary, postfix [expr]*, then Star/Slash*. */
static ExprPtr parse_multiplicative(const std::vector<Token>& tokens, size_t& i) {
  ExprPtr left = parse_primary(tokens, i);
  if (!left) return nullptr;
  left = parse_postfix(tokens, i, std::move(left));
  if (!left) return nullptr;

  while (!at_eof(tokens, i) && (tokens[i].kind == TokenKind::Star || tokens[i].kind == TokenKind::Slash)) {
    BinOp op = (tokens[i].kind == TokenKind::Star) ? BinOp::Mul : BinOp::Div;
    i++;
    ExprPtr right = parse_primary(tokens, i);
    if (!right) return nullptr;
    right = parse_postfix(tokens, i, std::move(right));
    if (!right) return nullptr;
    left = Expr::make_binop(op, std::move(left), std::move(right));
  }
  return left;
}

/* Additive level: multiplicative, (Plus|Minus)*, optional "as" cast. */
static ExprPtr parse_additive(const std::vector<Token>& tokens, size_t& i) {
  ExprPtr left = parse_multiplicative(tokens, i);
  if (!left) return nullptr;

  while (!at_eof(tokens, i) && (tokens[i].kind == TokenKind::Plus || tokens[i].kind == TokenKind::Minus)) {
    BinOp op = (tokens[i].kind == TokenKind::Plus) ? BinOp::Add : BinOp::Sub;
    i++;
    ExprPtr right = parse_multiplicative(tokens, i);
    if (!right) return nullptr;
    left = Expr::make_binop(op, std::move(left), std::move(right));
  }

  if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwAs) {
    i++;
    if (at_eof(tokens, i)) return nullptr;
    std::string type_name;
    if (tokens[i].kind == TokenKind::KwPtr) type_name = "ptr";
    else if (tokens[i].kind == TokenKind::KwI64) type_name = "i64";
    else if (tokens[i].kind == TokenKind::KwI32) type_name = "i32";
    else if (tokens[i].kind == TokenKind::KwF64) type_name = "f64";
    else if (tokens[i].kind == TokenKind::KwF32) type_name = "f32";
    else return nullptr;
    i++;
    left = Expr::make_cast(std::move(left), std::move(type_name));
  }
  return left;
}

static ExprPtr parse_expr(const std::vector<Token>& tokens, size_t& i) {
  ExprPtr left = parse_additive(tokens, i);
  if (!left) return nullptr;

  while (!at_eof(tokens, i) && is_comparison(tokens[i].kind)) {
    CompareOp op = token_to_compare_op(tokens[i].kind);
    i++;
    ExprPtr right = parse_additive(tokens, i);
    if (!right) return nullptr;
    left = Expr::make_compare(op, std::move(left), std::move(right));
  }
  return left;
}

/* Parses "fn name(params) -> ret;" starting at KwFn. Fills out; does not set lib_name. */
static bool parse_fn_decl(const std::vector<Token>& tokens, size_t& i, ExternFn& out) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwFn) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
  out.name = tokens[i].ident;
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
      out.params.push_back({std::move(pname), token_to_ffi_type(tokens[i].kind)});
      out.param_type_names.push_back("");
    } else if (tokens[i].kind == TokenKind::Ident) {
      out.params.push_back({std::move(pname), FfiType::Ptr});
      out.param_type_names.push_back(tokens[i].ident);
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
        out.params.push_back({std::move(pname2), token_to_ffi_type(tokens[i].kind)});
        out.param_type_names.push_back("");
      } else if (tokens[i].kind == TokenKind::Ident) {
        out.params.push_back({std::move(pname2), FfiType::Ptr});
        out.param_type_names.push_back(tokens[i].ident);
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
    out.return_type = token_to_ffi_type(tokens[i].kind);
    out.return_type_name.clear();
  } else if (tokens[i].kind == TokenKind::Ident) {
    out.return_type = FfiType::Ptr;
    out.return_type_name = tokens[i].ident;
  } else {
    return false;
  }
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return false;
  i++;
  return true;
}

/* Same as parse_fn_decl but fills FnDecl (for import lib block). */
static bool parse_fn_decl_fndecl(const std::vector<Token>& tokens, size_t& i, FnDecl& out) {
  ExternFn ext;
  if (!parse_fn_decl(tokens, i, ext)) return false;
  out.name = std::move(ext.name);
  out.params = std::move(ext.params);
  out.param_type_names = std::move(ext.param_type_names);
  out.return_type = ext.return_type;
  out.return_type_name = std::move(ext.return_type_name);
  return true;
}

/* Parses "import lib "name" { struct X; struct Y; fn foo(...) -> ret; }; " at top level. */
static bool parse_import_lib(const std::vector<Token>& tokens, size_t& i, Program& prog) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwImport) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwLib) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::StringLiteral) return false;
  ImportLib imp;
  imp.name = tokens[i].str_value;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LCurly) return false;
  i++;
  while (!at_eof(tokens, i) && tokens[i].kind != TokenKind::RCurly) {
    if (tokens[i].kind == TokenKind::KwStruct) {
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
      imp.struct_names.push_back(tokens[i].ident);
      i++;
      if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return false;
      i++;
    } else if (tokens[i].kind == TokenKind::KwFn) {
      FnDecl fdecl;
      if (!parse_fn_decl_fndecl(tokens, i, fdecl)) return false;
      imp.fn_decls.push_back(std::move(fdecl));
    } else {
      return false;
    }
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RCurly) return false;
  i++;
  if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Semicolon) i++;
  prog.import_libs.push_back(std::move(imp));
  return true;
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
  if (lib.name.empty())
    lib.name = "__lib" + std::to_string(prog.libs.size());
  if (at_eof(tokens, i)) return false;
  if (tokens[i].kind == TokenKind::LCurly) {
    prog.libs.push_back(lib);
    i++;
    while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwFn) {
      ExternFn ext;
      if (!parse_fn_decl(tokens, i, ext)) return false;
      ext.lib_name = lib.name;
      prog.extern_fns.push_back(std::move(ext));
    }
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RCurly) return false;
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return false;
    i++;
    return true;
  }
  if (tokens[i].kind != TokenKind::Semicolon) return false;
  i++;
  prog.libs.push_back(std::move(lib));
  return true;
}

static bool parse_extern_fn(const std::vector<Token>& tokens, size_t& i, Program& prog) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwExtern) return false;
  i++;
  ExternFn ext;
  if (!parse_fn_decl(tokens, i, ext)) return false;
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

/* Parses optional "elif ( expr ) { block }"* and optional "else { block }". Returns else_body for an if. */
static std::vector<StmtPtr> parse_elif_else_chain(const std::vector<Token>& tokens, size_t& i) {
  std::vector<StmtPtr> out;
  if (at_eof(tokens, i)) return out;
  if (tokens[i].kind == TokenKind::KwElif) {
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LParen) return out;
    i++;
    ExprPtr cond = parse_expr(tokens, i);
    if (!cond || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return out;
    i++;
    std::vector<StmtPtr> then_body;
    if (!parse_block(tokens, i, then_body)) return out;
    std::vector<StmtPtr> else_body = parse_elif_else_chain(tokens, i);
    out.push_back(Stmt::make_if(std::move(cond), std::move(then_body), std::move(else_body)));
    return out;
  }
  if (tokens[i].kind == TokenKind::KwElse) {
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LCurly) return out;
    if (!parse_block(tokens, i, out)) return out;
    return out;
  }
  return out;
}

static StmtPtr parse_stmt(const std::vector<Token>& tokens, size_t& i) {
  if (at_eof(tokens, i)) return nullptr;
  if (tokens[i].kind == TokenKind::KwReturn) {
    i++;
    ExprPtr expr = parse_expr(tokens, i);
    if (!expr || at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return nullptr;
    i++;
    return Stmt::make_return(std::move(expr));
  }
  if (tokens[i].kind == TokenKind::KwLet) {
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return nullptr;
    std::string name = tokens[i].ident;
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Equals) return nullptr;
    i++;
    ExprPtr init = parse_expr(tokens, i);
    if (!init || at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return nullptr;
    i++;
    return Stmt::make_let(std::move(name), std::move(init));
  }
  if (tokens[i].kind == TokenKind::KwIf) {
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LParen) return nullptr;
    i++;
    ExprPtr cond = parse_expr(tokens, i);
    if (!cond || at_eof(tokens, i) || tokens[i].kind != TokenKind::RParen) return nullptr;
    i++;
    std::vector<StmtPtr> then_body;
    if (!parse_block(tokens, i, then_body)) return nullptr;
    std::vector<StmtPtr> else_body = parse_elif_else_chain(tokens, i);
    return Stmt::make_if(std::move(cond), std::move(then_body), std::move(else_body));
  }
  if (tokens[i].kind == TokenKind::KwFor) {
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return nullptr;
    std::string loop_var = tokens[i].ident;
    i++;
    if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwIn) return nullptr;
    i++;
    ExprPtr iterable = parse_expr(tokens, i);
    if (!iterable || at_eof(tokens, i) || tokens[i].kind != TokenKind::LCurly) return nullptr;
    std::vector<StmtPtr> body;
    if (!parse_block(tokens, i, body)) return nullptr;
    return Stmt::make_for(std::move(loop_var), std::move(iterable), std::move(body));
  }
  ExprPtr expr = parse_expr(tokens, i);
  if (!expr) return nullptr;
  if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Equals) {
    if (expr->kind == Expr::Kind::VarRef || expr->kind == Expr::Kind::Index) {
      i++;
      ExprPtr value = parse_expr(tokens, i);
      if (!value || at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return nullptr;
      i++;
      return Stmt::make_assign(std::move(expr), std::move(value));
    }
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Semicolon) return nullptr;
  i++;
  return Stmt::make_expr(std::move(expr));
}

static bool parse_block(const std::vector<Token>& tokens, size_t& i, std::vector<StmtPtr>& out) {
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::LCurly) return false;
  i++;
  out.clear();
  while (!at_eof(tokens, i) && tokens[i].kind != TokenKind::RCurly) {
    StmtPtr s = parse_stmt(tokens, i);
    if (!s) return false;
    out.push_back(std::move(s));
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::RCurly) return false;
  i++;
  return true;
}

static bool parse_fn_def(const std::vector<Token>& tokens, size_t& i, Program& prog) {
  if (at_eof(tokens, i)) return false;
  bool exported = false;
  if (tokens[i].kind == TokenKind::KwExport) {
    exported = true;
    i++;
  }
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::KwFn) return false;
  i++;
  if (at_eof(tokens, i) || tokens[i].kind != TokenKind::Ident) return false;
  FnDef def;
  def.name = tokens[i].ident;
  def.exported = exported;
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
      def.params.push_back({std::move(pname), token_to_ffi_type(tokens[i].kind)});
      def.param_type_names.push_back("");
    } else if (tokens[i].kind == TokenKind::Ident) {
      def.params.push_back({std::move(pname), FfiType::Ptr});
      def.param_type_names.push_back(tokens[i].ident);
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
        def.params.push_back({std::move(pname2), token_to_ffi_type(tokens[i].kind)});
        def.param_type_names.push_back("");
      } else if (tokens[i].kind == TokenKind::Ident) {
        def.params.push_back({std::move(pname2), FfiType::Ptr});
        def.param_type_names.push_back(tokens[i].ident);
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
    def.return_type = token_to_ffi_type(tokens[i].kind);
    def.return_type_name.clear();
  } else if (tokens[i].kind == TokenKind::Ident) {
    def.return_type = FfiType::Ptr;
    def.return_type_name = tokens[i].ident;
  } else {
    return false;
  }
  i++;
  if (!parse_block(tokens, i, def.body)) return false;
  prog.user_fns.push_back(std::move(def));
  return true;
}

ParseResult parse(const std::vector<Token>& tokens) {
  size_t i = 0;
  auto prog = std::make_unique<Program>();

  while (!at_eof(tokens, i) && tokens[i].kind == TokenKind::KwImport) {
    if (!parse_import_lib(tokens, i, *prog)) {
      size_t line = 1, col = 1;
      if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
      return fail("invalid import lib", line, col);
    }
  }

  while (!at_eof(tokens, i) && (tokens[i].kind == TokenKind::KwOpaque || tokens[i].kind == TokenKind::KwStruct || tokens[i].kind == TokenKind::KwExport)) {
    if (tokens[i].kind == TokenKind::KwOpaque) {
      if (!parse_opaque_decl(tokens, i, *prog)) {
        size_t line = 1, col = 1;
        if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
        return fail("invalid opaque declaration", line, col);
      }
    } else {
      /* When we see "export fn", try parse_fn_def first so it consumes "export" and sets exported=true. */
      if (tokens[i].kind == TokenKind::KwExport && !at_eof(tokens, i + 1) && tokens[i + 1].kind == TokenKind::KwFn) {
        if (parse_fn_def(tokens, i, *prog)) continue;
      } else if (parse_struct_def(tokens, i, *prog)) {
        continue;
      } else if (parse_fn_def(tokens, i, *prog)) {
        continue;
      }
      size_t line = 1, col = 1;
      if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
      return fail("invalid struct or function definition", line, col);
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

  while (!at_eof(tokens, i) && (tokens[i].kind == TokenKind::KwFn || tokens[i].kind == TokenKind::KwExport)) {
    if (!parse_fn_def(tokens, i, *prog)) {
      size_t line = 1, col = 1;
      if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
      return fail("invalid function definition", line, col);
    }
  }

  /* Ordered list of let bindings, if/for statements, assignments, and expressions (any mix). */
  while (!at_eof(tokens, i) && tokens[i].kind != TokenKind::Eof) {
    if (tokens[i].kind == TokenKind::KwLet) {
      if (!parse_let_binding(tokens, i, *prog)) {
        size_t line = 1, col = 1;
        if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
        return fail("invalid let binding", line, col);
      }
      continue;
    }
    if (tokens[i].kind == TokenKind::KwIf || tokens[i].kind == TokenKind::KwFor) {
      StmtPtr stmt = parse_stmt(tokens, i);
      if (!stmt) {
        size_t line = 1, col = 1;
        if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
        return fail("invalid statement", line, col);
      }
      prog->top_level.push_back(std::move(stmt));
      if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Semicolon)
        i++;
      continue;
    }
    ExprPtr expr = parse_expr(tokens, i);
    if (!expr) {
      size_t line = 1, col = 1;
      if (i < tokens.size()) {
        line = tokens[i].line;
        col = tokens[i].column;
      }
      return fail("expected expression or let binding", line, col);
    }
    if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Equals &&
        (expr->kind == Expr::Kind::VarRef || expr->kind == Expr::Kind::Index)) {
      i++;
      ExprPtr value = parse_expr(tokens, i);
      if (!value) {
        size_t line = 1, col = 1;
        if (i < tokens.size()) { line = tokens[i].line; col = tokens[i].column; }
        return fail("invalid assignment", line, col);
      }
      prog->top_level.push_back(Stmt::make_assign(std::move(expr), std::move(value)));
    } else {
      prog->top_level.push_back(std::move(expr));
    }
    if (!at_eof(tokens, i) && tokens[i].kind == TokenKind::Semicolon) {
      i++;
    }
  }
  ParseResult r;
  r.program = std::move(prog);
  return r;
}

}  // namespace fusion
