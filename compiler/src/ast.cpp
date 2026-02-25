#include "ast.hpp"

namespace fusion {

ExprPtr Expr::make_int(int64_t value) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::IntLiteral;
  e->int_value = value;
  return e;
}

ExprPtr Expr::make_float(double value) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::FloatLiteral;
  e->float_value = value;
  return e;
}

ExprPtr Expr::make_string(std::string value) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::StringLiteral;
  e->str_value = std::move(value);
  return e;
}

ExprPtr Expr::make_binop(BinOp op, ExprPtr left, ExprPtr right) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::BinaryOp;
  e->bin_op = op;
  e->left = std::move(left);
  e->right = std::move(right);
  return e;
}

ExprPtr Expr::make_call(std::string callee, std::vector<ExprPtr> args) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::Call;
  e->callee = std::move(callee);
  e->args = std::move(args);
  return e;
}

ExprPtr Expr::make_var_ref(std::string name) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::VarRef;
  e->var_name = std::move(name);
  return e;
}

}  // namespace fusion
