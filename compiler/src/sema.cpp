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
        if (expr->args.size() != 1) {
          ctx.err->message = "print expects exactly one argument";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        FfiType arg_ty = expr_type(expr->args[0].get(), &ctx);
        if (arg_ty != FfiType::I64 && arg_ty != FfiType::F64 && arg_ty != FfiType::Ptr) {
          ctx.err->message = "print expects i64, f64, or pointer argument";
          return false;
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
      ctx.err->message = "cast: target type must be ptr or cstring";
      return false;
    }
    case Expr::Kind::Compare: {
      if (!expr->left || !expr->right) return false;
      if (!check_expr(expr->left.get(), ctx) || !check_expr(expr->right.get(), ctx)) return false;
      FfiType l = expr_type(expr->left.get(), &ctx);
      FfiType r = expr_type(expr->right.get(), &ctx);
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
      return FfiType::Void;
    case Expr::Kind::Compare:
      return FfiType::I64;  /* condition type as i64 0/1 for codegen */
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

static bool top_level_has_expr(const Program* program) {
  if (!program) return false;
  for (const auto& item : program->top_level)
    if (std::holds_alternative<ExprPtr>(item)) return true;
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
    case Stmt::Kind::Let:
      if (!check_expr(stmt->init.get(), ctx)) return false;
      if (ctx.var_types.count(stmt->name)) {
        ctx.err->message = def
          ? "duplicate variable '" + stmt->name + "' in function '" + def->name + "'"
          : "duplicate variable '" + stmt->name + "'";
        return false;
      }
      ctx.var_types[stmt->name] = expr_type(stmt->init.get(), &ctx);
      return true;
    case Stmt::Kind::Expr:
      return check_expr(stmt->expr.get(), ctx);
    case Stmt::Kind::If:
      if (!check_expr(stmt->cond.get(), ctx)) return false;
      for (StmtPtr& s : stmt->then_body)
        if (!check_stmt(ctx, def, s.get())) return false;
      for (StmtPtr& s : stmt->else_body)
        if (!check_stmt(ctx, def, s.get())) return false;
      return true;
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
  for (StmtPtr& stmt : def.body) {
    if (!check_stmt(fn_ctx, &def, stmt.get())) return false;
  }
  return true;
}

SemaResult check(Program* program) {
  SemaResult r;
  if (!program || program->top_level.empty() || !top_level_has_expr(program)) {
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
