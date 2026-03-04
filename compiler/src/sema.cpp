#include "ast.hpp"
#include "layout.hpp"
#include "sema.hpp"
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace fusion {

enum class AllocFlavor {
  HeapSingle,
  HeapArrayElementsPtr,
  StackSingle,
  StackArrayElementsPtr,
  Unknown,
};

enum class PtrBase {
  StackLocal,
  Heap,
  Param,
  Global,
  Unknown,
};

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
  std::vector<std::unordered_map<std::string, AllocFlavor>> var_flavor_scope_stack;
  std::vector<std::unordered_map<std::string, PtrBase>> var_base_scope_stack;
  /* Ptr-to-struct tracking: variable name -> struct name it points to */
  std::vector<std::unordered_map<std::string, std::string>> var_struct_scope_stack;
  /* Array element struct tracking: variable name -> struct name of elements */
  std::vector<std::unordered_map<std::string, std::string>> array_struct_scope_stack;
  /* Ptr element type tracking: variable name -> "char", struct name, or "" */
  std::vector<std::unordered_map<std::string, std::string>> var_ptr_element_scope_stack;
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

static AllocFlavor var_flavor_lookup(SemaContext* ctx, const std::string& name) {
  if (!ctx || ctx->var_flavor_scope_stack.empty()) return AllocFlavor::Unknown;
  for (auto it = ctx->var_flavor_scope_stack.rbegin(); it != ctx->var_flavor_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return AllocFlavor::Unknown;
}

static PtrBase var_base_lookup(SemaContext* ctx, const std::string& name) {
  if (!ctx || ctx->var_base_scope_stack.empty()) return PtrBase::Unknown;
  for (auto it = ctx->var_base_scope_stack.rbegin(); it != ctx->var_base_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return PtrBase::Unknown;
}

/* Lookup which struct a pointer variable points to. Returns "" if unknown. */
static std::string var_struct_lookup(SemaContext* ctx, const std::string& name) {
  if (!ctx || ctx->var_struct_scope_stack.empty()) return "";
  for (auto it = ctx->var_struct_scope_stack.rbegin(); it != ctx->var_struct_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return "";
}

/* Lookup ptr element type for a pointer variable. Returns "" if unknown. */
static std::string var_ptr_element_lookup(SemaContext* ctx, const std::string& name) {
  if (!ctx || ctx->var_ptr_element_scope_stack.empty()) return "";
  for (auto it = ctx->var_ptr_element_scope_stack.rbegin(); it != ctx->var_ptr_element_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return "";
}

/* Lookup struct name for elements of an array variable. Returns "" if unknown. */
static std::string array_struct_lookup(SemaContext* ctx, const std::string& name) {
  if (!ctx || ctx->array_struct_scope_stack.empty()) return "";
  for (auto it = ctx->array_struct_scope_stack.rbegin(); it != ctx->array_struct_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return "";
}

/* For a Call that returns ptr, return array_element_struct if the callee declares it. */
static std::string get_call_array_element_struct(Expr* expr, SemaContext* ctx) {
  if (!expr || expr->kind != Expr::Kind::Call || !ctx) return "";
  auto user_it = ctx->user_fn_by_name.find(expr->callee);
  if (user_it != ctx->user_fn_by_name.end()) {
    const FnDef& def = *user_it->second;
    if (def.return_type == FfiType::Ptr && def.return_type_name.empty() && !def.array_element_struct.empty())
      return def.array_element_struct;
  }
  auto ext_it = ctx->extern_fn_by_name.find(expr->callee);
  if (ext_it != ctx->extern_fn_by_name.end()) {
    const ExternFn& ext = ext_it->second;
    if (ext.return_type == FfiType::Ptr && ext.return_type_name.empty() && !ext.array_element_struct.empty())
      return ext.array_element_struct;
  }
  return "";
}

/* Get the struct name that an expression points to (for field access). */
static std::string expr_struct_name(Expr* expr, SemaContext* ctx);
static bool is_named_type_known(const std::string& name, Program* program);

static bool may_outlive_function(PtrBase b) {
  return b != PtrBase::StackLocal;
}

static bool is_stack_ptr(AllocFlavor f) {
  return f == AllocFlavor::StackSingle || f == AllocFlavor::StackArrayElementsPtr;
}

/* Update var's flavor and base in innermost scope that contains it. */
static void update_var_flavor_base(SemaContext* ctx, const std::string& name, AllocFlavor f, PtrBase b) {
  if (!ctx) return;
  for (auto it = ctx->var_flavor_scope_stack.rbegin(); it != ctx->var_flavor_scope_stack.rend(); ++it) {
    if (it->count(name)) {
      (*it)[name] = f;
      break;
    }
  }
  for (auto it = ctx->var_base_scope_stack.rbegin(); it != ctx->var_base_scope_stack.rend(); ++it) {
    if (it->count(name)) {
      (*it)[name] = b;
      break;
    }
  }
}

static FfiType expr_type(Expr* expr, SemaContext* ctx);  // forward

/* Returns the struct name an expression points to (for field access resolution). */
static std::string expr_struct_name(Expr* expr, SemaContext* ctx) {
  if (!expr || !ctx) return "";
  switch (expr->kind) {
    case Expr::Kind::VarRef: {
      std::string s = var_struct_lookup(ctx, expr->var_name);
      if (!s.empty()) return s;
      // Fallback: param ptr[Value] etc. may be in var_ptr_element_scope but not in var_struct_scope
      s = var_ptr_element_lookup(ctx, expr->var_name);
      if (!s.empty() && ctx->program && is_named_type_known(s, ctx->program))
        return s;
      return "";
    }
    case Expr::Kind::HeapAlloc:
    case Expr::Kind::StackAlloc:
      // Only struct names (not primitive type names)
      if (ctx->program) {
        for (const auto& s : ctx->program->struct_defs)
          if (s.name == expr->var_name) return expr->var_name;
      }
      return "";
    case Expr::Kind::Index:
      if (expr->left && expr->left->kind == Expr::Kind::VarRef)
        return array_struct_lookup(ctx, expr->left->var_name);
      if (expr->left && expr->left->kind == Expr::Kind::FieldAccess && ctx->layout_map) {
        Expr* fa = expr->left.get();
        std::string cur = expr_struct_name(fa->left.get(), ctx);
        if (cur.empty()) return "";
        for (const std::string& fname : fa->field_chain) {
          auto it = ctx->layout_map->find(cur);
          if (it == ctx->layout_map->end()) return "";
          for (const auto& f : it->second.fields) {
            if (f.first == fname) {
              cur = f.second.struct_name;
              if (cur.empty()) return "";
              break;
            }
          }
        }
        return cur;
      }
      if (expr->left && (expr->left->kind == Expr::Kind::HeapArray ||
                          expr->left->kind == Expr::Kind::StackArray)) {
        const std::string& tn = expr->left->var_name;
        if (ctx->program)
          for (const auto& s : ctx->program->struct_defs)
            if (s.name == tn) return tn;
      }
      return "";
    case Expr::Kind::FieldAccess: {
      // The struct name of the last field in the chain
      if (expr->field_chain.empty() || !ctx->layout_map) return "";
      std::string cur = expr_struct_name(expr->left.get(), ctx);
      if (cur.empty()) return "";
      for (size_t fi = 0; fi + 1 < expr->field_chain.size(); ++fi) {
        auto it = ctx->layout_map->find(cur);
        if (it == ctx->layout_map->end()) return "";
        bool found = false;
        for (const auto& f : it->second.fields) {
          if (f.first == expr->field_chain[fi]) {
            cur = f.second.struct_name;
            found = true;
            break;
          }
        }
        if (!found || cur.empty()) return "";
      }
      // Last field: return its struct_name if embedded, else ""
      auto it = ctx->layout_map->find(cur);
      if (it == ctx->layout_map->end()) return "";
      const std::string& last_field = expr->field_chain.back();
      for (const auto& f : it->second.fields) {
        if (f.first == last_field) return f.second.struct_name;
      }
      return "";
    }
    case Expr::Kind::Cast:
      if (ctx->program) {
        for (const auto& s : ctx->program->struct_defs)
          if (s.name == expr->var_name) return expr->var_name;
      }
      return "";
    case Expr::Kind::Call: {
      if (!ctx) return "";
      auto is_known_struct = [&](const std::string& name) -> bool {
        if (!ctx->program || name.empty()) return false;
        for (const auto& s : ctx->program->struct_defs)
          if (s.name == name) return true;
        return false;
      };
      auto user_it = ctx->user_fn_by_name.find(expr->callee);
      if (user_it != ctx->user_fn_by_name.end()) {
        if (!user_it->second->return_type_name.empty())
          return user_it->second->return_type_name;
        // -> ptr[T] return type: T is in array_element_struct; use it for struct field tracking
        if (user_it->second->return_type == FfiType::Ptr &&
            is_known_struct(user_it->second->array_element_struct))
          return user_it->second->array_element_struct;
      }
      auto ext_it = ctx->extern_fn_by_name.find(expr->callee);
      if (ext_it != ctx->extern_fn_by_name.end()) {
        if (!ext_it->second.return_type_name.empty())
          return ext_it->second.return_type_name;
        if (ext_it->second.return_type == FfiType::Ptr &&
            is_known_struct(ext_it->second.array_element_struct))
          return ext_it->second.array_element_struct;
      }
      return "";
    }
    default:
      return "";
  }
}

static AllocFlavor expr_flavor(Expr* expr, SemaContext* ctx) {
  if (!expr || !ctx) return AllocFlavor::Unknown;
  switch (expr->kind) {
    case Expr::Kind::StackAlloc: return AllocFlavor::StackSingle;
    case Expr::Kind::HeapAlloc: return AllocFlavor::HeapSingle;
    case Expr::Kind::StackArray: return AllocFlavor::StackArrayElementsPtr;
    case Expr::Kind::HeapArray: return AllocFlavor::HeapArrayElementsPtr;
    case Expr::Kind::AsHeap: return AllocFlavor::HeapSingle;  /* trust-me cast */
    case Expr::Kind::AsArray: return AllocFlavor::HeapArrayElementsPtr;  /* trust-me cast */
    case Expr::Kind::VarRef: return var_flavor_lookup(ctx, expr->var_name);
    default: return AllocFlavor::Unknown;
  }
}

static PtrBase expr_base(Expr* expr, SemaContext* ctx) {
  if (!expr || !ctx) return PtrBase::Unknown;
  switch (expr->kind) {
    case Expr::Kind::StackAlloc:
    case Expr::Kind::StackArray: return PtrBase::StackLocal;
    case Expr::Kind::HeapAlloc:
    case Expr::Kind::HeapArray: return PtrBase::Heap;
    case Expr::Kind::VarRef: return var_base_lookup(ctx, expr->var_name);
    case Expr::Kind::AsHeap:
    case Expr::Kind::AsArray: return expr_base(expr->left.get(), ctx);  /* base unchanged */
    default: return PtrBase::Unknown;
  }
}

static bool is_alloc_type(const std::string& name, Program* program) {
  if (name == "i8" || name == "i32" || name == "i64" || name == "f32" || name == "f64" || name == "ptr")
    return true;
  if (name.size() > 5 && name.substr(0,4) == "ptr[" && name.back() == ']') {
    const std::string inner = name.substr(4, name.size()-5);
    if (inner == "char") return true;  // ptr[char] = array of string pointers
    if (program)
      for (const auto& s : program->struct_defs)
        if (s.name == inner) return true;
    return false;
  }
  if (program)
    for (const auto& s : program->struct_defs)
      if (s.name == name) return true;
  return false;
}

/* Returns element type if expr is an array (ptr from stack_array/heap_array or VarRef to such); otherwise FfiType::Void. */
static FfiType get_array_element_type(Expr* expr, SemaContext* ctx) {
  if (!expr || !ctx) return FfiType::Void;
  if (expr->kind == Expr::Kind::VarRef) {
    return array_elem_lookup(ctx, expr->var_name);
  }
  if (expr->kind == Expr::Kind::FieldAccess && expr->field_chain.size() == 1 && ctx->layout_map) {
    std::string cur = expr_struct_name(expr->left.get(), ctx);
    if (cur.empty()) return FfiType::Void;
    auto it = ctx->layout_map->find(cur);
    if (it == ctx->layout_map->end()) return FfiType::Void;
    for (const auto& f : it->second.fields) {
      if (f.first == expr->field_chain[0])
        return f.second.type;
    }
    return FfiType::Void;
  }
  if (expr->kind == Expr::Kind::StackArray || expr->kind == Expr::Kind::HeapArray) {
    const std::string& t = expr->var_name;
    if (t == "i8") return FfiType::I8;
    if (t == "i32") return FfiType::I32;
    if (t == "i64") return FfiType::I64;
    if (t == "f32") return FfiType::F32;
    if (t == "f64") return FfiType::F64;
    if (t == "ptr" || (t.size() > 4 && t.substr(0,4) == "ptr[")) return FfiType::Ptr;
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
  if (expr->line > 0) {
    ctx.err->line = expr->line;
    ctx.err->column = expr->column;
  }
  switch (expr->kind) {
    case Expr::Kind::IntLiteral:
      return true;
    case Expr::Kind::FloatLiteral:
      return true;
    case Expr::Kind::StringLiteral:
      expr->inferred_ptr_element = "char";
      return true;
    case Expr::Kind::BinaryOp: {
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (!check_expr(expr->right.get(), ctx)) return false;
      FfiType l = expr_type(expr->left.get(), &ctx);
      FfiType r = expr_type(expr->right.get(), &ctx);
      bool both_numeric = (l == FfiType::I64 || l == FfiType::I32 || l == FfiType::F64) && (r == FfiType::I64 || r == FfiType::I32 || r == FfiType::F64);
      bool both_ptr = (l == FfiType::Ptr && r == FfiType::Ptr);
      if (expr->bin_op == BinOp::Add) {
        if (both_ptr) {
          if (!expr->left->inferred_ptr_element.empty() &&
              expr->left->inferred_ptr_element == expr->right->inferred_ptr_element)
            expr->inferred_ptr_element = expr->left->inferred_ptr_element;
          return true;
        }
        if (both_numeric) return true;
        if (l == FfiType::Ptr || r == FfiType::Ptr) {
          ctx.err->message = "operator +: strings (pointers) can only be added to strings";
          return false;
        }
        ctx.err->message = "operator +: operands must be numbers or both strings";
        return false;
      }
      /* Sub, Mul, Div: require numeric */
      if (!both_numeric) {
        ctx.err->message = "operator - (or * or /): operands must be numbers";
        return false;
      }
      return true;
    }
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
          if (arg_ty == FfiType::Ptr && is_stack_ptr(expr_flavor(expr->args[j + 1].get(), &ctx))) {
            ctx.err->message = "cannot pass stack pointer to indirect call (unknown callee)";
            return false;
          }
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
        expr->inferred_ptr_element = "char";
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
        expr->inferred_ptr_element = "char";
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
        expr->inferred_ptr_element = "char";
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
      if (expr->callee == "write_bytes" || expr->callee == "read_bytes") {
        if (expr->args.size() != 3) {
          ctx.err->message = expr->callee + " expects (handle, buffer, count)";
          return false;
        }
        if (!check_expr(expr->args[0].get(), ctx) || !check_expr(expr->args[1].get(), ctx) || !check_expr(expr->args[2].get(), ctx)) return false;
        if (expr_type(expr->args[0].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = std::string(expr->callee) + " first argument must be pointer (file handle)";
          return false;
        }
        if (expr_type(expr->args[1].get(), &ctx) != FfiType::Ptr) {
          ctx.err->message = std::string(expr->callee) + " second argument must be pointer (buffer)";
          return false;
        }
        if (expr_type(expr->args[2].get(), &ctx) != FfiType::I64) {
          ctx.err->message = std::string(expr->callee) + " third argument must be i64 (byte count)";
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
          if (arg_ty == FfiType::Ptr && is_stack_ptr(expr_flavor(expr->args[j].get(), &ctx))) {
            ctx.err->message = "cannot pass stack pointer to extern function '" + expr->callee + "'";
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
          bool noescape = (j < def.param_noescape.size() && def.param_noescape[j]);
          if (arg_ty == FfiType::Ptr && is_stack_ptr(expr_flavor(expr->args[j].get(), &ctx)) && !noescape) {
            ctx.err->message = "cannot pass stack pointer to '" + expr->callee + "' (param not noescape)";
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
      if (ty == FfiType::Ptr) {
        std::string pe = var_ptr_element_lookup(&ctx, expr->var_name);
        if (!pe.empty()) expr->inferred_ptr_element = pe;
      }
      return true;
    }
    case Expr::Kind::StackAlloc:
    case Expr::Kind::HeapAlloc:
      if (!is_alloc_type(expr->var_name, ctx.program)) {
        ctx.err->message = "stack/heap: unknown type '" + expr->var_name + "'";
        return false;
      }
      return true;
    case Expr::Kind::StackArray:
    case Expr::Kind::HeapArray:
      if (!expr->left) return false;
      if (!is_alloc_type(expr->var_name, ctx.program)) {
        ctx.err->message = "stack_array/heap_array: unknown element type '" + expr->var_name + "'";
        return false;
      }
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::I64) {
        ctx.err->message = "stack_array/heap_array: count must be i64";
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
    case Expr::Kind::Free: {
      if (!expr->left) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "free: argument must be a pointer";
        return false;
      }
      AllocFlavor f = expr_flavor(expr->left.get(), &ctx);
      if (f == AllocFlavor::HeapSingle) return true;
      if (f == AllocFlavor::StackSingle || f == AllocFlavor::StackArrayElementsPtr ||
          f == AllocFlavor::HeapArrayElementsPtr) {
        ctx.err->message = "free: use free_array for array allocations; cannot free stack allocation";
        return false;
      }
      ctx.err->message = "free: unknown pointer origin; use as_heap(ptr) to assert heap allocation";
      return false;
    }
    case Expr::Kind::FreeArray: {
      if (!expr->left) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "free_array: argument must be a pointer";
        return false;
      }
      AllocFlavor fa = expr_flavor(expr->left.get(), &ctx);
      if (fa == AllocFlavor::HeapArrayElementsPtr) return true;
      if (fa == AllocFlavor::HeapSingle || fa == AllocFlavor::StackSingle ||
          fa == AllocFlavor::StackArrayElementsPtr) {
        ctx.err->message = "free_array: use free for single allocations; cannot free stack allocation";
        return false;
      }
      ctx.err->message = "free_array: unknown pointer origin; use as_array(ptr, T) to assert array allocation";
      return false;
    }
    case Expr::Kind::AsHeap:
    case Expr::Kind::AsArray:
      if (!expr->left) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "as_heap/as_array: argument must be a pointer";
        return false;
      }
      if (expr->kind == Expr::Kind::AsArray && !is_alloc_type(expr->var_name, ctx.program)) {
        ctx.err->message = "as_array: unknown element type '" + expr->var_name + "'";
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
      if (expr_type(expr->right.get(), &ctx) == FfiType::Ptr &&
          is_stack_ptr(expr_flavor(expr->right.get(), &ctx)) &&
          may_outlive_function(expr_base(expr->left.get(), &ctx))) {
        ctx.err->message = "store: cannot store stack pointer into memory that may outlive function";
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
      if (field_ty == FfiType::Ptr &&
          is_stack_ptr(expr_flavor(expr->right.get(), &ctx)) &&
          may_outlive_function(expr_base(expr->left.get(), &ctx))) {
        ctx.err->message = "store_field: cannot store stack pointer into memory that may outlive function";
        return false;
      }
      return true;
    }
    case Expr::Kind::FieldAccess: {
      if (!expr->left || expr->field_chain.empty()) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      if (expr_type(expr->left.get(), &ctx) != FfiType::Ptr) {
        ctx.err->message = "field access: base must be a pointer";
        return false;
      }
      if (!ctx.layout_map) return false;
      std::string struct_name = expr_struct_name(expr->left.get(), &ctx);
      if (struct_name.empty() && expr->left->kind == Expr::Kind::VarRef) {
        std::string pe = var_ptr_element_lookup(&ctx, expr->left->var_name);
        if (!pe.empty() && ctx.program && is_named_type_known(pe, ctx.program))
          struct_name = pe;
      }
      if (struct_name.empty()) {
        ctx.err->message = "field access: cannot determine struct type of base expression";
        return false;
      }
      // Annotate for codegen
      expr->load_field_struct = struct_name;
      std::string cur = struct_name;
      for (size_t fi = 0; fi < expr->field_chain.size(); ++fi) {
        auto it = ctx.layout_map->find(cur);
        if (it == ctx.layout_map->end()) {
          ctx.err->message = "field access: unknown struct '" + cur + "'";
          return false;
        }
        const std::string& field = expr->field_chain[fi];
        bool found = false;
        for (const auto& f : it->second.fields) {
          if (f.first == field) {
            if (fi + 1 < expr->field_chain.size()) {
              if (f.second.struct_name.empty()) {
                ctx.err->message = "field access: intermediate field '" + field +
                    "' is not an embedded struct in '" + cur + "'";
                return false;
              }
              cur = f.second.struct_name;
            }
            found = true;
            break;
          }
        }
        if (!found) {
          ctx.err->message = "field access: unknown field '" + field + "' in struct '" + cur + "'";
          return false;
        }
      }
      return true;
    }
    case Expr::Kind::Cast: {
      if (!expr->left || expr->var_name.empty()) return false;
      if (!check_expr(expr->left.get(), ctx)) return false;
      FfiType from = expr_type(expr->left.get(), &ctx);
      if (expr->var_name == "ptr" || expr->var_name == "char") {
        if (from != FfiType::Ptr) {
          ctx.err->message = "cast to ptr: operand must be a pointer";
          return false;
        }
        if (expr->var_name == "char") expr->inferred_ptr_element = "char";
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
      /* Cast to struct: ptr -> struct* (reinterpret) */
      if (ctx.program) {
        for (const auto& s : ctx.program->struct_defs) {
          if (s.name == expr->var_name) {
            if (from != FfiType::Ptr) {
              ctx.err->message = "cast to struct: operand must be a pointer";
              return false;
            }
            return true;
          }
        }
      }
      ctx.err->message = "cast: target type must be ptr[void], ptr[char], i64, i32, f64, f32, or a struct name";
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
      if (l == FfiType::Ptr && r == FfiType::Ptr && expr->bin_op == BinOp::Add)
        return FfiType::Ptr;
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
      if (expr->callee == "write_bytes" || expr->callee == "read_bytes") return FfiType::I64;
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
    case Expr::Kind::StackAlloc:
    case Expr::Kind::HeapAlloc:
    case Expr::Kind::StackArray:
    case Expr::Kind::HeapArray:
      return FfiType::Ptr;
    case Expr::Kind::Free:
    case Expr::Kind::FreeArray:
      return FfiType::Void;
    case Expr::Kind::AsHeap:
    case Expr::Kind::AsArray:
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
    case Expr::Kind::FieldAccess: {
      if (!ctx || !ctx->layout_map || expr->field_chain.empty()) return FfiType::Void;
      // Use annotated struct name if available, else derive
      std::string struct_name = expr->load_field_struct;
      if (struct_name.empty()) struct_name = expr_struct_name(expr->left.get(), ctx);
      if (struct_name.empty()) return FfiType::Void;
      std::string cur = struct_name;
      for (size_t fi = 0; fi < expr->field_chain.size(); ++fi) {
        auto it = ctx->layout_map->find(cur);
        if (it == ctx->layout_map->end()) return FfiType::Void;
        const std::string& field = expr->field_chain[fi];
        bool found = false;
        for (const auto& f : it->second.fields) {
          if (f.first == field) {
            if (fi + 1 < expr->field_chain.size()) {
              cur = f.second.struct_name;
            } else {
              // Last field
              return f.second.struct_name.empty() ? f.second.type : FfiType::Ptr;
            }
            found = true;
            break;
          }
        }
        if (!found) return FfiType::Void;
      }
      return FfiType::Void;
    }
    case Expr::Kind::Cast:
      if (expr->var_name == "ptr" || expr->var_name == "char") return FfiType::Ptr;
      if (expr->var_name == "i64") return FfiType::I64;
      if (expr->var_name == "i32") return FfiType::I32;
      if (expr->var_name == "f64") return FfiType::F64;
      if (expr->var_name == "f32") return FfiType::F32;
      if (ctx && ctx->program) {
        for (const auto& s : ctx->program->struct_defs)
          if (s.name == expr->var_name) return FfiType::Ptr;
      }
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
  if (name == "char") return true;  // char is valid as ptr element type
  for (const std::string& o : program->opaque_types)
    if (o == name) return true;
  for (const auto& s : program->struct_defs)
    if (s.name == name) return true;
  return false;
}

/* Valid for ptr[T] return / array element: primitives (i8, i32, i64, f32, f64, char) or known struct/opaque. */
static bool is_valid_array_element_type(const std::string& name, Program* program) {
  if (name == "char" || name == "i8" || name == "i32" || name == "i64" || name == "f32" || name == "f64")
    return true;
  return program && is_named_type_known(name, program);
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
      if (def->return_type == FfiType::Ptr && is_stack_ptr(expr_flavor(stmt->expr.get(), &ctx))) {
        ctx.err->message = "cannot return stack pointer (stack allocation escapes)";
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
      AllocFlavor let_flavor = expr_flavor(stmt->init.get(), &ctx);
      PtrBase let_base = (let_ty == FfiType::Ptr) ? expr_base(stmt->init.get(), &ctx) : PtrBase::Unknown;
      if (!ctx.var_flavor_scope_stack.empty())
        ctx.var_flavor_scope_stack.back()[stmt->name] = let_flavor;
      if (!ctx.var_base_scope_stack.empty())
        ctx.var_base_scope_stack.back()[stmt->name] = let_base;
      if (let_ty == FfiType::Ptr && !ctx.fnptr_scope_stack.empty()) {
        FnPtrSig sig;
        if (lookup_fnptr_sig(&ctx, stmt->init.get(), &sig))
          ctx.fnptr_scope_stack.back()[stmt->name] = sig;
      }
      // Track ptr-to-struct
      if (let_ty == FfiType::Ptr && !ctx.var_struct_scope_stack.empty()) {
        std::string sname = expr_struct_name(stmt->init.get(), &ctx);
        if (sname.empty() && stmt->init->kind == Expr::Kind::Index && stmt->init->left &&
            stmt->init->left->kind == Expr::Kind::VarRef)
          sname = array_struct_lookup(&ctx, stmt->init->left->var_name);
        if (sname.empty() && stmt->init->kind == Expr::Kind::Index && stmt->init->left &&
            stmt->init->left->kind == Expr::Kind::FieldAccess && ctx.layout_map) {
          Expr* fa = stmt->init->left.get();
          std::string base_struct = expr_struct_name(fa->left.get(), &ctx);
          if (base_struct.empty() && fa->left->kind == Expr::Kind::VarRef)
            base_struct = var_ptr_element_lookup(&ctx, fa->left->var_name);
          if (!base_struct.empty() && ctx.program && is_named_type_known(base_struct, ctx.program)) {
            for (const std::string& fname : fa->field_chain) {
              auto it = ctx.layout_map->find(base_struct);
              if (it == ctx.layout_map->end()) break;
              for (const auto& f : it->second.fields) {
                if (f.first == fname) { base_struct = f.second.struct_name; break; }
              }
            }
            if (!base_struct.empty()) sname = base_struct;
          }
        }
        if (!sname.empty()) {
          ctx.var_struct_scope_stack.back()[stmt->name] = sname;
          if (stmt->init->kind == Expr::Kind::FieldAccess && !ctx.array_struct_scope_stack.empty())
            ctx.array_struct_scope_stack.back()[stmt->name] = sname;
        }
      }
      FfiType elem_ty = get_array_element_type(stmt->init.get(), &ctx);
      if (elem_ty != FfiType::Void) {
        ctx.array_element_scope_stack.back()[stmt->name] = elem_ty;
      } else if (let_ty == FfiType::Ptr && stmt->init->kind == Expr::Kind::FieldAccess) {
        Expr* e = stmt->init.get();
        if (!e->field_chain.empty() && !e->load_field_struct.empty() && ctx.layout_map) {
          std::string cur = e->load_field_struct;
          for (size_t fi = 0; fi < e->field_chain.size(); ++fi) {
            auto it = ctx.layout_map->find(cur);
            if (it == ctx.layout_map->end()) break;
            bool found = false;
            for (const auto& f : it->second.fields) {
              if (f.first == e->field_chain[fi]) {
                if (fi + 1 == e->field_chain.size() && f.second.type == FfiType::Ptr) {
                  ctx.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
                  if (!f.second.struct_name.empty() && !ctx.array_struct_scope_stack.empty())
                    ctx.array_struct_scope_stack.back()[stmt->name] = f.second.struct_name;
                }
                cur = f.second.struct_name.empty() ? cur : f.second.struct_name;
                found = true;
                break;
              }
            }
            if (!found) break;
          }
        }
      } else if (let_ty == FfiType::Ptr && stmt->init->kind == Expr::Kind::Call)
        ctx.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
      // Track array element struct name
      if (!ctx.array_struct_scope_stack.empty()) {
        Expr* init = stmt->init.get();
        if (init && (init->kind == Expr::Kind::HeapArray || init->kind == Expr::Kind::StackArray) && ctx.program) {
          const std::string& t = init->var_name;
          if (t.size() > 5 && t.substr(0,4) == "ptr[" && t.back() == ']')
            ctx.array_struct_scope_stack.back()[stmt->name] = t.substr(4, t.size()-5);
          else
            for (const auto& s : ctx.program->struct_defs)
              if (s.name == t) {
                ctx.array_struct_scope_stack.back()[stmt->name] = t;
                break;
              }
        } else if (init && init->kind == Expr::Kind::Call && let_ty == FfiType::Ptr) {
          std::string elem_struct = get_call_array_element_struct(init, &ctx);
          if (!elem_struct.empty())
            ctx.array_struct_scope_stack.back()[stmt->name] = elem_struct;
        }
      }
      // Track ptr element type for this let binding
      if (let_ty == FfiType::Ptr && !ctx.var_ptr_element_scope_stack.empty()) {
        Expr* init = stmt->init.get();
        if (init && !init->inferred_ptr_element.empty())
          ctx.var_ptr_element_scope_stack.back()[stmt->name] = init->inferred_ptr_element;
      }
      return true;
    }
    case Stmt::Kind::Expr:
      return check_expr(stmt->expr.get(), ctx);
    case Stmt::Kind::If:
      if (!check_expr(stmt->cond.get(), ctx)) return false;
      ctx.var_scope_stack.push_back({});
      ctx.array_element_scope_stack.push_back({});
      ctx.fnptr_scope_stack.push_back({});
      ctx.var_flavor_scope_stack.push_back({});
      ctx.var_base_scope_stack.push_back({});
      ctx.var_struct_scope_stack.push_back({});
      ctx.array_struct_scope_stack.push_back({});
      ctx.var_ptr_element_scope_stack.push_back({});
      for (StmtPtr& s : stmt->then_body)
        if (!check_stmt(ctx, def, s.get())) {
          ctx.var_scope_stack.pop_back();
          ctx.array_element_scope_stack.pop_back();
          ctx.fnptr_scope_stack.pop_back();
          ctx.var_flavor_scope_stack.pop_back();
          ctx.var_base_scope_stack.pop_back();
          ctx.var_struct_scope_stack.pop_back();
          ctx.array_struct_scope_stack.pop_back();
          ctx.var_ptr_element_scope_stack.pop_back();
          return false;
        }
      ctx.var_scope_stack.pop_back();
      ctx.array_element_scope_stack.pop_back();
      ctx.fnptr_scope_stack.pop_back();
      ctx.var_flavor_scope_stack.pop_back();
      ctx.var_base_scope_stack.pop_back();
      ctx.var_struct_scope_stack.pop_back();
      ctx.array_struct_scope_stack.pop_back();
      ctx.var_ptr_element_scope_stack.pop_back();
      if (stmt->else_body.empty()) return true;
      ctx.var_scope_stack.push_back({});
      ctx.array_element_scope_stack.push_back({});
      ctx.fnptr_scope_stack.push_back({});
      ctx.var_flavor_scope_stack.push_back({});
      ctx.var_base_scope_stack.push_back({});
      ctx.var_struct_scope_stack.push_back({});
      ctx.array_struct_scope_stack.push_back({});
      ctx.var_ptr_element_scope_stack.push_back({});
      for (StmtPtr& s : stmt->else_body)
        if (!check_stmt(ctx, def, s.get())) {
          ctx.var_scope_stack.pop_back();
          ctx.array_element_scope_stack.pop_back();
          ctx.fnptr_scope_stack.pop_back();
          ctx.var_flavor_scope_stack.pop_back();
          ctx.var_base_scope_stack.pop_back();
          ctx.var_struct_scope_stack.pop_back();
          ctx.array_struct_scope_stack.pop_back();
          ctx.var_ptr_element_scope_stack.pop_back();
          return false;
        }
      ctx.var_scope_stack.pop_back();
      ctx.array_element_scope_stack.pop_back();
      ctx.fnptr_scope_stack.pop_back();
      ctx.var_flavor_scope_stack.pop_back();
      ctx.var_base_scope_stack.pop_back();
      ctx.var_struct_scope_stack.pop_back();
      ctx.array_struct_scope_stack.pop_back();
      ctx.var_ptr_element_scope_stack.pop_back();
      return true;
    case Stmt::Kind::For: {
      if (!stmt->cond) return false;
      ctx.var_scope_stack.push_back({});
      ctx.array_element_scope_stack.push_back({});
      ctx.fnptr_scope_stack.push_back({});
      ctx.var_flavor_scope_stack.push_back({});
      ctx.var_base_scope_stack.push_back({});
      ctx.var_struct_scope_stack.push_back({});
      ctx.array_struct_scope_stack.push_back({});
      ctx.var_ptr_element_scope_stack.push_back({});
      if (stmt->for_init) {
        if (stmt->for_init->kind == Stmt::Kind::Let) {
          if (!check_expr(stmt->for_init->init.get(), ctx)) {
            ctx.var_scope_stack.pop_back();
            ctx.array_element_scope_stack.pop_back();
            ctx.fnptr_scope_stack.pop_back();
            ctx.var_flavor_scope_stack.pop_back();
            ctx.var_base_scope_stack.pop_back();
            ctx.var_struct_scope_stack.pop_back();
            ctx.array_struct_scope_stack.pop_back();
            ctx.var_ptr_element_scope_stack.pop_back();
            return false;
          }
          if (ctx.var_scope_stack.back().count(stmt->for_init->name)) {
            ctx.err->message = def
              ? "duplicate variable '" + stmt->for_init->name + "' in function '" + def->name + "'"
              : "duplicate variable '" + stmt->for_init->name + "'";
            ctx.var_scope_stack.pop_back();
            ctx.array_element_scope_stack.pop_back();
            ctx.fnptr_scope_stack.pop_back();
            ctx.var_flavor_scope_stack.pop_back();
            ctx.var_base_scope_stack.pop_back();
            ctx.var_struct_scope_stack.pop_back();
            ctx.array_struct_scope_stack.pop_back();
            ctx.var_ptr_element_scope_stack.pop_back();
            return false;
          }
          ctx.var_scope_stack.back()[stmt->for_init->name] = expr_type(stmt->for_init->init.get(), &ctx);
          AllocFlavor init_flavor = expr_flavor(stmt->for_init->init.get(), &ctx);
          PtrBase init_base = (expr_type(stmt->for_init->init.get(), &ctx) == FfiType::Ptr)
            ? expr_base(stmt->for_init->init.get(), &ctx) : PtrBase::Unknown;
          ctx.var_flavor_scope_stack.back()[stmt->for_init->name] = init_flavor;
          ctx.var_base_scope_stack.back()[stmt->for_init->name] = init_base;
        } else if (stmt->for_init->kind == Stmt::Kind::Assign) {
          if (!check_stmt(ctx, def, stmt->for_init.get())) {
            ctx.var_scope_stack.pop_back();
            ctx.array_element_scope_stack.pop_back();
            ctx.fnptr_scope_stack.pop_back();
            ctx.var_flavor_scope_stack.pop_back();
            ctx.var_base_scope_stack.pop_back();
            ctx.var_struct_scope_stack.pop_back();
            ctx.array_struct_scope_stack.pop_back();
            ctx.var_ptr_element_scope_stack.pop_back();
            return false;
          }
        }
      }
      if (!check_expr(stmt->cond.get(), ctx)) {
        ctx.var_scope_stack.pop_back();
        ctx.array_element_scope_stack.pop_back();
        ctx.fnptr_scope_stack.pop_back();
        ctx.var_flavor_scope_stack.pop_back();
        ctx.var_base_scope_stack.pop_back();
        ctx.var_struct_scope_stack.pop_back();
        ctx.array_struct_scope_stack.pop_back();
        ctx.var_ptr_element_scope_stack.pop_back();
        return false;
      }
      if (stmt->for_update) {
        if (stmt->for_update->kind != Stmt::Kind::Assign || !check_stmt(ctx, def, stmt->for_update.get())) {
          ctx.var_scope_stack.pop_back();
          ctx.array_element_scope_stack.pop_back();
          ctx.fnptr_scope_stack.pop_back();
          ctx.var_flavor_scope_stack.pop_back();
          ctx.var_base_scope_stack.pop_back();
          ctx.var_struct_scope_stack.pop_back();
          ctx.array_struct_scope_stack.pop_back();
          ctx.var_ptr_element_scope_stack.pop_back();
          return false;
        }
      }
      for (StmtPtr& s : stmt->body)
        if (!check_stmt(ctx, def, s.get())) {
          ctx.var_scope_stack.pop_back();
          ctx.array_element_scope_stack.pop_back();
          ctx.fnptr_scope_stack.pop_back();
          ctx.var_flavor_scope_stack.pop_back();
          ctx.var_base_scope_stack.pop_back();
          ctx.var_struct_scope_stack.pop_back();
          ctx.array_struct_scope_stack.pop_back();
          ctx.var_ptr_element_scope_stack.pop_back();
          return false;
        }
      ctx.var_scope_stack.pop_back();
      ctx.array_element_scope_stack.pop_back();
      ctx.fnptr_scope_stack.pop_back();
      ctx.var_flavor_scope_stack.pop_back();
      ctx.var_base_scope_stack.pop_back();
      ctx.var_struct_scope_stack.pop_back();
      ctx.array_struct_scope_stack.pop_back();
      ctx.var_ptr_element_scope_stack.pop_back();
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
        if (var_ty == FfiType::Ptr && val_ty == FfiType::Ptr) {
          AllocFlavor val_flavor = expr_flavor(stmt->init.get(), &ctx);
          PtrBase val_base = expr_base(stmt->init.get(), &ctx);
          update_var_flavor_base(&ctx, stmt->expr->var_name, val_flavor, val_base);
          if (!ctx.fnptr_scope_stack.empty()) {
            FnPtrSig sig;
            if (lookup_fnptr_sig(&ctx, stmt->init.get(), &sig))
              ctx.fnptr_scope_stack.back()[stmt->expr->var_name] = sig;
          }
        }
        return true;
      }
      if (stmt->expr->kind == Expr::Kind::Index) {
        FfiType elem_ty = get_array_element_type(stmt->expr->left.get(), &ctx);
        if (elem_ty == FfiType::Void) elem_ty = FfiType::I64;
        FfiType val_ty = expr_type(stmt->init.get(), &ctx);
        bool compat = (elem_ty == val_ty) ||
          (elem_ty == FfiType::Ptr && val_ty == FfiType::I64) ||
          (elem_ty == FfiType::I64 && val_ty == FfiType::Ptr) ||
          (elem_ty == FfiType::I8 && val_ty == FfiType::I64);
        if (!compat) {
          ctx.err->message = "assignment type mismatch for array element";
          return false;
        }
        if (elem_ty == FfiType::Ptr && val_ty == FfiType::Ptr &&
            is_stack_ptr(expr_flavor(stmt->init.get(), &ctx)) &&
            may_outlive_function(expr_base(stmt->expr->left.get(), &ctx))) {
          ctx.err->message = "cannot store stack pointer into array that may outlive function";
          return false;
        }
        return true;
      }
      if (stmt->expr->kind == Expr::Kind::FieldAccess) {
        FfiType field_ty = expr_type(stmt->expr.get(), &ctx);
        FfiType val_ty = expr_type(stmt->init.get(), &ctx);
        bool compat = (field_ty == val_ty) ||
          (field_ty == FfiType::Ptr && val_ty == FfiType::I64) ||
          (field_ty == FfiType::I64 && val_ty == FfiType::Ptr) ||
          (field_ty == FfiType::F64 && val_ty == FfiType::I64) ||
          (field_ty == FfiType::I64 && val_ty == FfiType::F64);
        if (!compat) {
          ctx.err->message = "field assignment: value type does not match field type";
          return false;
        }
        if (field_ty == FfiType::Ptr && val_ty == FfiType::Ptr &&
            is_stack_ptr(expr_flavor(stmt->init.get(), &ctx)) &&
            may_outlive_function(expr_base(stmt->expr->left.get(), &ctx))) {
          ctx.err->message = "cannot store stack pointer into memory that may outlive function";
          return false;
        }
        return true;
      }
      ctx.err->message = "assignment target must be a variable, index, or field access";
      return false;
    }
  }
  return false;
}

static bool check_fn_def(SemaContext& ctx, FnDef& def) {
  if (!def.array_element_struct.empty() && !is_valid_array_element_type(def.array_element_struct, ctx.program)) {
    ctx.err->message = "unknown array element struct '" + def.array_element_struct + "' in fn '" + def.name + "'";
    return false;
  }
  std::unordered_map<std::string, FfiType> local;
  std::unordered_map<std::string, FfiType> array_local;
  std::unordered_map<std::string, AllocFlavor> param_flavor;
  std::unordered_map<std::string, PtrBase> param_base;
  for (size_t j = 0; j < def.params.size(); ++j) {
    local[def.params[j].first] = def.params[j].second;
    param_flavor[def.params[j].first] = AllocFlavor::Unknown;
    param_base[def.params[j].first] = PtrBase::Param;
  }
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
  fn_ctx.var_flavor_scope_stack.push_back(std::move(param_flavor));
  fn_ctx.var_base_scope_stack.push_back(std::move(param_base));
  std::unordered_map<std::string, std::string> param_struct;
  if (ctx.program) {
    for (size_t j = 0; j < def.params.size() && j < def.param_type_names.size(); ++j) {
      if (!def.param_type_names[j].empty() &&
          is_named_type_known(def.param_type_names[j], ctx.program))
        param_struct[def.params[j].first] = def.param_type_names[j];
    }
  }
  fn_ctx.var_struct_scope_stack.push_back(std::move(param_struct));
  fn_ctx.array_struct_scope_stack.push_back({});
  std::unordered_map<std::string, std::string> param_ptr_element;
  for (size_t j = 0; j < def.params.size() && j < def.param_type_names.size(); ++j) {
    if (!def.param_type_names[j].empty())
      param_ptr_element[def.params[j].first] = def.param_type_names[j];
  }
  fn_ctx.var_ptr_element_scope_stack.push_back(std::move(param_ptr_element));
  for (StmtPtr& stmt : def.body) {
    if (!check_stmt(fn_ctx, &def, stmt.get())) return false;
  }
  return true;
}

SemaResult check(Program* program) {
  SemaResult r;
  if (!program) {
    r.error.message = "null program";
    r.errors.push_back(r.error);
    return r;
  }
  if (!program->extern_fns.empty() && program->libs.empty()) {
    r.error.message = "at least one extern lib required when declaring extern fn";
    r.errors.push_back(r.error);
    return r;
  }
  std::unordered_set<std::string> lib_names;
  for (const ExternLib& lib : program->libs)
    lib_names.insert(lib.name);
  for (const ExternFn& ext : program->extern_fns) {
    if (lib_names.find(ext.lib_name) == lib_names.end()) {
      r.error.message = "extern fn '" + ext.name + "' references unknown lib '" + ext.lib_name + "'";
      r.errors.push_back(r.error);
      return r;
    }
    bool param_names_ok = (ext.param_type_names.size() == ext.params.size());
    if (param_names_ok) {
      for (size_t j = 0; j < ext.param_type_names.size(); ++j) {
        if (!ext.param_type_names[j].empty() && !is_named_type_known(ext.param_type_names[j], program)) {
          r.error.message = "unknown type '" + ext.param_type_names[j] + "' in extern fn '" + ext.name + "'";
          r.errors.push_back(r.error);
          return r;
        }
      }
    }
    if (!ext.return_type_name.empty() && !is_named_type_known(ext.return_type_name, program)) {
      r.error.message = "unknown return type '" + ext.return_type_name + "' in extern fn '" + ext.name + "'";
      r.errors.push_back(r.error);
      return r;
    }
    if (!ext.array_element_struct.empty() && !is_valid_array_element_type(ext.array_element_struct, program)) {
      r.error.message = "unknown array element struct '" + ext.array_element_struct + "' in extern fn '" + ext.name + "'";
      r.errors.push_back(r.error);
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
      r.errors.push_back(r.error);
      return r;
    }
    if (ctx.user_fn_by_name.count(def.name)) {
      r.error.message = "duplicate function definition '" + def.name + "'";
      r.errors.push_back(r.error);
      return r;
    }
    ctx.user_fn_by_name[def.name] = &def;
  }
  for (FnDef& def : program->user_fns) {
    if (!check_fn_def(ctx, def)) {
      r.errors.push_back(r.error);
      r.error = {};
    }
  }
  ctx.var_scope_stack.push_back({});
  ctx.array_element_scope_stack.push_back({});
  ctx.fnptr_scope_stack.push_back({});
  ctx.var_flavor_scope_stack.push_back({});
  ctx.var_base_scope_stack.push_back({});
  ctx.var_struct_scope_stack.push_back({});
  ctx.array_struct_scope_stack.push_back({});
  ctx.var_ptr_element_scope_stack.push_back({});
  for (const TopLevelItem& item : program->top_level) {
    if (const LetBinding* binding = std::get_if<LetBinding>(&item)) {
      if (!check_expr(binding->init.get(), ctx)) {
        r.errors.push_back(r.error);
        r.error = r.errors[0];
        return r;
      }
      FfiType ty = expr_type(binding->init.get(), &ctx);
      if (ctx.var_scope_stack.back().count(binding->name)) {
        ctx.err->message = "duplicate variable '" + binding->name + "'";
        r.errors.push_back(r.error);
        r.error = r.errors[0];
        return r;
      }
      ctx.var_scope_stack.back()[binding->name] = ty;
      AllocFlavor bind_flavor = expr_flavor(binding->init.get(), &ctx);
      PtrBase bind_base = (ty == FfiType::Ptr) ? expr_base(binding->init.get(), &ctx) : PtrBase::Unknown;
      ctx.var_flavor_scope_stack.back()[binding->name] = bind_flavor;
      ctx.var_base_scope_stack.back()[binding->name] = bind_base;
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
      else if (ty == FfiType::Ptr && binding->init->kind == Expr::Kind::FieldAccess) {
        Expr* e = binding->init.get();
        if (!e->field_chain.empty() && !e->load_field_struct.empty() && ctx.layout_map) {
          std::string cur = e->load_field_struct;
          for (size_t fi = 0; fi < e->field_chain.size(); ++fi) {
            auto it = ctx.layout_map->find(cur);
            if (it == ctx.layout_map->end()) break;
            bool found = false;
            for (const auto& f : it->second.fields) {
              if (f.first == e->field_chain[fi]) {
                if (fi + 1 == e->field_chain.size() && f.second.type == FfiType::Ptr) {
                  ctx.array_element_scope_stack.back()[binding->name] = FfiType::Ptr;
                  if (!f.second.struct_name.empty() && !ctx.array_struct_scope_stack.empty())
                    ctx.array_struct_scope_stack.back()[binding->name] = f.second.struct_name;
                }
                cur = f.second.struct_name.empty() ? cur : f.second.struct_name;
                found = true;
                break;
              }
            }
            if (!found) break;
          }
        }
      }
      // Track ptr-to-struct for top-level bindings
      if (ty == FfiType::Ptr) {
        std::string sname = expr_struct_name(binding->init.get(), &ctx);
        if (sname.empty() && binding->init->kind == Expr::Kind::Index && binding->init->left &&
            binding->init->left->kind == Expr::Kind::VarRef)
          sname = array_struct_lookup(&ctx, binding->init->left->var_name);
        if (!sname.empty())
          ctx.var_struct_scope_stack.back()[binding->name] = sname;
      }
      // Track ptr element type for top-level bindings
      if (ty == FfiType::Ptr && !ctx.var_ptr_element_scope_stack.empty()) {
        Expr* init = binding->init.get();
        if (init && !init->inferred_ptr_element.empty())
          ctx.var_ptr_element_scope_stack.back()[binding->name] = init->inferred_ptr_element;
      }
      // Track array element struct name
      {
        Expr* init = binding->init.get();
        if (init && (init->kind == Expr::Kind::HeapArray || init->kind == Expr::Kind::StackArray) && ctx.program) {
          const std::string& t = init->var_name;
          if (t.size() > 5 && t.substr(0,4) == "ptr[" && t.back() == ']')
            ctx.array_struct_scope_stack.back()[binding->name] = t.substr(4, t.size()-5);
          else
            for (const auto& s : ctx.program->struct_defs)
              if (s.name == t) {
                ctx.array_struct_scope_stack.back()[binding->name] = t;
                break;
              }
        } else if (init && init->kind == Expr::Kind::Call && ty == FfiType::Ptr) {
          std::string elem_struct = get_call_array_element_struct(init, &ctx);
          if (!elem_struct.empty())
            ctx.array_struct_scope_stack.back()[binding->name] = elem_struct;
        }
      }
    } else if (const ExprPtr* expr = std::get_if<ExprPtr>(&item)) {
      if (!check_expr(expr->get(), ctx)) {
        r.errors.push_back(r.error);
        r.error = {};
      }
    } else {
      const StmtPtr& stmt = std::get<StmtPtr>(item);
      if (!check_stmt(ctx, nullptr, stmt.get())) {
        r.errors.push_back(r.error);
        r.error = {};
      }
    }
  }
  r.ok = r.errors.empty();
  if (!r.ok) r.error = r.errors[0];
  return r;
}

}  // namespace fusion
