#ifndef FUSION_AST_HPP
#define FUSION_AST_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fusion {

enum class BinOp {
  Add,
  Sub,
  Mul,
  Div
};

enum class CompareOp {
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge
};

/* FFI type kind, matches rt_ffi_type_kind_t. */
enum class FfiType {
  Void,
  I8,
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
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BinaryOp,
    Call,
    VarRef,
    StackAlloc,
    HeapAlloc,
    StackArray,
    HeapArray,
    Free,
    FreeArray,
    AsHeap,
    AsArray,
    AddrOf,
    Load,
    LoadF64,
    LoadI32,
    LoadPtr,
    Store,
    LoadField,
    StoreField,
    FieldAccess,
    Cast,
    Compare,
    Index
  };
  Kind kind = Kind::IntLiteral;

  int64_t int_value = 0;
  double float_value = 0.0;
  std::string str_value;
  BinOp bin_op = BinOp::Add;
  CompareOp compare_op = CompareOp::Eq;
  ExprPtr left;
  ExprPtr right;
  std::string callee;
  std::vector<ExprPtr> args;
  std::string call_type_arg;  // optional type arg for Call: e.g. range elem type, from_str result type; "" = none
  std::string var_name;  // for VarRef, or heap_array type name for HeapArray
  std::string load_field_struct;  // for LoadField / FieldAccess (base struct name, filled by sema)
  std::string load_field_field;   // for LoadField
  std::vector<std::string> field_chain;  // for FieldAccess: ordered list of field names

  /* When non-empty, sema inferred the call signature for call(ptr, ...); codegen uses this. */
  std::vector<FfiType> inferred_call_param_types;
  FfiType inferred_call_result_type = FfiType::Void;

  /* Ptr element type inferred by sema: "" = unknown/void, "char" = string, struct name = typed ptr. */
  std::string inferred_ptr_element;

  /* Source position for error reporting; 0 = unknown. */
  size_t line = 0;
  size_t column = 0;

  static ExprPtr make_int(int64_t value);
  static ExprPtr make_float(double value);
  static ExprPtr make_string(std::string value);
  static ExprPtr make_binop(BinOp op, ExprPtr left, ExprPtr right);
  static ExprPtr make_call(std::string callee, std::vector<ExprPtr> args, std::string call_type_arg = "");
  static ExprPtr make_var_ref(std::string name);
  static ExprPtr make_stack_alloc(std::string type_name);
  static ExprPtr make_heap_alloc(std::string type_name);
  static ExprPtr make_stack_array(std::string element_type, ExprPtr count_expr);
  static ExprPtr make_heap_array(std::string element_type, ExprPtr count_expr);
  static ExprPtr make_free(ExprPtr ptr);
  static ExprPtr make_free_array(ExprPtr ptr);
  static ExprPtr make_as_heap(ExprPtr ptr);
  static ExprPtr make_as_array(ExprPtr ptr, std::string element_type);
  static ExprPtr make_index(ExprPtr base, ExprPtr index_expr);
  static ExprPtr make_addr_of(ExprPtr expr);
  static ExprPtr make_load(ExprPtr ptr);
  static ExprPtr make_load_f64(ExprPtr ptr);
  static ExprPtr make_load_i32(ExprPtr ptr);
  static ExprPtr make_load_ptr(ExprPtr ptr);
  static ExprPtr make_store(ExprPtr ptr, ExprPtr value);
  static ExprPtr make_load_field(ExprPtr ptr, std::string struct_name, std::string field_name);
  static ExprPtr make_store_field(ExprPtr ptr, std::string struct_name, std::string field_name, ExprPtr value);
  static ExprPtr make_field_access(ExprPtr base, std::vector<std::string> field_chain);
  static ExprPtr make_cast(ExprPtr operand, std::string target_type_name);
  static ExprPtr make_compare(CompareOp op, ExprPtr left, ExprPtr right);

  /** Deep copy for multifile merge. */
  ExprPtr clone() const;
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
  std::string array_element_struct;  // non-empty when return ptr: array elements are ptr to this struct
  std::string lib_name;  // empty = default lib
};

/* Signature-only function declaration (for import lib block). Same shape as ExternFn but for Fusion fns. */
struct FnDecl {
  std::string name;
  std::vector<std::pair<std::string, FfiType>> params;
  std::vector<std::string> param_type_names;
  FfiType return_type = FfiType::Void;
  std::string return_type_name;
  std::string array_element_struct;  // non-empty when return ptr: array elements are ptr to this struct
};

