#include "ast.hpp"
#include "layout.hpp"
#include "sema.hpp"
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace fusion {

struct SemaContext {
  std::unordered_set<std::string> lib_names;
  std::unordered_map<std::string, ExternFn> extern_fn_by_name;
  std::unordered_map<std::string, FnDef*> user_fn_by_name;
  std::unordered_map<std::string, FfiType> var_types;
  std::unordered_map<std::string, FfiType> array_element_by_var;  // var name -> element type when value is array
  LayoutMap* layout_map = nullptr;  // from Program::struct_defs
  Program* program = nullptr;
  SemaError* err = nullptr;
};

static bool is_alloc_type(const std::string& name, Program* program) {
  if (name == "i32" || name == "i64" || name == "f32" || name == "f64" || name == "ptr" || name == "cstring")
    return true;
  if (program)
    for (const auto& s : program->struct_defs)
      if (s.name == name) return true;
  return false;
}

static FfiType expr_type(Expr* expr, SemaContext* ctx);  // returns type of expression for type-checking

/* Returns element type if expr is an array (ptr from alloc_array/range or VarRef to such); otherwise FfiType::Void. */
static FfiType get_array_element_type(Expr* expr, SemaContext* ctx) {
  if (!expr || !ctx) return FfiType::Void;
  if (expr->kind == Expr::Kind::VarRef) {
    auto it = ctx->array_element_by_var.find(expr->var_name);
    if (it != ctx->array_element_by_var.end()) return it->second;
    return FfiType::Void;
  }
  if (expr->kind == Expr::Kind::Call && expr->callee == "range") {
    if (!expr->call_type_arg.empty()) {
      if (expr->call_type_arg == "i32") return FfiType::I32;
      if (expr->call_type_arg == "i64") return FfiType::I64;
      if (expr->call_type_arg == "f32") return FfiType::F32;
      if (expr->call_type_arg == "f64") return FfiType::F64;
    }
    return FfiType::I64;
  }
  if (expr->kind == Expr::Kind::AllocArray) {
    const std::string& t = expr->var_name;
    if (t == "i32") return FfiType::I32;
    if (t == "i64") return FfiType::I64;
    if (t == "f32") return FfiType::F32;
    if (t == "f64") return FfiType::F64;
    if (t == "ptr" || t == "cstring") return FfiType::Ptr;
    return FfiType::Void;
  }
  return FfiType::Void;
}

static bool check_expr(Expr* expr, SemaContext& ctx) {
  if (!expr) return false;
  switch (expr->kind) {
    case Expr::Kind::IntLiteral:
      return true;
    case Expr::Kind::FloatLiteral:
    case Expr::Kind::StringLiteral:
      return true;
    case Expr::Kind::BinaryOp:
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (!check_expr(expr->right.get(), ctx)) return false;
      return true;
    case Expr::Kind::Call: {
      if (expr->callee == "print") {
        if (expr->args.size() != 1 && expr->args.size() != 2) {
          ctx.err->message = "print expects 1 or 2 arguments";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        FfiType arg_ty = expr_type(expr->args[0].get(), &ctx);
        if (arg_ty != FfiType::I64 && arg_ty != FfiType::F64 && arg_ty != FfiType::Ptr) {
          ctx.err->message = "print expects i64, f64, or pointer argument";
          return false;
        }
        if (expr->args.size() == 2) {
          if (!check_expr(expr->args[1].get(), ctx)) return false;
          if (expr_type(expr->args[1].get(), &ctx) != FfiType::I64) {
            ctx.err->message = "print stream argument must be i64";
            return false;
          }
        }
        return true;
      }
      if (expr->callee == "read_line") {
        if (expr->args.size() != 0) {
          ctx.err->message = "read_line expects no arguments";
          return false;
        }
        return true;
      }
      if (expr->callee == "to_str") {
        if (expr->args.size() != 1) {
          ctx.err->message = "to_str expects exactly one argument";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        FfiType t = expr_type(expr->args[0].get(), &ctx);
        if (t != FfiType::I64 && t != FfiType::F64) {
          ctx.err->message = "to_str expects i64 or f64 argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "from_str") {
        if (expr->args.size() != 1) {
          ctx.err->message = "from_str expects one argument (string)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "from_str expects pointer (string) argument";
          return false;
        }
        if (expr->call_type_arg != "i64" && expr->call_type_arg != "f64") {
          ctx.err->message = "from_str requires type argument: use from_str(s, i64) or from_str(s, f64)";
          return false;
        }
        return true;
      }
      if (expr->callee == "open") {
        if (expr->args.size() != 2) {
          ctx.err->message = "open expects (path, mode)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx) || !check_expr(expr->args[1].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr || expr_type(expr->args[1].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "open expects two pointer (string) arguments";
          return false;
        }
        return true;
      }
      if (expr->callee == "close") {
        if (expr->args.size() != 1) {
          ctx.err->message = "close expects one argument (file handle)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "close expects pointer argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "read_line_file") {
        if (expr->args.size() != 1) {
          ctx.err->message = "read_line_file expects one argument (file handle)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "read_line_file expects pointer argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "write_file") {
        if (expr->args.size() != 2) {
          ctx.err->message = "write_file expects (handle, value)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx) || !check_expr(expr->args[1].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "write_file first argument must be pointer (file handle)";
          return false;
        }
        FfiType val_ty = expr_type(expr->args[1].get(), &ctx);
        if (val_ty != FfiType::I64 && val_ty != FfiType::F64 && val_ty != FfiType::Ptr) {
          ctx.err->message = "write_file second argument must be i64, f64, or ptr";
          return false;
        }
        return true;
      }
      if (expr->callee == "eof_file") {
        if (expr->args.size() != 1) {
          ctx.err->message = "eof_file expects one argument (file handle)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "eof_file expects pointer argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "line_count_file") {
        if (expr->args.size() != 1) {
          ctx.err->message = "line_count_file expects one argument (file handle)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "line_count_file expects pointer argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "range") {
        if (expr->args.size() != 1 && expr->args.size() != 2) {
          ctx.err->message = "range expects 1 or 2 arguments";
          return false;
        }
        for (size_t j = 0; j < expr->args.size(); ++j) {
          if (!check_expr(expr->args[j].get(), ctx)) return false;
          if (expr_type(expr->args[j].get(), &ctx) != FfiType::I64) {
            ctx.err->message = "range arguments must be i64";
            return false;
          }
        }
        return true;
      }
      auto ext_it = ctx.extern_fn_by_name.find(expr->callee);
      if (ext_it != ctx.extern_fn_by_name.end()) {
        const ExternFn& ext = ext_it->second;
        if (expr->args.size() != ext.params.size()) {
          ctx.err->message = "call to '" + expr->callee + "' has wrong number of arguments";
          return false;
        }
        for (size_t j = 0; j < expr->args.size(); ++j) {
          if (!check_expr(expr->args[j].get(), ctx)) return false;
          FfiType arg_ty = expr_type(expr->args[j].get(), &ctx);
          if (arg_ty != ext.params[j].second) {
            ctx.err->message = "argument type mismatch in call to '" + expr->callee + "'";
            return false;
          }
        }
        return true;
      }
      auto user_it = ctx.user_fn_by_name.find(expr->callee);
      if (user_it != ctx.user_fn_by_name.end()) {
        const FnDef& def = *user_it->second;
        if (expr->args.size() != def.params.size()) {
          ctx.err->message = "call to '" + expr->callee + "' has wrong number of arguments";
          return false;
        }
        for (size_t j = 0; j < expr->args.size(); ++j) {
          if (!check_expr(expr->args[j].get(), ctx)) return false;
          FfiType arg_ty = expr_type(expr->args[j].get(), &ctx);
          if (arg_ty != def.params[j].second) {
            ctx.err->message = "argument type mismatch in call to '" + expr->callee + "'";
            return false;
          }
        }
        return true;
      }
      ctx.err->message = "unknown function '" + expr->callee + "'";
      return false;
    }
    case Expr::Kind::VarRef: {
      auto it = ctx.var_types.find(expr->var_name);
      if (it == ctx.var_types.end()) {
        ctx.err->message = "undefined variable '" + expr->var_name + "'";
        return false;
      }
      return true;
    }
    case Expr::Kind::Alloc:
      if (!is_alloc_type(expr->var_name, ctx.program)) {
        ctx.err->message = "alloc: unknown type '" + expr->var_name + "'";
        return false;
      }
      return true;
    case Expr::Kind::AllocArray:
      if (!expr->left) return false;
      if (!is_alloc_type(expr->var_name, ctx.program)) {
        ctx.err->message = "alloc_array: unknown element type '" + expr->var_name + "'";
        return false;
      }
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::I64) {
        ctx.err->message = "alloc_array: count must be i64";
        return false;
      }
      return true;
    case Expr::Kind::Index:
      if (!expr->left || !expr->right) return false;
      if (!check_expr(expr->left.get(), ctx) || !check_expr(expr->right.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "index: base must be a pointer (array)";
        return false;
      }
      if (expr_type(expr->right.get(), &ctx) != FfiType::I64) {
        ctx.err->message = "index: index must be i64";
        return false;
      }
      return true;
    case Expr::Kind::AllocBytes:
      if (!expr->left) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::I64) {
        ctx.err->message = "alloc_bytes: size must be i64";
        return false;
      }
      return true;
    case Expr::Kind::AddrOf:
      if (!expr->left || expr->left->kind != Expr::Kind::VarRef) {
        ctx.err->message = "addr_of: argument must be a variable";
        return false;
      }
      return check_expr(expr->left.get(), ctx);
    case Expr::Kind::Load:
    case Expr::Kind::LoadF64:
    case Expr::Kind::LoadI32:
    case Expr::Kind::LoadPtr:
      if (!expr->left) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "load/load_f64/load_ptr: argument must be a pointer";
        return false;
      }
      return true;
    case Expr::Kind::Store:
      if (!expr->left || !expr->right) return false;
      if (!check_expr(expr->left.get(), ctx) || !check_expr(expr->right.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "store: first argument must be a pointer";
        return false;
      }
      return true;
    case Expr::Kind::LoadField: {
      if (!expr->left) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "load_field: first argument must be a pointer";
        return false;
      }
      if (!ctx.layout_map) return false;
      auto it = ctx.layout_map->find(expr->load_field_struct);
      if (it == ctx.layout_map->end()) {
        ctx.err->message = "load_field: unknown struct '" + expr->load_field_struct + "'";
        return false;
      }
      for (const auto& f : it->second.fields)
        if (f.first == expr->load_field_field) return true;
      ctx.err->message = "load_field: unknown field '" + expr->load_field_field + "' in struct '" + expr->load_field_struct + "'";
      return false;
    }
    case Expr::Kind::StoreField: {
      if (!expr->left || !expr->right) return false;
      if (!check_expr(expr->left.get(), ctx) || !check_expr(expr->right.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "store_field: first argument must be a pointer";
        return false;
      }
      if (!ctx.layout_map) return false;
      auto it = ctx.layout_map->find(expr->load_field_struct);
      if (it == ctx.layout_map->end()) {
        ctx.err->message = "store_field: unknown struct '" + expr->load_field_struct + "'";
        return false;
      }
      FfiType field_ty = FfiType::Void;
      for (const auto& f : it->second.fields)
        if (f.first == expr->load_field_field) { field_ty = f.second.type; break; }
      if (field_ty == FfiType::Void) {
        ctx.err->message = "store_field: unknown field '" + expr->load_field_field + "' in struct '" + expr->load_field_struct + "'";
        return false;
      }
      FfiType val_ty = expr_type(expr->right.get(), &ctx);
      bool compat = (val_ty == field_ty) ||
        (val_ty == FfiType::Ptr && field_ty == FfiType::I64) ||
        (val_ty == FfiType::I64 && field_ty == FfiType::Ptr);
      if (!compat) {
        ctx.err->message = "store_field: value type does not match field type";
        return false;
      }
      return true;
    }
    case Expr::Kind::Cast: {
      if (!expr->left || expr->var_name.empty()) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      FfiType from = expr_type(expr->left.get(), &ctx);
      if (expr->var_name == "ptr" || expr->var_name == "cstring") {
        if (from != FfiType::Ptr) {
          ctx.err->message = "cast to ptr/cstring: operand must be a pointer";
          return false;
        }
        return true;
      }
      if (expr->var_name == "i64" || expr->var_name == "i32" || expr->var_name == "f64" || expr->var_name == "f32") {
        bool from_numeric = (from == FfiType::I64 || from == FfiType::I32 || from == FfiType::F64 || from == FfiType::F32);
        if (!from_numeric) {
          ctx.err->message = "cast to numeric type: operand must be i64, i32, f64, or f32";
          return false;
        }
        return true;
      }
      ctx.err->message = "cast: target type must be ptr, cstring, i64, i32, f64, or f32";
      return false;
    }
    case Expr::Kind::Compare: {
      if (!expr->left || !expr->right) return false;
      if (!check_expr(expr->left.get(), ctx) || !check_expr(expr->right.get(), ctx)) return false;
      FfiType l = expr_type(expr->left.get(), &ctx);
      FfiType r = expr_type(expr->right.get(), &ctx);
      if (l == FfiType::Ptr && r == FfiType::Ptr) {
        if (expr->compare_op != CompareOp::Eq && expr->compare_op != CompareOp::Ne) {
          ctx.err->message = "pointer comparison only supports == and !=";
          return false;
        }
        return true;
      }
      bool numeric = (l == FfiType::I64 || l == FfiType::F64) && (r == FfiType::I64 || r == FfiType::F64);
      if (!numeric) {
        ctx.err->message = "comparison operands must be numeric (i64 or f64)";
        return false;
      }
      return true;
    }
  }
  return false;
}

static FfiType expr_type(Expr* expr, SemaContext* ctx) {
  if (!expr) return FfiType::Void;
  switch (expr->kind) {
    case Expr::Kind::IntLiteral:
      return FfiType::I64;
    case Expr::Kind::FloatLiteral:
      return FfiType::F64;
    case Expr::Kind::StringLiteral:
      return FfiType::Ptr;
    case Expr::Kind::BinaryOp: {
      FfiType l = expr_type(expr->left.get(), ctx);
      FfiType r = expr_type(expr->right.get(), ctx);
      return (l == FfiType::F64 || r == FfiType::F64) ? FfiType::F64 : FfiType::I64;
    }
    case Expr::Kind::Call: {
      if (expr->callee == "print") return FfiType::Void;
      if (expr->callee == "range") return FfiType::Ptr;
      if (expr->callee == "read_line" || expr->callee == "read_line_file") return FfiType::Ptr;
      if (expr->callee == "to_str") return FfiType::Ptr;
      if (expr->callee == "from_str") {
        if (expr->call_type_arg == "i64") return FfiType::I64;
        if (expr->call_type_arg == "f64") return FfiType::F64;
        return FfiType::Void;
      }
      if (expr->callee == "open") return FfiType::Ptr;
      if (expr->callee == "close") return FfiType::Void;
      if (expr->callee == "write_file") return FfiType::Void;
      if (expr->callee == "eof_file" || expr->callee == "line_count_file") return FfiType::I64;
      if (ctx) {
        auto ext_it = ctx->extern_fn_by_name.find(expr->callee);
        if (ext_it != ctx->extern_fn_by_name.end()) return ext_it->second.return_type;
        auto user_it = ctx->user_fn_by_name.find(expr->callee);
        if (user_it != ctx->user_fn_by_name.end()) return user_it->second->return_type;
      }
      return FfiType::Void;
    }
    case Expr::Kind::VarRef:
      if (ctx) {
        auto it = ctx->var_types.find(expr->var_name);
        if (it != ctx->var_types.end()) return it->second;
      }
      return FfiType::Void;
    case Expr::Kind::Alloc:
    case Expr::Kind::AllocArray:
    case Expr::Kind::AllocBytes:
      return FfiType::Ptr;
    case Expr::Kind::AddrOf:
      return FfiType::Ptr;
    case Expr::Kind::Load:
    case Expr::Kind::LoadI32:
      return FfiType::I64;
    case Expr::Kind::LoadF64:
      return FfiType::F64;
    case Expr::Kind::LoadPtr:
      return FfiType::Ptr;
    case Expr::Kind::Store:
    case Expr::Kind::StoreField:
      return FfiType::Void;
    case Expr::Kind::LoadField: {
      if (!ctx || !ctx->layout_map) return FfiType::Void;
      auto it = ctx->layout_map->find(expr->load_field_struct);
      if (it == ctx->layout_map->end()) return FfiType::Void;
      for (const auto& f : it->second.fields)
        if (f.first == expr->load_field_field) return f.second.type;
      return FfiType::Void;
    }
    case Expr::Kind::Cast:
      if (expr->var_name == "ptr" || expr->var_name == "cstring") return FfiType::Ptr;
      if (expr->var_name == "i64") return FfiType::I64;
      if (expr->var_name == "i32") return FfiType::I32;
      if (expr->var_name == "f64") return FfiType::F64;
      if (expr->var_name == "f32") return FfiType::F32;
      return FfiType::Void;
    case Expr::Kind::Compare:
      return FfiType::I64;  /* condition type as i64 0/1 for codegen */
    case Expr::Kind::Index: {
      FfiType elem = get_array_element_type(expr->left.get(), ctx);
      return (elem != FfiType::Void) ? elem : FfiType::I64;
    }
  }
  return FfiType::Void;
}

static bool is_named_type_known(const std::string& name, Program* program) {
  for (const std::string& o : program->opaque_types)
    if (o == name) return true;
  for (const auto& s : program->struct_defs)
    if (s.name == name) return true;
  return false;
}

static bool check_stmt(SemaContext& ctx, FnDef* def, Stmt* stmt);

static bool check_stmt(SemaContext& ctx, FnDef* def, Stmt* stmt) {
  if (!stmt) return false;
  switch (stmt->kind) {
    case Stmt::Kind::Return:
      if (!def) {
        ctx.err->message = "return only allowed inside a function";
        return false;
      }
      if (!check_expr(stmt->expr.get(), ctx)) return false;
      if (expr_type(stmt->expr.get(), &ctx) != def->return_type) {
        ctx.err->message = "return type does not match function return type in '" + def->name + "'";
        return false;
      }
      return true;
    case Stmt::Kind::Let: {
      if (!check_expr(stmt->init.get(), ctx)) return false;
      if (ctx.var_types.count(stmt->name)) {
        ctx.err->message = def
          ? "duplicate variable '" + stmt->name + "' in function '" + def->name + "'"
          : "duplicate variable '" + stmt->name + "'";
        return false;
      }
      FfiType let_ty = expr_type(stmt->init.get(), &ctx);
      ctx.var_types[stmt->name] = let_ty;
      FfiType elem_ty = get_array_element_type(stmt->init.get(), &ctx);
      if (elem_ty != FfiType::Void) {
        ctx.array_element_by_var[stmt->name] = elem_ty;
      } else if (let_ty == FfiType::Ptr && stmt->init->kind == Expr::Kind::LoadField) {
        Expr* e = stmt->init.get();
        auto it = ctx.layout_map->find(e->load_field_struct);
        if (it != ctx.layout_map->end()) {
          for (const auto& f : it->second.fields)
            if (f.first == e->load_field_field && f.second.type == FfiType::Ptr) {
              ctx.array_element_by_var[stmt->name] = FfiType::Ptr;
              break;
            }
        }
      }
      return true;
    }
    case Stmt::Kind::Expr:
      return check_expr(stmt->expr.get(), ctx);
    case Stmt::Kind::If:
      if (!check_expr(stmt->cond.get(), ctx)) return false;
      for (StmtPtr& s : stmt->then_body)
        if (!check_stmt(ctx, def, s.get())) return false;
      for (StmtPtr& s : stmt->else_body)
        if (!check_stmt(ctx, def, s.get())) return false;
      return true;
    case Stmt::Kind::For: {
      if (!stmt->iterable || !check_expr(stmt->iterable.get(), ctx)) return false;
      FfiType elem_ty = get_array_element_type(stmt->iterable.get(), &ctx);
      if (elem_ty == FfiType::Void) {
        ctx.err->message = "for-in requires an array (e.g. range(n) or alloc_array)";
        return false;
      }
      if (ctx.var_types.count(stmt->name)) {
        ctx.err->message = def
          ? "duplicate variable '" + stmt->name + "' in function '" + def->name + "'"
          : "duplicate variable '" + stmt->name + "'";
        return false;
      }
      ctx.var_types[stmt->name] = elem_ty;
      ctx.array_element_by_var[stmt->name] = elem_ty;  // loop var is not an array, but keep consistent
      for (StmtPtr& s : stmt->body)
        if (!check_stmt(ctx, def, s.get())) return false;
      ctx.var_types.erase(stmt->name);
      ctx.array_element_by_var.erase(stmt->name);
      return true;
    }
    case Stmt::Kind::Assign: {
      if (!stmt->expr || !stmt->init) return false;
      if (!check_expr(stmt->expr.get(), ctx) || !check_expr(stmt->init.get(), ctx)) return false;
      if (stmt->expr->kind == Expr::Kind::VarRef) {
        FfiType var_ty = expr_type(stmt->expr.get(), &ctx);
        FfiType val_ty = expr_type(stmt->init.get(), &ctx);
        bool compat = (var_ty == val_ty) ||
          (var_ty == FfiType::Ptr && val_ty == FfiType::I64) ||
          (var_ty == FfiType::I64 && val_ty == FfiType::Ptr);
        if (!compat) {
          ctx.err->message = "assignment type mismatch";
          return false;
        }
        return true;
      }
      if (stmt->expr->kind == Expr::Kind::Index) {
        FfiType elem_ty = get_array_element_type(stmt->expr->left.get(), &ctx);
        if (elem_ty == FfiType::Void) elem_ty = FfiType::I64;
        FfiType val_ty = expr_type(stmt->init.get(), &ctx);
        bool compat = (elem_ty == val_ty) ||
          (elem_ty == FfiType::Ptr && val_ty == FfiType::I64) ||
          (elem_ty == FfiType::I64 && val_ty == FfiType::Ptr);
        if (!compat) {
          ctx.err->message = "assignment type mismatch for array element";
          return false;
        }
        return true;
      }
      ctx.err->message = "assignment target must be a variable or index";
      return false;
    }
  }
  return false;
}

static bool check_fn_def(SemaContext& ctx, FnDef& def) {
  std::unordered_map<std::string, FfiType> local;
  for (size_t j = 0; j < def.params.size(); ++j)
    local[def.params[j].first] = def.params[j].second;
  SemaContext fn_ctx;
  fn_ctx.err = ctx.err;
  fn_ctx.layout_map = ctx.layout_map;
  fn_ctx.program = ctx.program;
  fn_ctx.extern_fn_by_name = ctx.extern_fn_by_name;
  fn_ctx.user_fn_by_name = ctx.user_fn_by_name;
  fn_ctx.var_types = local;
  for (const auto& p : def.params)
    if (p.second == FfiType::Ptr)
      fn_ctx.array_element_by_var[p.first] = FfiType::Ptr;
  for (StmtPtr& stmt : def.body) {
    if (!check_stmt(fn_ctx, &def, stmt.get())) return false;
  }
  return true;
}

SemaResult check(Program* program) {
  SemaResult r;
  if (!program || program->top_level.empty()) {
    r.error.message = "no program or no statements";
    return r;
  }
  if (!program->extern_fns.empty() && program->libs.empty()) {
    r.error.message = "at least one extern lib required when declaring extern fn";
    return r;
  }
  std::unordered_set<std::string> lib_names;
  for (const ExternLib& lib : program->libs)
    lib_names.insert(lib.name);
  for (const ExternFn& ext : program->extern_fns) {
    if (lib_names.find(ext.lib_name) == lib_names.end()) {
      r.error.message = "extern fn '" + ext.name + "' references unknown lib '" + ext.lib_name + "'";
      return r;
    }
    bool param_names_ok = (ext.param_type_names.size() == ext.params.size());
    if (param_names_ok) {
      for (size_t j = 0; j < ext.param_type_names.size(); ++j) {
        if (!ext.param_type_names[j].empty() && !is_named_type_known(ext.param_type_names[j], program)) {
          r.error.message = "unknown type '" + ext.param_type_names[j] + "' in extern fn '" + ext.name + "'";
          return r;
        }
      }
    }
    if (!ext.return_type_name.empty() && !is_named_type_known(ext.return_type_name, program)) {
      r.error.message = "unknown return type '" + ext.return_type_name + "' in extern fn '" + ext.name + "'";
      return r;
    }
  }
  LayoutMap layout_map = build_layout_map(program->struct_defs);
  SemaContext ctx;
  ctx.err = &r.error;
  ctx.layout_map = &layout_map;
  ctx.program = program;
  for (const ExternFn& ext : program->extern_fns) {
    ctx.extern_fn_by_name[ext.name] = ext;
  }
  for (FnDef& def : program->user_fns) {
    if (ctx.extern_fn_by_name.count(def.name)) {
      r.error.message = "function '" + def.name + "' conflicts with extern function";
      return r;
    }
    if (ctx.user_fn_by_name.count(def.name)) {
      r.error.message = "duplicate function definition '" + def.name + "'";
      return r;
    }
    ctx.user_fn_by_name[def.name] = &def;
  }
  for (FnDef& def : program->user_fns) {
    if (!check_fn_def(ctx, def)) return r;
  }
  for (const TopLevelItem& item : program->top_level) {
    if (const LetBinding* binding = std::get_if<LetBinding>(&item)) {
      if (!check_expr(binding->init.get(), ctx)) return r;
      FfiType ty = expr_type(binding->init.get(), &ctx);
      if (ctx.var_types.count(binding->name)) {
        ctx.err->message = "duplicate variable '" + binding->name + "'";
        return r;
      }
      ctx.var_types[binding->name] = ty;
      FfiType elem_ty = get_array_element_type(binding->init.get(), &ctx);
      if (elem_ty != FfiType::Void)
        ctx.array_element_by_var[binding->name] = elem_ty;
    } else if (const ExprPtr* expr = std::get_if<ExprPtr>(&item)) {
      if (!check_expr(expr->get(), ctx)) return r;
    } else {
      const StmtPtr& stmt = std::get<StmtPtr>(item);
      if (!check_stmt(ctx, nullptr, stmt.get())) return r;
    }
  }
  r.ok = true;
  return r;
}

}  // namespace fusion
