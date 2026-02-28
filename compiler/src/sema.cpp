#include "ast.hpp"
#include "layout.hpp"
#include "sema.hpp"
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace fusion {

static FnPtrSig fn_def_to_sig(const FnDef& def) {
  FnPtrSig sig;
  for (const auto& p : def.params) sig.params.push_back(p.second);
  sig.result = def.return_type;
  return sig;
}

static FnPtrSig extern_fn_to_sig(const ExternFn& ext) {
  FnPtrSig sig;
  for (const auto& p : ext.params) sig.params.push_back(p.second);
  sig.result = ext.return_type;
  return sig;
}

struct SemaContext {
  std::unordered_set<std::string> lib_names;
  std::unordered_map<std::string, ExternFn> extern_fn_by_name;
  std::unordered_map<std::string, FnDef*> user_fn_by_name;
  std::vector<std::unordered_map<std::string, FfiType>> var_scope_stack;
  std::vector<std::unordered_map<std::string, FfiType>> array_element_scope_stack;
  std::vector<std::unordered_map<std::string, FnPtrSig>> fnptr_scope_stack;
  LayoutMap* layout_map = nullptr;  // from Program::struct_defs
  Program* program = nullptr;
  SemaError* err = nullptr;
  bool has_expected_return_type = false;
  FfiType expected_return_type = FfiType::Void;
};

/* Lookup variable type from innermost to outermost scope. */
static bool var_type_lookup(SemaContext* ctx, const std::string& name, FfiType* out) {
  if (!ctx || ctx->var_scope_stack.empty()) return false;
  for (auto it = ctx->var_scope_stack.rbegin(); it != ctx->var_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) {
      *out = fit->second;
      return true;
    }
  }
  return false;
}

