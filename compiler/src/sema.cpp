#include "ast.hpp"
#include "sema.hpp"
#include <unordered_map>
#include <unordered_set>

namespace fusion {

struct SemaContext {
  std::unordered_set<std::string> lib_names;
  std::unordered_map<std::string, ExternFn> extern_fn_by_name;
  std::unordered_map<std::string, FfiType> var_types;
  SemaError* err = nullptr;
};

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
        if (arg_ty != FfiType::I64 && arg_ty != FfiType::F64 && arg_ty != FfiType::Cstring) {
          ctx.err->message = "print expects i64, f64, or string argument";
          return false;
        }
        return true;
      }
      auto it = ctx.extern_fn_by_name.find(expr->callee);
      if (it == ctx.extern_fn_by_name.end()) {
        ctx.err->message = "unknown function '" + expr->callee + "'";
        return false;
      }
      const ExternFn& ext = it->second;
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
    case Expr::Kind::VarRef: {
      auto it = ctx.var_types.find(expr->var_name);
      if (it == ctx.var_types.end()) {
        ctx.err->message = "undefined variable '" + expr->var_name + "'";
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
      return FfiType::Cstring;
    case Expr::Kind::BinaryOp:
      return FfiType::I64;
    case Expr::Kind::Call: {
      if (expr->callee == "print") return FfiType::Void;
      if (ctx) {
        auto it = ctx->extern_fn_by_name.find(expr->callee);
        if (it != ctx->extern_fn_by_name.end()) return it->second.return_type;
      }
      return FfiType::Void;
    }
    case Expr::Kind::VarRef:
      if (ctx) {
        auto it = ctx->var_types.find(expr->var_name);
        if (it != ctx->var_types.end()) return it->second;
      }
      return FfiType::Void;
  }
  return FfiType::Void;
}

SemaResult check(Program* program) {
  SemaResult r;
  if (!program || !program->root_expr) {
    r.error.message = "no program or root expression";
    return r;
  }
  if (!program->extern_fns.empty() && program->libs.empty()) {
    r.error.message = "at least one extern lib required when declaring extern fn";
    return r;
  }
  SemaContext ctx;
  ctx.err = &r.error;
  for (const ExternFn& ext : program->extern_fns) {
    ctx.extern_fn_by_name[ext.name] = ext;
  }
  for (const LetBinding& binding : program->bindings) {
    if (!check_expr(binding.init.get(), ctx)) return r;
    FfiType ty = expr_type(binding.init.get(), &ctx);
    if (ctx.var_types.count(binding.name)) {
      ctx.err->message = "duplicate variable '" + binding.name + "'";
      return r;
    }
    ctx.var_types[binding.name] = ty;
  }
  r.ok = check_expr(program->root_expr.get(), ctx);
  return r;
}

}  // namespace fusion
