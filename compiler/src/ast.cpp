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

ExprPtr Expr::make_alloc(std::string type_name) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::Alloc;
  e->var_name = std::move(type_name);
  return e;
}

ExprPtr Expr::make_alloc_bytes(ExprPtr size_expr) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::AllocBytes;
  e->left = std::move(size_expr);
  return e;
}

ExprPtr Expr::make_addr_of(ExprPtr expr) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::AddrOf;
  e->left = std::move(expr);
  return e;
}

ExprPtr Expr::make_load(ExprPtr ptr) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::Load;
  e->left = std::move(ptr);
  return e;
}

ExprPtr Expr::make_load_f64(ExprPtr ptr) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::LoadF64;
  e->left = std::move(ptr);
  return e;
}

ExprPtr Expr::make_load_i32(ExprPtr ptr) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::LoadI32;
  e->left = std::move(ptr);
  return e;
}

ExprPtr Expr::make_load_ptr(ExprPtr ptr) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::LoadPtr;
  e->left = std::move(ptr);
  return e;
}

ExprPtr Expr::make_store(ExprPtr ptr, ExprPtr value) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::Store;
  e->left = std::move(ptr);
  e->right = std::move(value);
  return e;
}

ExprPtr Expr::make_load_field(ExprPtr ptr, std::string struct_name, std::string field_name) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::LoadField;
  e->left = std::move(ptr);
  e->load_field_struct = std::move(struct_name);
  e->load_field_field = std::move(field_name);
  return e;
}

ExprPtr Expr::make_store_field(ExprPtr ptr, std::string struct_name, std::string field_name, ExprPtr value) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::StoreField;
  e->left = std::move(ptr);
  e->right = std::move(value);
  e->load_field_struct = std::move(struct_name);
  e->load_field_field = std::move(field_name);
  return e;
}

ExprPtr Expr::make_cast(ExprPtr operand, std::string target_type_name) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::Cast;
  e->left = std::move(operand);
  e->var_name = std::move(target_type_name);
  return e;
}

StmtPtr Stmt::make_return(ExprPtr expr) {
  auto s = std::make_unique<Stmt>();
  s->kind = Kind::Return;
  s->expr = std::move(expr);
  return s;
}

StmtPtr Stmt::make_let(std::string name, ExprPtr init) {
  auto s = std::make_unique<Stmt>();
  s->kind = Kind::Let;
  s->name = std::move(name);
  s->init = std::move(init);
  return s;
}

StmtPtr Stmt::make_expr(ExprPtr expr) {
  auto s = std::make_unique<Stmt>();
  s->kind = Kind::Expr;
  s->expr = std::move(expr);
  return s;
}

}  // namespace fusion
