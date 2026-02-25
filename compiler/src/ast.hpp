#ifndef FUSION_AST_HPP
#define FUSION_AST_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fusion {

enum class BinOp { Add };

/* FFI type kind, matches rt_ffi_type_kind_t. */
enum class FfiType {
  Void,
  I32,
  I64,
  F32,
  F64,
  Ptr,
  Cstring,
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  enum class Kind { IntLiteral, FloatLiteral, StringLiteral, BinaryOp, Call, VarRef };
  Kind kind = Kind::IntLiteral;

  int64_t int_value = 0;
  double float_value = 0.0;
  std::string str_value;
  BinOp bin_op = BinOp::Add;
  ExprPtr left;
  ExprPtr right;
  std::string callee;
  std::vector<ExprPtr> args;
  std::string var_name;  // for VarRef

  static ExprPtr make_int(int64_t value);
  static ExprPtr make_float(double value);
  static ExprPtr make_string(std::string value);
  static ExprPtr make_binop(BinOp op, ExprPtr left, ExprPtr right);
  static ExprPtr make_call(std::string callee, std::vector<ExprPtr> args);
  static ExprPtr make_var_ref(std::string name);
};

/* extern lib "path"; or extern lib "path" as name; */
struct ExternLib {
  std::string path;
  std::string name;  // empty = default (single lib)
};

/* extern fn name(x: f64, ...) -> ret_type; (symbol in default lib) */
struct ExternFn {
  std::string name;
  std::vector<std::pair<std::string, FfiType>> params;
  FfiType return_type;
  std::string lib_name;  // empty = default lib
};

/* let name = init; */
struct LetBinding {
  std::string name;
  ExprPtr init;
};

struct Program;
using ProgramPtr = std::unique_ptr<Program>;

/* Top-level: zero or more extern decls, zero or more let-bindings, then one or more expressions (; separated). */
struct Program {
  std::vector<ExternLib> libs;
  std::vector<ExternFn> extern_fns;
  std::vector<LetBinding> bindings;
  std::vector<ExprPtr> stmts;  /* executed in order; optional semicolons between */
};

}  // namespace fusion

#endif