/* import lib "name" { struct X; fn foo(...) -> ret; }; struct_names are name-only. */
struct ImportLib {
  std::string name;
  std::vector<std::string> struct_names;
  std::vector<FnDecl> fn_decls;
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
  enum class Kind { Return, Let, Expr, If, For, Assign };
  Kind kind = Kind::Return;
  ExprPtr expr;       // for Return, ExprStmt, Assign (LHS target)
  std::string name;   // for Let
  ExprPtr init;       // for Let (init), Assign (RHS value)
  ExprPtr cond;       // for If, For
  std::vector<StmtPtr> then_body;  // for If
  std::vector<StmtPtr> else_body;   // for If
  StmtPtr for_init;   // for For (Let or Assign, optional)
  StmtPtr for_update; // for For (Assign, optional)
  std::vector<StmtPtr> body;        // for For
  static StmtPtr make_return(ExprPtr expr);
  static StmtPtr make_let(std::string name, ExprPtr init);
  static StmtPtr make_expr(ExprPtr expr);
  static StmtPtr make_if(ExprPtr cond, std::vector<StmtPtr> then_body, std::vector<StmtPtr> else_body);
  static StmtPtr make_for(StmtPtr init, ExprPtr cond, StmtPtr update, std::vector<StmtPtr> body);
  static StmtPtr make_assign(ExprPtr target, ExprPtr value);

  /** Deep copy for multifile merge. */
  StmtPtr clone() const;
};

/* User-defined function: fn name(params) -> ret { body }. */
struct FnDef {
  std::string name;
  std::vector<std::pair<std::string, FfiType>> params;
  std::vector<std::string> param_type_names;  // same size as params; "" = keyword type
  std::vector<bool> param_noescape;  // same size as params; true = noescape
  FfiType return_type = FfiType::Void;
  std::string return_type_name;  // non-empty = named type -> PTR
  std::string array_element_struct;  // non-empty when return ptr: array elements are ptr to this struct
  std::vector<StmtPtr> body;
  bool exported = false;

  /** Deep copy for multifile merge. */
  FnDef clone() const;
};

/* struct Name { field: type; ... }; fields may be primitives or embedded structs.
 * field_type_names[i] non-empty means fields[i] is an embedded struct of that name
 * (FfiType for that field is Void as placeholder). */
struct StructDef {
  std::string name;
  std::vector<std::pair<std::string, FfiType>> fields;
  std::vector<std::string> field_type_names;  // parallel to fields; "" = primitive, else = struct name
  bool exported = false;
};

struct Program;
using ProgramPtr = std::unique_ptr<Program>;

/* One top-level item: let binding, expression, or statement (e.g. if). */
using TopLevelItem = std::variant<LetBinding, ExprPtr, StmtPtr>;

/* Top-level: import_libs first, then opaque/struct decls, extern decls, user fn defs, then let-bindings and expressions. */
struct Program {
  std::vector<ImportLib> import_libs;
  std::vector<std::string> opaque_types;
  std::vector<StructDef> struct_defs;
  std::vector<ExternLib> libs;
  std::vector<ExternFn> extern_fns;
  std::vector<FnDef> user_fns;
  std::vector<TopLevelItem> top_level;  /* executed in order; items are let bindings, if/for statements, assignments, or expressions */
};

// Returns the FfiType for a known builtin call with a fixed return type, or std::nullopt if
// not a simple fixed-return builtin. Callers handle "call" and "from_str" separately.
inline std::optional<FfiType> builtin_fixed_return_type(const std::string& callee) {
  static const std::unordered_map<std::string, FfiType> table = {
    {"get_func_ptr",    FfiType::Ptr},
    {"print",           FfiType::Void},
    {"println",         FfiType::Void},
    {"len",             FfiType::I64},
    {"read_line",       FfiType::Ptr},
    {"read_line_file",  FfiType::Ptr},
    {"to_str",          FfiType::Ptr},
    {"open",            FfiType::Ptr},
    {"close",           FfiType::Void},
    {"write_file",      FfiType::Void},
    {"eof_file",        FfiType::I64},
    {"line_count_file", FfiType::I64},
    {"write_bytes",     FfiType::I64},
    {"read_bytes",      FfiType::I64},
    {"http_request",    FfiType::Ptr},
    {"http_status",     FfiType::I64},
    {"str_dup",         FfiType::Ptr},
  };
  auto it = table.find(callee);
  if (it != table.end()) return it->second;
  return std::nullopt;
}

}  // namespace fusion

#endif
