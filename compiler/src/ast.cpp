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

ExprPtr Expr::make_call(std::string callee, std::vector<ExprPtr> args, std::string call_type_arg) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::Call;
  e->callee = std::move(callee);
  e->args = std::move(args);
  e->call_type_arg = std::move(call_type_arg);
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

ExprPtr Expr::make_alloc_array(std::string element_type, ExprPtr count_expr) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::AllocArray;
  e->var_name = std::move(element_type);
  e->left = std::move(count_expr);
  return e;
}

ExprPtr Expr::make_index(ExprPtr base, ExprPtr index_expr) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::Index;
  e->left = std::move(base);
  e->right = std::move(index_expr);
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

ExprPtr Expr::make_compare(CompareOp op, ExprPtr left, ExprPtr right) {
  auto e = std::make_unique<Expr>();
  e->kind = Kind::Compare;
  e->compare_op = op;
  e->left = std::move(left);
  e->right = std::move(right);
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

StmtPtr Stmt::make_if(ExprPtr cond, std::vector<StmtPtr> then_body, std::vector<StmtPtr> else_body) {
  auto s = std::make_unique<Stmt>();
  s->kind = Kind::If;
  s->cond = std::move(cond);
  s->then_body = std::move(then_body);
  s->else_body = std::move(else_body);
  return s;
}

StmtPtr Stmt::make_for(std::string loop_var, ExprPtr iterable, std::vector<StmtPtr> body) {
  auto s = std::make_unique<Stmt>();
  s->kind = Kind::For;
  s->name = std::move(loop_var);
  s->iterable = std::move(iterable);
  s->body = std::move(body);
  return s;
}

StmtPtr Stmt::make_assign(ExprPtr target, ExprPtr value) {
  auto s = std::make_unique<Stmt>();
  s->kind = Kind::Assign;
  s->expr = std::move(target);
  s->init = std::move(value);
  return s;
}

ExprPtr Expr::clone() const {
  auto e = std::make_unique<Expr>();
  e->kind = kind;
  e->int_value = int_value;
  e->float_value = float_value;
  e->str_value = str_value;
  e->bin_op = bin_op;
  e->compare_op = compare_op;
  e->callee = callee;
  e->call_type_arg = call_type_arg;
  e->var_name = var_name;
  e->load_field_struct = load_field_struct;
  e->load_field_field = load_field_field;
  e->inferred_call_param_types = inferred_call_param_types;
  e->inferred_call_result_type = inferred_call_result_type;
  if (left) e->left = left->clone();
  if (right) e->right = right->clone();
  for (const auto& a : args) e->args.push_back(a ? a->clone() : nullptr);
  return e;
}

StmtPtr Stmt::clone() const {
  auto s = std::make_unique<Stmt>();
  s->kind = kind;
  s->name = name;
  if (expr) s->expr = expr->clone();
  if (init) s->init = init->clone();
  if (cond) s->cond = cond->clone();
  if (iterable) s->iterable = iterable->clone();
  for (const auto& t : then_body) s->then_body.push_back(t ? t->clone() : nullptr);
  for (const auto& t : else_body) s->else_body.push_back(t ? t->clone() : nullptr);
  for (const auto& b : body) s->body.push_back(b ? b->clone() : nullptr);
  return s;
}

FnDef FnDef::clone() const {
  FnDef c;
  c.name = name;
  c.params = params;
  c.param_type_names = param_type_names;
  c.return_type = return_type;
  c.return_type_name = return_type_name;
  c.exported = exported;
  for (const auto& b : body) c.body.push_back(b ? b->clone() : nullptr);
  return c;
}

}  // namespace fusion
