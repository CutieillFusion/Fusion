#ifndef FUSION_AST_HPP
#define FUSION_AST_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fusion {

enum class BinOp { Add };

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  enum class Kind { IntLiteral, BinaryOp, Call };
  Kind kind = Kind::IntLiteral;

  int64_t int_value = 0;
  BinOp bin_op = BinOp::Add;
  ExprPtr left;
  ExprPtr right;
  std::string callee;
  std::vector<ExprPtr> args;

  static ExprPtr make_int(int64_t value);
  static ExprPtr make_binop(BinOp op, ExprPtr left, ExprPtr right);
  static ExprPtr make_call(std::string callee, std::vector<ExprPtr> args);
};

}  // namespace fusion

#endif