/* Lookup array element type from innermost to outermost scope. */
static FfiType array_elem_lookup(SemaContext* ctx, const std::string& name) {
  if (!ctx || ctx->array_element_scope_stack.empty()) return FfiType::Void;
  for (auto it = ctx->array_element_scope_stack.rbegin(); it != ctx->array_element_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return FfiType::Void;
}

static bool is_alloc_type(const std::string& name, Program* program) {
  if (name == "i32" || name == "i64" || name == "f32" || name == "f64" || name == "ptr")
    return true;
  if (program)
    for (const auto& s : program->struct_defs)
      if (s.name == name) return true;
  return false;
}

static FfiType expr_type(Expr* expr, SemaContext* ctx);  // returns type of expression for type-checking

/* Returns element type if expr is an array (ptr from alloc_array or VarRef to such); otherwise FfiType::Void. */
static FfiType get_array_element_type(Expr* expr, SemaContext* ctx) {
  if (!expr || !ctx) return FfiType::Void;
  if (expr->kind == Expr::Kind::VarRef) {
    return array_elem_lookup(ctx, expr->var_name);
  }
  if (expr->kind == Expr::Kind::AllocArray) {
    const std::string& t = expr->var_name;
    if (t == "i32") return FfiType::I32;
    if (t == "i64") return FfiType::I64;
    if (t == "f32") return FfiType::F32;
    if (t == "f64") return FfiType::F64;
    if (t == "ptr") return FfiType::Ptr;
    return FfiType::Void;
  }
  return FfiType::Void;
}

/* Lookup function pointer signature for an expression. Returns true if known. */
static bool lookup_fnptr_sig(SemaContext* ctx, Expr* expr, FnPtrSig* out) {
  if (!ctx || !expr || !out) return false;
  if (expr->kind == Expr::Kind::VarRef) {
    for (auto it = ctx->fnptr_scope_stack.rbegin(); it != ctx->fnptr_scope_stack.rend(); ++it) {
      auto fit = it->find(expr->var_name);
      if (fit != it->end()) {
        *out = fit->second;
        return true;
      }
    }
    auto user_it = ctx->user_fn_by_name.find(expr->var_name);
    if (user_it != ctx->user_fn_by_name.end()) {
      *out = fn_def_to_sig(*user_it->second);
      return true;
    }
    auto ext_it = ctx->extern_fn_by_name.find(expr->var_name);
    if (ext_it != ctx->extern_fn_by_name.end()) {
      *out = extern_fn_to_sig(ext_it->second);
      return true;
    }
    return false;
  }
  if (expr->kind == Expr::Kind::Call && expr->callee == "get_func_ptr" &&
      expr->args.size() == 1 && expr->args[0]->kind == Expr::Kind::VarRef) {
    const std::string& fn_name = expr->args[0]->var_name;
    auto user_it = ctx->user_fn_by_name.find(fn_name);
    if (user_it != ctx->user_fn_by_name.end()) {
      *out = fn_def_to_sig(*user_it->second);
      return true;
    }
    auto ext_it = ctx->extern_fn_by_name.find(fn_name);
    if (ext_it != ctx->extern_fn_by_name.end()) {
      *out = extern_fn_to_sig(ext_it->second);
      return true;
    }
    return false;
  }
  return false;
}

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
      if (expr->callee == "get_func_ptr") {
        if (expr->args.size() != 1) {
          ctx.err->message = "get_func_ptr expects exactly one argument";
          return false;
        }
        if (expr->args[0]->kind != Expr::Kind::VarRef) {
          ctx.err->message = "get_func_ptr argument must be a function name";
          return false;
        }
        const std::string& fn_name = expr->args[0]->var_name;
        auto user_it = ctx.user_fn_by_name.find(fn_name);
        auto ext_it = ctx.extern_fn_by_name.find(fn_name);
        if (user_it == ctx.user_fn_by_name.end() && ext_it == ctx.extern_fn_by_name.end()) {
          ctx.err->message = "get_func_ptr: unknown function '" + fn_name + "'";
          return false;
        }
        return true;
      }
      if (expr->callee == "call") {
        if (expr->args.size() < 1) {
          ctx.err->message = "call expects at least a function pointer argument";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "call first argument must be a function pointer";
          return false;
        }
        FnPtrSig sig;
        if (!lookup_fnptr_sig(&ctx, expr->args[0].get(), &sig)) {
          /* First arg is Ptr but target unknown (e.g. load_field): infer signature from call site. */
          for (size_t k = 1; k < expr->args.size(); ++k) {
            if (!check_expr(expr->args[k].get(), ctx)) return false;
          }
          sig.params.clear();
          for (size_t k = 1; k < expr->args.size(); ++k)
            sig.params.push_back(expr_type(expr->args[k].get(), &ctx));
          sig.result = ctx.has_expected_return_type ? ctx.expected_return_type : FfiType::Void;
          expr->inferred_call_param_types = sig.params;
          expr->inferred_call_result_type = sig.result;
        }
        if (expr->args.size() - 1 != sig.params.size()) {
          ctx.err->message = "call: wrong number of arguments for function pointer";
          return false;
        }
        for (size_t j = 0; j < sig.params.size(); ++j) {
          if (!check_expr(expr->args[j + 1].get(), ctx)) return false;
          FfiType arg_ty = expr_type(expr->args[j + 1].get(), &ctx);
          FfiType want = sig.params[j];
          bool compat = (arg_ty == want) ||
            (arg_ty == FfiType::I64 && (want == FfiType::F64 || want == FfiType::F32)) ||
            (arg_ty == FfiType::F64 && want == FfiType::I64) ||
            (arg_ty == FfiType::Ptr && want == FfiType::I64) ||
            (arg_ty == FfiType::I64 && want == FfiType::Ptr);
          if (!compat) {
            ctx.err->message = "call: argument type mismatch for function pointer";
            return false;
          }
        }
        return true;
      }
      if (expr->callee == "print") {
        if (expr->args.size() != 1 && expr->args.size() != 2) {
          ctx.err->message = "print expects 1 or 2 arguments";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        FfiType arg_ty = expr_type(expr->args[0].get(), &ctx);
        if (arg_ty != FfiType::I64 && arg_ty != FfiType::F64 && arg_ty != FfiType::Ptr) {
          ctx.err->message = "print expects i64, f64, or pointer argument";
          return false;
        }
        if (expr->args.size() == 2) {
          if (!check_expr(expr->args[1].get(), ctx)) return false;
          if (expr_type(expr->args[1].get(), &ctx) != FfiType::I64) {
            ctx.err->message = "print stream argument must be i64";
            return false;
          }
        }
        return true;
      }
      if (expr->callee == "read_line") {
        if (expr->args.size() != 0) {
          ctx.err->message = "read_line expects no arguments";
          return false;
        }
        return true;
      }
      if (expr->callee == "to_str") {
        if (expr->args.size() != 1) {
          ctx.err->message = "to_str expects exactly one argument";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        FfiType t = expr_type(expr->args[0].get(), &ctx);
        if (t != FfiType::I64 && t != FfiType::F64) {
          ctx.err->message = "to_str expects i64 or f64 argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "from_str") {
        if (expr->args.size() != 1) {
          ctx.err->message = "from_str expects one argument (string)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "from_str expects pointer (string) argument";
          return false;
        }
        if (expr->call_type_arg != "i64" && expr->call_type_arg != "f64") {
          ctx.err->message = "from_str requires type argument: use from_str(s, i64) or from_str(s, f64)";
          return false;
        }
        return true;
      }
      if (expr->callee == "open") {
        if (expr->args.size() != 2) {
          ctx.err->message = "open expects (path, mode)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx) || !check_expr(expr->args[1].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr || expr_type(expr->args[1].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "open expects two pointer (string) arguments";
          return false;
        }
        return true;
      }
      if (expr->callee == "close") {
        if (expr->args.size() != 1) {
          ctx.err->message = "close expects one argument (file handle)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "close expects pointer argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "read_line_file") {
        if (expr->args.size() != 1) {
          ctx.err->message = "read_line_file expects one argument (file handle)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "read_line_file expects pointer argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "write_file") {
        if (expr->args.size() != 2) {
          ctx.err->message = "write_file expects (handle, value)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx) || !check_expr(expr->args[1].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "write_file first argument must be pointer (file handle)";
          return false;
        }
        FfiType val_ty = expr_type(expr->args[1].get(), &ctx);
        if (val_ty != FfiType::I64 && val_ty != FfiType::F64 && val_ty != FfiType::Ptr) {
          ctx.err->message = "write_file second argument must be i64, f64, or ptr";
          return false;
        }
        return true;
      }
      if (expr->callee == "eof_file") {
        if (expr->args.size() != 1) {
          ctx.err->message = "eof_file expects one argument (file handle)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "eof_file expects pointer argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "line_count_file") {
        if (expr->args.size() != 1) {
          ctx.err->message = "line_count_file expects one argument (file handle)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "line_count_file expects pointer argument";
          return false;
        }
        return true;
      }
      if (expr->callee == "len") {
        if (expr->args.size() != 1) {
          ctx.err->message = "len expects 1 argument";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = "len expects a pointer (array)";
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
      FfiType ty;
      if (!var_type_lookup(&ctx, expr->var_name, &ty)) {
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
    case Expr::Kind::AllocArray:
      if (!expr->left) return false;
      if (!is_alloc_type(expr->var_name, ctx.program)) {
        ctx.err->message = "alloc_array: unknown element type '" + expr->var_name + "'";
        return false;
      }
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::I64) {
        ctx.err->message = "alloc_array: count must be i64";
        return false;
      }
      return true;
    case Expr::Kind::Index:
      if (!expr->left || !expr->right) return false;
      if (!check_expr(expr->left.get(), ctx) || !check_expr(expr->right.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "index: base must be a pointer (array)";
        return false;
      }
      if (expr_type(expr->right.get(), &ctx) != FfiType::I64) {
        ctx.err->message = "index: index must be i64";
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
      if (expr->var_name == "ptr") {
        if (from != FfiType::Ptr) {
          ctx.err->message = "cast to ptr: operand must be a pointer";
          return false;
        }
        return true;
      }
      if (expr->var_name == "i64" || expr->var_name == "i32" || expr->var_name == "f64" || expr->var_name == "f32") {
        bool from_numeric = (from == FfiType::I64 || from == FfiType::I32 || from == FfiType::F64 || from == FfiType::F32);
        if (!from_numeric) {
          ctx.err->message = "cast to numeric type: operand must be i64, i32, f64, or f32";
          return false;
        }
        return true;
      }
      ctx.err->message = "cast: target type must be ptr, i64, i32, f64, or f32";
      return false;
    }
    case Expr::Kind::Compare: {
      if (!expr->left || !expr->right) return false;
      if (!check_expr(expr->left.get(), ctx) || !check_expr(expr->right.get(), ctx)) return false;
      FfiType l = expr_type(expr->left.get(), &ctx);
      FfiType r = expr_type(expr->right.get(), &ctx);
      if (l == FfiType::Ptr && r == FfiType::Ptr) {
        if (expr->compare_op != CompareOp::Eq && expr->compare_op != CompareOp::Ne) {
          ctx.err->message = "pointer comparison only supports == and !=";
          return false;
        }
        return true;
      }
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
      if (expr->callee == "get_func_ptr") return FfiType::Ptr;
      if (expr->callee == "call") {
        if (ctx) {
          FnPtrSig sig;
          if (expr->args.size() >= 1 && lookup_fnptr_sig(ctx, expr->args[0].get(), &sig))
            return sig.result;
          if (!expr->inferred_call_param_types.empty())
            return expr->inferred_call_result_type;
        }
        return FfiType::Void;
      }
      if (expr->callee == "print") return FfiType::Void;
      if (expr->callee == "len") return FfiType::I64;
      if (expr->callee == "read_line" || expr->callee == "read_line_file") return FfiType::Ptr;
      if (expr->callee == "to_str") return FfiType::Ptr;
      if (expr->callee == "from_str") {
        if (expr->call_type_arg == "i64") return FfiType::I64;
        if (expr->call_type_arg == "f64") return FfiType::F64;
        return FfiType::Void;
      }
      if (expr->callee == "open") return FfiType::Ptr;
      if (expr->callee == "close") return FfiType::Void;
      if (expr->callee == "write_file") return FfiType::Void;
      if (expr->callee == "eof_file" || expr->callee == "line_count_file") return FfiType::I64;
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
        FfiType ty;
        if (var_type_lookup(ctx, expr->var_name, &ty)) return ty;
      }
      return FfiType::Void;
    case Expr::Kind::Alloc:
    case Expr::Kind::AllocArray:
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
      if (expr->var_name == "ptr") return FfiType::Ptr;
      if (expr->var_name == "i64") return FfiType::I64;
      if (expr->var_name == "i32") return FfiType::I32;
      if (expr->var_name == "f64") return FfiType::F64;
      if (expr->var_name == "f32") return FfiType::F32;
      return FfiType::Void;
    case Expr::Kind::Compare:
      return FfiType::I64;  /* condition type as i64 0/1 for codegen */
    case Expr::Kind::Index: {
      FfiType elem = get_array_element_type(expr->left.get(), ctx);
      return (elem != FfiType::Void) ? elem : FfiType::I64;
    }
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

static bool check_stmt(SemaContext& ctx, FnDef* def, Stmt* stmt);

static bool check_stmt(SemaContext& ctx, FnDef* def, Stmt* stmt) {
  if (!stmt) return false;
  switch (stmt->kind) {
    case Stmt::Kind::Return:
      if (!def) {
        ctx.err->message = "return only allowed inside a function";
        return false;
      }
      ctx.has_expected_return_type = true;
      ctx.expected_return_type = def->return_type;
      if (!check_expr(stmt->expr.get(), ctx)) {
        ctx.has_expected_return_type = false;
        return false;
      }
      ctx.has_expected_return_type = false;
      if (expr_type(stmt->expr.get(), &ctx) != def->return_type) {
        ctx.err->message = "return type does not match function return type in '" + def->name + "'";
        return false;
      }
      return true;
    case Stmt::Kind::Let: {
      if (!check_expr(stmt->init.get(), ctx)) return false;
      if (ctx.var_scope_stack.empty() || ctx.var_scope_stack.back().count(stmt->name)) {
        ctx.err->message = def
          ? "duplicate variable '" + stmt->name + "' in function '" + def->name + "'"
          : "duplicate variable '" + stmt->name + "'";
        return false;
      }
      FfiType let_ty = expr_type(stmt->init.get(), &ctx);
      ctx.var_scope_stack.back()[stmt->name] = let_ty;
      if (let_ty == FfiType::Ptr && !ctx.fnptr_scope_stack.empty()) {
        FnPtrSig sig;
        if (lookup_fnptr_sig(&ctx, stmt->init.get(), &sig))
          ctx.fnptr_scope_stack.back()[stmt->name] = sig;
      }
      FfiType elem_ty = get_array_element_type(stmt->init.get(), &ctx);
      if (elem_ty != FfiType::Void) {
        ctx.array_element_scope_stack.back()[stmt->name] = elem_ty;
      } else if (let_ty == FfiType::Ptr && stmt->init->kind == Expr::Kind::LoadField) {
        Expr* e = stmt->init.get();
        auto it = ctx.layout_map->find(e->load_field_struct);
        if (it != ctx.layout_map->end()) {
          for (const auto& f : it->second.fields)
            if (f.first == e->load_field_field && f.second.type == FfiType::Ptr) {
              ctx.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
              break;
            }
        }
      } else if (let_ty == FfiType::Ptr && stmt->init->kind == Expr::Kind::Call)
        ctx.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
      return true;
    }
    case Stmt::Kind::Expr:
      return check_expr(stmt->expr.get(), ctx);
    case Stmt::Kind::If:
      if (!check_expr(stmt->cond.get(), ctx)) return false;
      ctx.var_scope_stack.push_back({});
      ctx.array_element_scope_stack.push_back({});
      ctx.fnptr_scope_stack.push_back({});
      for (StmtPtr& s : stmt->then_body)
        if (!check_stmt(ctx, def, s.get())) {
          ctx.var_scope_stack.pop_back();
          ctx.array_element_scope_stack.pop_back();
          ctx.fnptr_scope_stack.pop_back();
          return false;
        }
      ctx.var_scope_stack.pop_back();
      ctx.array_element_scope_stack.pop_back();
      ctx.fnptr_scope_stack.pop_back();
      if (stmt->else_body.empty()) return true;
      ctx.var_scope_stack.push_back({});
      ctx.array_element_scope_stack.push_back({});
      ctx.fnptr_scope_stack.push_back({});
      for (StmtPtr& s : stmt->else_body)
        if (!check_stmt(ctx, def, s.get())) {
          ctx.var_scope_stack.pop_back();
          ctx.array_element_scope_stack.pop_back();
          ctx.fnptr_scope_stack.pop_back();
          return false;
        }
      ctx.var_scope_stack.pop_back();
      ctx.array_element_scope_stack.pop_back();
      ctx.fnptr_scope_stack.pop_back();
      return true;
    case Stmt::Kind::For: {
      if (!stmt->cond) return false;
      ctx.var_scope_stack.push_back({});
      ctx.array_element_scope_stack.push_back({});
      ctx.fnptr_scope_stack.push_back({});
      if (stmt->for_init) {
        if (stmt->for_init->kind == Stmt::Kind::Let) {
          if (!check_expr(stmt->for_init->init.get(), ctx)) {
            ctx.var_scope_stack.pop_back();
            ctx.array_element_scope_stack.pop_back();
            ctx.fnptr_scope_stack.pop_back();
            return false;
          }
          if (ctx.var_scope_stack.back().count(stmt->for_init->name)) {
            ctx.err->message = def
              ? "duplicate variable '" + stmt->for_init->name + "' in function '" + def->name + "'"
              : "duplicate variable '" + stmt->for_init->name + "'";
            ctx.var_scope_stack.pop_back();
            ctx.array_element_scope_stack.pop_back();
            ctx.fnptr_scope_stack.pop_back();
            return false;
          }
          ctx.var_scope_stack.back()[stmt->for_init->name] = expr_type(stmt->for_init->init.get(), &ctx);
        } else if (stmt->for_init->kind == Stmt::Kind::Assign) {
          if (!check_stmt(ctx, def, stmt->for_init.get())) {
            ctx.var_scope_stack.pop_back();
            ctx.array_element_scope_stack.pop_back();
            ctx.fnptr_scope_stack.pop_back();
            return false;
          }
        }
      }
      if (!check_expr(stmt->cond.get(), ctx)) {
        ctx.var_scope_stack.pop_back();
        ctx.array_element_scope_stack.pop_back();
        ctx.fnptr_scope_stack.pop_back();
        return false;
      }
      if (stmt->for_update) {
        if (stmt->for_update->kind != Stmt::Kind::Assign || !check_stmt(ctx, def, stmt->for_update.get())) {
          ctx.var_scope_stack.pop_back();
          ctx.array_element_scope_stack.pop_back();
          ctx.fnptr_scope_stack.pop_back();
          return false;
        }
      }
      for (StmtPtr& s : stmt->body)
        if (!check_stmt(ctx, def, s.get())) {
          ctx.var_scope_stack.pop_back();
          ctx.array_element_scope_stack.pop_back();
          ctx.fnptr_scope_stack.pop_back();
          return false;
        }
      ctx.var_scope_stack.pop_back();
      ctx.array_element_scope_stack.pop_back();
      ctx.fnptr_scope_stack.pop_back();
      return true;
    }
    case Stmt::Kind::Assign: {
      if (!stmt->expr || !stmt->init) return false;
      if (!check_expr(stmt->expr.get(), ctx) || !check_expr(stmt->init.get(), ctx)) return false;
      if (stmt->expr->kind == Expr::Kind::VarRef) {
        FfiType var_ty = expr_type(stmt->expr.get(), &ctx);
        FfiType val_ty = expr_type(stmt->init.get(), &ctx);
        bool compat = (var_ty == val_ty) ||
          (var_ty == FfiType::Ptr && val_ty == FfiType::I64) ||
          (var_ty == FfiType::I64 && val_ty == FfiType::Ptr);
        if (!compat) {
          ctx.err->message = "assignment type mismatch";
          return false;
        }
        if (var_ty == FfiType::Ptr && val_ty == FfiType::Ptr && !ctx.fnptr_scope_stack.empty()) {
          FnPtrSig sig;
          if (lookup_fnptr_sig(&ctx, stmt->init.get(), &sig))
            ctx.fnptr_scope_stack.back()[stmt->expr->var_name] = sig;
        }
        return true;
      }
      if (stmt->expr->kind == Expr::Kind::Index) {
        FfiType elem_ty = get_array_element_type(stmt->expr->left.get(), &ctx);
        if (elem_ty == FfiType::Void) elem_ty = FfiType::I64;
        FfiType val_ty = expr_type(stmt->init.get(), &ctx);
        bool compat = (elem_ty == val_ty) ||
          (elem_ty == FfiType::Ptr && val_ty == FfiType::I64) ||
          (elem_ty == FfiType::I64 && val_ty == FfiType::Ptr);
        if (!compat) {
          ctx.err->message = "assignment type mismatch for array element";
          return false;
        }
        return true;
      }
      ctx.err->message = "assignment target must be a variable or index";
      return false;
    }
  }
  return false;
}

static bool check_fn_def(SemaContext& ctx, FnDef& def) {
  std::unordered_map<std::string, FfiType> local;
  std::unordered_map<std::string, FfiType> array_local;
  for (size_t j = 0; j < def.params.size(); ++j)
    local[def.params[j].first] = def.params[j].second;
  for (const auto& p : def.params)
    if (p.second == FfiType::Ptr)
      array_local[p.first] = FfiType::Ptr;
  SemaContext fn_ctx;
  fn_ctx.err = ctx.err;
  fn_ctx.layout_map = ctx.layout_map;
  fn_ctx.program = ctx.program;
  fn_ctx.extern_fn_by_name = ctx.extern_fn_by_name;
  fn_ctx.user_fn_by_name = ctx.user_fn_by_name;
  fn_ctx.var_scope_stack.push_back(std::move(local));
  fn_ctx.array_element_scope_stack.push_back(std::move(array_local));
  fn_ctx.fnptr_scope_stack.push_back({});
  for (StmtPtr& stmt : def.body) {
    if (!check_stmt(fn_ctx, &def, stmt.get())) return false;
  }
  return true;
}

SemaResult check(Program* program) {
  SemaResult r;
  if (!program || program->top_level.empty()) {
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
  ctx.var_scope_stack.push_back({});
  ctx.array_element_scope_stack.push_back({});
  ctx.fnptr_scope_stack.push_back({});
  for (const TopLevelItem& item : program->top_level) {
    if (const LetBinding* binding = std::get_if<LetBinding>(&item)) {
      if (!check_expr(binding->init.get(), ctx)) return r;
      FfiType ty = expr_type(binding->init.get(), &ctx);
      if (ctx.var_scope_stack.back().count(binding->name)) {
        ctx.err->message = "duplicate variable '" + binding->name + "'";
        return r;
      }
      ctx.var_scope_stack.back()[binding->name] = ty;
      if (ty == FfiType::Ptr) {
        FnPtrSig sig;
        if (lookup_fnptr_sig(&ctx, binding->init.get(), &sig))
          ctx.fnptr_scope_stack.back()[binding->name] = sig;
      }
      FfiType elem_ty = get_array_element_type(binding->init.get(), &ctx);
      if (elem_ty != FfiType::Void)
        ctx.array_element_scope_stack.back()[binding->name] = elem_ty;
      else if (ty == FfiType::Ptr && binding->init->kind == Expr::Kind::Call)
        ctx.array_element_scope_stack.back()[binding->name] = FfiType::Ptr;
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
