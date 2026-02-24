#include "sema.hpp"

namespace fusion {

static bool check_expr(Expr* expr, SemaError& err) {
  if (!expr) return false;
  switch (expr->kind) {
    case Expr::Kind::IntLiteral:
      return true;
    case Expr::Kind::BinaryOp:
      if (!check_expr(expr->left.get(), err)) return false;
      if (!check_expr(expr->right.get(), err)) return false;
      return true;
    case Expr::Kind::Call:
      if (expr->callee == "print") {
        if (expr->args.size() != 1) {
          err.message = "print expects exactly one argument";
          return false;
        }
        if (!check_expr(expr->args[0].get(), err)) return false;
        return true;
      }
      err.message = "unknown function '" + expr->callee + "'";
      return false;
  }
  return false;
}

SemaResult check(Expr* expr) {
  SemaResult r;
  r.ok = check_expr(expr, r.error);
  return r;
}

}  // namespace fusion
