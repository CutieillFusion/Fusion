#ifndef FUSION_AST_HPP
#define FUSION_AST_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
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
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  enum class Kind {
    IntLiteral, FloatLiteral, StringLiteral, BinaryOp, Call, VarRef,
    Alloc, AllocBytes, AddrOf, Load, LoadF64, LoadI32, LoadPtr, Store, LoadField, StoreField, Cast
  };
  Kind kind = Kind::IntLiteral;

  int64_t int_value = 0;
  double float_value = 0.0;
  std::string str_value;
  BinOp bin_op = BinOp::Add;
  ExprPtr left;
  ExprPtr right;
  std::string callee;
  std::vector<ExprPtr> args;
  std::string var_name;  // for VarRef, or alloc type name for Alloc
  std::string load_field_struct;  // for LoadField
  std::string load_field_field;   // for LoadField

  static ExprPtr make_int(int64_t value);
  static ExprPtr make_float(double value);
  static ExprPtr make_string(std::string value);
  static ExprPtr make_binop(BinOp op, ExprPtr left, ExprPtr right);
  static ExprPtr make_call(std::string callee, std::vector<ExprPtr> args);
  static ExprPtr make_var_ref(std::string name);
  static ExprPtr make_alloc(std::string type_name);
  static ExprPtr make_alloc_bytes(ExprPtr size_expr);
  static ExprPtr make_addr_of(ExprPtr expr);
  static ExprPtr make_load(ExprPtr ptr);
  static ExprPtr make_load_f64(ExprPtr ptr);
  static ExprPtr make_load_i32(ExprPtr ptr);
  static ExprPtr make_load_ptr(ExprPtr ptr);
  static ExprPtr make_store(ExprPtr ptr, ExprPtr value);
  static ExprPtr make_load_field(ExprPtr ptr, std::string struct_name, std::string field_name);
  static ExprPtr make_store_field(ExprPtr ptr, std::string struct_name, std::string field_name, ExprPtr value);
  static ExprPtr make_cast(ExprPtr operand, std::string target_type_name);
};

/* extern lib "path"; or extern lib "path" as name; */
struct ExternLib {
  std::string path;
  std::string name;  // empty = default (single lib)
};

/* extern fn name(x: f64, ...) -> ret_type; (symbol in default lib).
 * param_type_names[i] non-empty means param type was a named type (opaque/struct) -> PTR at ABI. */
struct ExternFn {
  std::string name;
  std::vector<std::pair<std::string, FfiType>> params;
  std::vector<std::string> param_type_names;  // same size as params; "" = keyword type
  FfiType return_type;
  std::string return_type_name;  // non-empty = named type (opaque/struct) -> PTR
  std::string lib_name;  // empty = default lib
};

/* let name = init; */
struct LetBinding {
  std::string name;
  ExprPtr init;
};

/* Statement inside a function body. */
struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
struct Stmt {
  enum class Kind { Return, Let, Expr };
  Kind kind = Kind::Return;
  ExprPtr expr;       // for Return and ExprStmt
  std::string name;   // for Let
  ExprPtr init;       // for Let
  static StmtPtr make_return(ExprPtr expr);
  static StmtPtr make_let(std::string name, ExprPtr init);
  static StmtPtr make_expr(ExprPtr expr);
};

/* User-defined function: fn name(params) -> ret { body }. */
struct FnDef {
  std::string name;
  std::vector<std::pair<std::string, FfiType>> params;
  std::vector<std::string> param_type_names;  // same size as params; "" = keyword type
  FfiType return_type = FfiType::Void;
  std::string return_type_name;  // non-empty = named type -> PTR
  std::vector<StmtPtr> body;
};

/* struct Name { field: type; ... }; fields use primitive FfiType only in v1. */
struct StructDef {
  std::string name;
  std::vector<std::pair<std::string, FfiType>> fields;
};

struct Program;
using ProgramPtr = std::unique_ptr<Program>;

/* One top-level item: either a let binding or an expression. */
using TopLevelItem = std::variant<LetBinding, ExprPtr>;

/* Top-level: opaque/struct decls, extern decls, user fn defs, then let-bindings and expressions. */
struct Program {
  std::vector<std::string> opaque_types;
  std::vector<StructDef> struct_defs;
  std::vector<ExternLib> libs;
  std::vector<ExternFn> extern_fns;
  std::vector<FnDef> user_fns;
  std::vector<TopLevelItem> top_level;  /* executed in order; at least one must be an expression */
};

}  // namespace fusion

#endif
