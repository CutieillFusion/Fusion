#include "codegen.hpp"
#include "ast.hpp"
#include "layout.hpp"
#include "sema.hpp"
#include <variant>
#include <unordered_set>

#include "codegen_pch.hpp"

#ifdef FUSION_HAVE_LLVM
using namespace llvm;
using namespace llvm::orc;
#endif

namespace fusion {

#ifdef FUSION_HAVE_LLVM
static thread_local std::string s_codegen_error;
const std::string& codegen_last_error() { return s_codegen_error; }
/* Match rt_ffi_type_kind_t enum in runtime.h */
static int ffi_type_to_kind(FfiType t) {
  switch (t) {
    case FfiType::Void: return 0;
    case FfiType::I8: return 2;
    case FfiType::I32: return 1;
    case FfiType::I64: return 2;
    case FfiType::F32: return 3;
    case FfiType::F64: return 4;
    case FfiType::Ptr: return 5;
  }
  return 0;
}

static Type* ffi_type_to_llvm(FfiType t, LLVMContext& ctx, IRBuilder<>& B) {
  switch (t) {
    case FfiType::Void: return B.getVoidTy();
    case FfiType::I8: return B.getInt8Ty();
    case FfiType::I32: return B.getInt32Ty();
    case FfiType::I64: return B.getInt64Ty();
    case FfiType::F32: return B.getFloatTy();
    case FfiType::F64: return B.getDoubleTy();
    case FfiType::Ptr: return PointerType::get(Type::getInt8Ty(ctx), 0);
  }
  return B.getVoidTy();
}

static FfiType expr_type(Expr* expr, Program* program,
    const std::unordered_map<std::string, FfiType>* local_types = nullptr);

static FfiType binding_type(const LetBinding& binding, Program* program) {
  return expr_type(binding.init.get(), program, nullptr);
}

static FfiType expr_type(Expr* expr, Program* program,
    const std::unordered_map<std::string, FfiType>* local_types) {
  if (!expr) return FfiType::Void;
  switch (expr->kind) {
    case Expr::Kind::IntLiteral: return FfiType::I64;
    case Expr::Kind::FloatLiteral: return FfiType::F64;
    case Expr::Kind::StringLiteral: return FfiType::Ptr;
    case Expr::Kind::BinaryOp: {
      FfiType l = expr_type(expr->left.get(), program, local_types);
      FfiType r = expr_type(expr->right.get(), program, local_types);
      if (l == FfiType::Ptr && r == FfiType::Ptr && expr->bin_op == BinOp::Add)
        return FfiType::Ptr;
      return (l == FfiType::F64 || r == FfiType::F64) ? FfiType::F64 : FfiType::I64;
    }
    case Expr::Kind::VarRef: {
      if (local_types) {
        auto it = local_types->find(expr->var_name);
        if (it != local_types->end()) return it->second;
      }
      if (!program) return FfiType::Void;
      for (const TopLevelItem& item : program->top_level)
        if (const LetBinding* b = std::get_if<LetBinding>(&item))
          if (b->name == expr->var_name) return binding_type(*b, program);
      return FfiType::Void;
    }
    case Expr::Kind::Call:
      if (auto bt = builtin_fixed_return_type(expr->callee)) return *bt;
      if (expr->callee == "call" && program && expr->args.size() >= 1) {
        if (expr->args[0]->kind == Expr::Kind::Call && expr->args[0]->callee == "get_func_ptr" &&
            expr->args[0]->args.size() == 1 && expr->args[0]->args[0]->kind == Expr::Kind::VarRef) {
          const std::string& fn_name = expr->args[0]->args[0]->var_name;
          for (const FnDef& def : program->user_fns)
            if (def.name == fn_name) return def.return_type;
          for (const ExternFn& ext : program->extern_fns)
            if (ext.name == fn_name) return ext.return_type;
        }
      }
      if (expr->callee == "call") return FfiType::Void;
      if (expr->callee == "from_str") {
        if (expr->call_type_arg == "i64") return FfiType::I64;
        if (expr->call_type_arg == "f64") return FfiType::F64;
        return FfiType::Void;
      }
      if (program) {
        for (const ExternFn& ext : program->extern_fns)
          if (ext.name == expr->callee) return ext.return_type;
        for (const FnDef& def : program->user_fns)
          if (def.name == expr->callee) return def.return_type;
      }
      return FfiType::Void;
    case Expr::Kind::StackAlloc:
    case Expr::Kind::HeapAlloc:
    case Expr::Kind::StackArray:
    case Expr::Kind::HeapArray:
    case Expr::Kind::AsHeap:
    case Expr::Kind::AsArray:
    case Expr::Kind::AddrOf:
    case Expr::Kind::LoadPtr:
      return FfiType::Ptr;
    case Expr::Kind::Load:
    case Expr::Kind::LoadI32:
      return FfiType::I64;
    case Expr::Kind::LoadF64:
      return FfiType::F64;
    case Expr::Kind::Store:
    case Expr::Kind::StoreField:
      return FfiType::Void;
    case Expr::Kind::LoadField: {
      if (!program) return FfiType::Void;
      LayoutMap layout_map = build_layout_map(program->struct_defs);
      auto it = layout_map.find(expr->load_field_struct);
      if (it == layout_map.end()) return FfiType::Void;
      for (const auto& f : it->second.fields)
        if (f.first == expr->load_field_field) return f.second.type;
      return FfiType::Void;
    }
    case Expr::Kind::FieldAccess: {
      if (!program || expr->field_chain.empty() || expr->load_field_struct.empty()) return FfiType::Void;
      LayoutMap layout_map = build_layout_map(program->struct_defs);
      std::string cur = expr->load_field_struct;
      for (size_t fi = 0; fi < expr->field_chain.size(); ++fi) {
        auto it = layout_map.find(cur);
        if (it == layout_map.end()) return FfiType::Void;
        bool found = false;
        for (const auto& f : it->second.fields) {
          if (f.first == expr->field_chain[fi]) {
            if (fi + 1 < expr->field_chain.size())
              cur = f.second.struct_name;
            else
              return f.second.struct_name.empty() ? f.second.type : FfiType::Ptr;
            found = true;
            break;
          }
        }
        if (!found) return FfiType::Void;
      }
      return FfiType::Void;
    }
    case Expr::Kind::Cast:
      if (expr->var_name == "ptr") return FfiType::Ptr;
      return FfiType::Void;
    case Expr::Kind::Compare:
      return FfiType::I64;  /* condition produces i1 in IR */
    case Expr::Kind::Index:
      return FfiType::I64;  /* actual element type comes from array_element_type_from_expr in codegen */
  }
  return FfiType::Void;
}

struct CodegenEnv {
  Program* program = nullptr;
  Module* module = nullptr;
  IRBuilder<>* builder = nullptr;
  LayoutMap* layout_map = nullptr;
  std::unordered_map<std::string, Value*> lib_handles;
  std::vector<std::unordered_map<std::string, Value*>> vars_scope_stack;
  std::vector<std::unordered_map<std::string, FfiType>> array_element_scope_stack;
  std::vector<std::unordered_map<std::string, FnPtrSig>> fnptr_scope_stack;
  /* Ptr-to-struct tracking: variable name -> struct name */
  std::vector<std::unordered_map<std::string, std::string>> var_struct_scope_stack;
  /* Array element struct tracking: variable name -> struct name of elements */
  std::vector<std::unordered_map<std::string, std::string>> array_struct_scope_stack;
  /* Raw pointer vars (from casts) — have elem type but no array header for bounds checking */
  std::unordered_set<std::string> raw_ptr_vars;
  std::unordered_map<std::string, Function*> user_fns;
  const std::unordered_map<std::string, FfiType>* var_types = nullptr;
  std::unordered_map<std::string, FfiType>* fn_var_types = nullptr;
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

static bool codegen_lookup_fnptr_sig(CodegenEnv& env, Expr* expr, FnPtrSig* out) {
  if (!expr || !out) return false;
  if (expr->kind == Expr::Kind::VarRef) {
    for (auto it = env.fnptr_scope_stack.rbegin(); it != env.fnptr_scope_stack.rend(); ++it) {
      auto fit = it->find(expr->var_name);
      if (fit != it->end()) {
        *out = fit->second;
        return true;
      }
    }
    if (env.program) {
      for (const FnDef& def : env.program->user_fns)
        if (def.name == expr->var_name) {
          *out = fn_def_to_sig(def);
          return true;
        }
      for (const ExternFn& ext : env.program->extern_fns)
        if (ext.name == expr->var_name) {
          *out = extern_fn_to_sig(ext);
          return true;
        }
    }
    return false;
  }
  if (expr->kind == Expr::Kind::Call && expr->callee == "get_func_ptr" &&
      expr->args.size() == 1 && expr->args[0]->kind == Expr::Kind::VarRef) {
    const std::string& fn_name = expr->args[0]->var_name;
    if (env.program) {
      for (const FnDef& def : env.program->user_fns)
        if (def.name == fn_name) {
          *out = fn_def_to_sig(def);
          return true;
        }
      for (const ExternFn& ext : env.program->extern_fns)
        if (ext.name == fn_name) {
          *out = extern_fn_to_sig(ext);
          return true;
        }
    }
    return false;
  }
  return false;
}

/* Lookup variable from innermost to outermost scope. */
static Value* vars_lookup(CodegenEnv& env, const std::string& name) {
  for (auto it = env.vars_scope_stack.rbegin(); it != env.vars_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return nullptr;
}

static FfiType array_elem_lookup(CodegenEnv& env, const std::string& name) {
  for (auto it = env.array_element_scope_stack.rbegin(); it != env.array_element_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return FfiType::Void;
}

/* Returns element type if expr is an array; otherwise FfiType::Void. */
static FfiType array_element_type_from_expr(Expr* expr, CodegenEnv& env) {
  if (!expr) return FfiType::Void;
  if (expr->kind == Expr::Kind::VarRef) {
    return array_elem_lookup(env, expr->var_name);
  }
  if (expr->kind == Expr::Kind::StackArray || expr->kind == Expr::Kind::HeapArray ||
      expr->kind == Expr::Kind::AsArray) {
    const std::string& raw = expr->var_name;
    const std::string& t = (raw.size() > 4 && raw.substr(0,4) == "ptr[") ? std::string("ptr") : raw;
    if (t == "i8") return FfiType::I8;
    if (t == "i32") return FfiType::I32;
    if (t == "i64") return FfiType::I64;
    if (t == "f32") return FfiType::F32;
    if (t == "f64") return FfiType::F64;
    if (t == "ptr") return FfiType::Ptr;
    return FfiType::Void;
  }
  if (expr->kind == Expr::Kind::FieldAccess && !expr->field_chain.empty() &&
      !expr->load_field_struct.empty() && env.layout_map) {
    std::string cur = expr->load_field_struct;
    for (size_t fi = 0; fi < expr->field_chain.size(); ++fi) {
      auto it = env.layout_map->find(cur);
      if (it == env.layout_map->end()) return FfiType::Void;
      for (const auto& f : it->second.fields) {
        if (f.first == expr->field_chain[fi]) {
          if (fi + 1 == expr->field_chain.size())
            return f.second.type;
          cur = f.second.struct_name;
          if (cur.empty()) return FfiType::Void;
          break;
        }
      }
    }
  }
  return FfiType::Void;
}

/* If expr is a Call to a user fn that returns ptr[T], return T (struct name); else "". */
static std::string get_call_array_element_struct_name(Expr* expr, Program* program) {
  if (!expr || expr->kind != Expr::Kind::Call || !program) return "";
  for (const FnDef& def : program->user_fns) {
    if (def.name == expr->callee &&
        def.return_type == FfiType::Ptr &&
        !def.array_element_struct.empty())
      return def.array_element_struct;
  }
  return "";
}

/* Lookup which struct a pointer variable points to in codegen. */
static std::string var_struct_lookup_cg(CodegenEnv& env, const std::string& name) {
  for (auto it = env.var_struct_scope_stack.rbegin(); it != env.var_struct_scope_stack.rend(); ++it) {
    auto fit = it->find(name);
    if (fit != it->end()) return fit->second;
  }
  return "";
}

/* Get struct name that an expression points to (for field access codegen). */
static std::string expr_struct_name_cg(Expr* expr, CodegenEnv& env) {
  if (!expr) return "";
  if (expr->kind == Expr::Kind::VarRef)
    return var_struct_lookup_cg(env, expr->var_name);
  if (expr->kind == Expr::Kind::HeapAlloc || expr->kind == Expr::Kind::StackAlloc) {
    if (env.layout_map && env.layout_map->count(expr->var_name))
      return expr->var_name;
    return "";
  }
  if (expr->kind == Expr::Kind::Index) {
    if (expr->left && expr->left->kind == Expr::Kind::VarRef) {
      for (auto it = env.array_struct_scope_stack.rbegin(); it != env.array_struct_scope_stack.rend(); ++it) {
        auto fit = it->find(expr->left->var_name);
        if (fit != it->end()) return fit->second;
      }
    }
    if (expr->left && (expr->left->kind == Expr::Kind::HeapArray || expr->left->kind == Expr::Kind::StackArray)) {
      if (env.layout_map && env.layout_map->count(expr->left->var_name))
        return expr->left->var_name;
    }
  }
  if (expr->kind == Expr::Kind::FieldAccess) {
    // Use annotated struct name; find struct name of last field
    if (!expr->load_field_struct.empty() && env.layout_map) {
      std::string cur = expr->load_field_struct;
      for (size_t fi = 0; fi + 1 < expr->field_chain.size(); ++fi) {
        auto it = env.layout_map->find(cur);
        if (it == env.layout_map->end()) return "";
        for (const auto& f : it->second.fields)
          if (f.first == expr->field_chain[fi]) { cur = f.second.struct_name; break; }
        if (cur.empty()) return "";
      }
      auto it = env.layout_map->find(cur);
      if (it == env.layout_map->end()) return "";
      for (const auto& f : it->second.fields)
        if (f.first == expr->field_chain.back()) return f.second.struct_name;
    }
  }
  return "";
}

/* Compute field address pointer given base ptr and field chain. Returns nullptr on error.
 * out_field_type receives the FfiType of the terminal field (Void if embedded struct). */
static Value* emit_field_address(CodegenEnv& env, Value* base_ptr, const std::string& base_struct,
                                  const std::vector<std::string>& field_chain,
                                  FfiType* out_field_type) {
  LLVMContext& ctx = env.builder->getContext();
  IRBuilder<>& B = *env.builder;
  Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
  if (!base_ptr || base_struct.empty() || !env.layout_map || field_chain.empty()) return nullptr;
  if (base_ptr->getType() != i8ptr) {
    if (base_ptr->getType() == B.getInt64Ty())
      base_ptr = B.CreateIntToPtr(base_ptr, i8ptr);
    else
      base_ptr = B.CreatePointerCast(base_ptr, i8ptr);
  }
  std::string cur = base_struct;
  size_t total_offset = 0;
  FfiType field_ty = FfiType::Void;
  for (size_t fi = 0; fi < field_chain.size(); ++fi) {
    auto it = env.layout_map->find(cur);
    if (it == env.layout_map->end()) return nullptr;
    bool found = false;
    for (const auto& f : it->second.fields) {
      if (f.first == field_chain[fi]) {
        total_offset += f.second.offset;
        if (fi + 1 < field_chain.size()) {
          if (f.second.struct_name.empty()) return nullptr;
          cur = f.second.struct_name;
        } else {
          field_ty = f.second.type;
        }
        found = true;
        break;
      }
    }
    if (!found) return nullptr;
  }
  *out_field_type = field_ty;
  return B.CreateGEP(B.getInt8Ty(), base_ptr, B.getInt64(total_offset));
}

/* Like expr_type but uses array_element_type_from_expr for Index so f64 arrays yield F64. */
static FfiType expr_type_proper(Expr* expr, Program* program,
    const std::unordered_map<std::string, FfiType>* local_types, CodegenEnv& env) {
  if (!expr) return FfiType::Void;
  if (expr->kind == Expr::Kind::Index) {
    FfiType e = array_element_type_from_expr(expr->left.get(), env);
    return (e != FfiType::Void) ? e : FfiType::I64;
  }
  if (expr->kind == Expr::Kind::BinaryOp) {
    FfiType l = expr_type_proper(expr->left.get(), program, local_types, env);
    FfiType r = expr_type_proper(expr->right.get(), program, local_types, env);
    if (l == FfiType::Ptr && r == FfiType::Ptr && expr->bin_op == BinOp::Add)
      return FfiType::Ptr;
    return (l == FfiType::F64 || r == FfiType::F64) ? FfiType::F64 : FfiType::I64;
  }
  return expr_type(expr, program, local_types);
}

/* Header size H = align_up(8, alignof(T)). Returns 8 for primitives; for structs uses layout. */
static size_t array_header_size(const std::string& elem_type_name, LayoutMap* layout_map) {
  size_t align = 8;
  if (elem_type_name == "i8") align = 1;
  else if (elem_type_name == "i32" || elem_type_name == "f32") align = 4;
  else if (elem_type_name == "i64" || elem_type_name == "f64" || elem_type_name == "ptr") align = 8;
  else if (layout_map) {
    auto it = layout_map->find(elem_type_name);
    if (it != layout_map->end()) align = it->second.alignment;
  }
  return (8 + align - 1) / align * align;
}

/* Elem size in bytes. */
static size_t elem_size_from_type(const std::string& elem_type_name, LayoutMap* layout_map) {
  if (elem_type_name == "i8") return 1;
  if (elem_type_name == "i32" || elem_type_name == "f32") return 4;
  if (elem_type_name == "i64" || elem_type_name == "f64" || elem_type_name == "ptr") return 8;
  if (layout_map) {
    auto it = layout_map->find(elem_type_name);
    if (it != layout_map->end()) return it->second.size;
  }
  return 8;
}

/* Get elem type name from array expr (for StackArray/HeapArray) or from VarRef via env. */
static std::string array_elem_type_name(Expr* expr, CodegenEnv& env) {
  if (expr->kind == Expr::Kind::StackArray || expr->kind == Expr::Kind::HeapArray ||
      expr->kind == Expr::Kind::AsArray)
    return expr->var_name;
  if (expr->kind == Expr::Kind::VarRef) {
    /* VarRef: we don't store type name in env; use FfiType to infer. For structs use array_struct_scope. */
    FfiType t = array_elem_lookup(env, expr->var_name);
    if (t == FfiType::I8) return "i8";
    if (t == FfiType::I32) return "i32";
    if (t == FfiType::I64) return "i64";
    if (t == FfiType::F32) return "f32";
    if (t == FfiType::F64) return "f64";
    if (t == FfiType::Ptr) return "ptr";
  }
  return "i64";  /* fallback */
}

static Value* coerce_value_to_type(CodegenEnv& env, Value* v, FfiType from_ffi, Type* want) {
  if (!v || !want) return nullptr;
  IRBuilder<>& B = *env.builder;
  Type* have = v->getType();
  if (have == want) return v;
  if (want == B.getDoubleTy()) {
    if (have == B.getInt64Ty() || have == B.getInt32Ty())
      return B.CreateSIToFP(v, B.getDoubleTy());
    if (have == B.getFloatTy())
      return B.CreateFPExt(v, B.getDoubleTy());
  }
  if (want == B.getFloatTy()) {
    if (have == B.getDoubleTy())
      return B.CreateFPTrunc(v, B.getFloatTy());
    if (have == B.getInt64Ty() || have == B.getInt32Ty())
      return B.CreateSIToFP(v, B.getFloatTy());
  }
  if (want == B.getInt64Ty()) {
    if (have == B.getDoubleTy() || have == B.getFloatTy())
      return B.CreateFPToSI(v, B.getInt64Ty());
    if (have == B.getInt32Ty())
      return B.CreateSExt(v, B.getInt64Ty());
    if (have->isPointerTy())
      return B.CreatePtrToInt(v, B.getInt64Ty());
  }
  if (want == B.getInt32Ty()) {
    if (have == B.getInt64Ty())
      return B.CreateTrunc(v, B.getInt32Ty());
    if (have == B.getDoubleTy() || have == B.getFloatTy())
      return B.CreateFPToSI(v, B.getInt32Ty());
  }
  if (want->isPointerTy()) {
    if (have->isPointerTy())
      return B.CreatePointerCast(v, want);
    if (have == B.getInt64Ty())
      return B.CreateIntToPtr(v, want);
  }
  return nullptr;
}

static Value* emit_expr(CodegenEnv& env, Expr* expr) {
  if (!expr) return nullptr;
  LLVMContext& ctx = env.builder->getContext();
  IRBuilder<>& B = *env.builder;
  Module* M = env.module;
  Program* prog = env.program;

  switch (expr->kind) {
    case Expr::Kind::VarRef: {
      Value* alloca_val = vars_lookup(env, expr->var_name);
      if (!alloca_val) {
        if (s_codegen_error.empty())
          s_codegen_error = "variable '" + expr->var_name + "' not found";
        return nullptr;
      }
      AllocaInst* alloca = cast<AllocaInst>(alloca_val);
      return B.CreateLoad(alloca->getAllocatedType(), alloca, expr->var_name + ".load");
    }
    case Expr::Kind::IntLiteral:
      return B.getInt64(expr->int_value);
    case Expr::Kind::FloatLiteral:
      return llvm::ConstantFP::get(B.getDoubleTy(), expr->float_value);
    case Expr::Kind::StringLiteral: {
      /* Stack buffer for content, then rt_str_dup so the returned pointer outlives this function
         (e.g. when stored in a struct and used after the callee returns). */
      std::string s = expr->str_value + '\0';
      Constant* str_const = ConstantDataArray::getString(ctx, s, false);
      Type* str_ty = str_const->getType();
      Value* str_buf = B.CreateAlloca(str_ty, nullptr, "str");
      B.CreateStore(str_const, str_buf);
      Value* str_i8 = B.CreatePointerCast(str_buf, PointerType::get(Type::getInt8Ty(ctx), 0));
      Function* dup_fn = M->getFunction("rt_str_dup");
      if (!dup_fn) {
        s_codegen_error = "rt_str_dup not found for string literal";
        return nullptr;
      }
      return B.CreateCall(dup_fn, {str_i8}, "str_lit");
    }
    case Expr::Kind::BinaryOp: {
      FfiType tyL = expr_type_proper(expr->left.get(), prog, env.var_types, env);
      FfiType tyR = expr_type_proper(expr->right.get(), prog, env.var_types, env);
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (tyL == FfiType::Ptr && tyR == FfiType::Ptr && expr->bin_op == BinOp::Add) {
        Value* L = emit_expr(env, expr->left.get());
        if (!L) {
          if (s_codegen_error.empty())
            s_codegen_error = "binary op: left expression failed";
          return nullptr;
        }
        if (L->getType() != i8ptr) L = B.CreatePointerCast(L, i8ptr);
        Function* dup_fn = M->getFunction("rt_str_dup");
        if (!dup_fn) {
          s_codegen_error = "rt_str_dup not found";
          return nullptr;
        }
        Value* L_copy = B.CreateCall(dup_fn, {L}, "str_dup");
        Value* R = emit_expr(env, expr->right.get());
        if (!R) {
          if (s_codegen_error.empty())
            s_codegen_error = "binary op: right expression failed";
          return nullptr;
        }
        if (R->getType() != i8ptr) R = B.CreatePointerCast(R, i8ptr);
        Function* fn = M->getFunction("rt_str_concat");
        if (!fn) {
          s_codegen_error = "rt_str_concat not found";
          return nullptr;
        }
        return B.CreateCall(fn, {L_copy, R}, "str_concat");
      }
      Value* L = emit_expr(env, expr->left.get());
      Value* R = emit_expr(env, expr->right.get());
      if (!L || !R) {
        if (s_codegen_error.empty())
          s_codegen_error = "binary op: left or right expression failed";
        return nullptr;
      }
      bool is_f64 = (tyL == FfiType::F64 || tyR == FfiType::F64);
      if (is_f64) {
        if (L->getType() != B.getDoubleTy()) L = B.CreateSIToFP(L, B.getDoubleTy());
        if (R->getType() != B.getDoubleTy()) R = B.CreateSIToFP(R, B.getDoubleTy());
      } else {
        if (L->getType() != B.getInt64Ty()) {
          Value* c = coerce_value_to_type(env, L, tyL, B.getInt64Ty());
          if (!c) { s_codegen_error = "binary op: cannot coerce left to i64"; return nullptr; }
          L = c;
        }
        if (R->getType() != B.getInt64Ty()) {
          Value* c = coerce_value_to_type(env, R, tyR, B.getInt64Ty());
          if (!c) { s_codegen_error = "binary op: cannot coerce right to i64"; return nullptr; }
          R = c;
        }
      }
      switch (expr->bin_op) {
        case BinOp::Add:
          return is_f64 ? B.CreateFAdd(L, R, "add") : B.CreateAdd(L, R, "add");
        case BinOp::Sub:
          return is_f64 ? B.CreateFSub(L, R, "sub") : B.CreateSub(L, R, "sub");
        case BinOp::Mul:
          return is_f64 ? B.CreateFMul(L, R, "mul") : B.CreateMul(L, R, "mul");
        case BinOp::Div:
          return is_f64 ? B.CreateFDiv(L, R, "div") : B.CreateSDiv(L, R, "div");
      }
      if (s_codegen_error.empty())
        s_codegen_error = "binary op: unknown operator";
      return nullptr;
    }
    case Expr::Kind::Call: {
      if (expr->callee == "get_func_ptr") {
        if (expr->args.size() != 1 || expr->args[0]->kind != Expr::Kind::VarRef) return nullptr;
        const std::string& fn_name = expr->args[0]->var_name;
        auto uf_it = env.user_fns.find(fn_name);
        if (uf_it == env.user_fns.end()) return nullptr;
        Function* fn = uf_it->second;
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        return B.CreatePointerCast(fn, i8ptr);
      }
      if (expr->callee == "call") {
        if (expr->args.size() < 1) return nullptr;
        FnPtrSig sig;
        if (!codegen_lookup_fnptr_sig(env, expr->args[0].get(), &sig)) {
          if (expr->inferred_call_param_types.size() == expr->args.size() - 1) {
            sig.params = expr->inferred_call_param_types;
            sig.result = expr->inferred_call_result_type;
          } else {
            s_codegen_error = "cannot determine function signature for call";
            return nullptr;
          }
        }
        std::vector<Type*> param_types;
        for (FfiType p : sig.params)
          param_types.push_back(ffi_type_to_llvm(p, ctx, B));
        Type* ret_ty = ffi_type_to_llvm(sig.result, ctx, B);
        FunctionType* ft = FunctionType::get(ret_ty, param_types, false);
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* callee_i8 = emit_expr(env, expr->args[0].get());
        if (!callee_i8) return nullptr;
        if (callee_i8->getType() != i8ptr) callee_i8 = B.CreatePointerCast(callee_i8, i8ptr);
        Function* rt_panic_fn = M->getFunction("rt_panic");
        if (!rt_panic_fn) return nullptr;
        Value* is_null = B.CreateIsNull(callee_i8);
        BasicBlock* cont_bb = BasicBlock::Create(ctx, "call.cont", B.GetInsertBlock()->getParent());
        BasicBlock* panic_bb = BasicBlock::Create(ctx, "call.panic", B.GetInsertBlock()->getParent());
        B.CreateCondBr(is_null, panic_bb, cont_bb);
        B.SetInsertPoint(panic_bb);
        const char* msg = "call on null function pointer";
        Type* msg_ty = ArrayType::get(Type::getInt8Ty(ctx), strlen(msg) + 1);
        Value* msg_buf = B.CreateAlloca(msg_ty, nullptr, "panic_msg");
        B.CreateStore(ConstantDataArray::getString(ctx, msg, true), msg_buf);
        B.CreateCall(rt_panic_fn, B.CreatePointerCast(msg_buf, i8ptr));
        B.CreateUnreachable();
        B.SetInsertPoint(cont_bb);
        Value* callee_typed = B.CreatePointerCast(callee_i8, PointerType::get(ft, 0));
        std::vector<Value*> call_args;
        for (size_t k = 0; k < sig.params.size(); ++k) {
          Value* v = emit_expr(env, expr->args[k + 1].get());
          if (!v) return nullptr;
          Type* want = ft->getParamType(k);
          v = coerce_value_to_type(env, v, sig.params[k], want);
          if (!v) return nullptr;
          call_args.push_back(v);
        }
        CallInst* ci = B.CreateCall(ft, callee_typed, call_args);
        if (sig.result == FfiType::Void) return B.getInt64(0);
        return ci;
      }
      if (expr->callee == "print") {
        if (expr->args.size() != 1 && expr->args.size() != 2) return nullptr;
        Value* arg_val = emit_expr(env, expr->args[0].get());
        if (!arg_val) return nullptr;
        Value* stream_val = expr->args.size() >= 2 ? emit_expr(env, expr->args[1].get()) : B.getInt64(0);
        if (!stream_val) return nullptr;
        if (stream_val->getType() != B.getInt64Ty()) stream_val = B.CreateIntCast(stream_val, B.getInt64Ty(), true);
        FfiType arg_ty = FfiType::Void;
        if (expr->args[0]->kind == Expr::Kind::Index) {
          arg_ty = array_element_type_from_expr(expr->args[0]->left.get(), env);
        }
        if (arg_ty == FfiType::Void)
          arg_ty = expr_type(expr->args[0].get(), prog, env.var_types);
        Function* rt_print = M->getFunction("rt_print_cstring");
        if (!rt_print) return nullptr;
        if (arg_ty == FfiType::F64) {
          Function* to_str = M->getFunction("rt_to_str_f64");
          if (!to_str) return nullptr;
          if (arg_val->getType() != B.getDoubleTy())
            arg_val = B.CreateSIToFP(arg_val, B.getDoubleTy());
          Value* s = B.CreateCall(to_str, arg_val, "to_str");
          return B.CreateCall(rt_print, {s, stream_val});
        }
        if (arg_ty == FfiType::Ptr) {
          Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
          if (arg_val->getType() != i8ptr) arg_val = B.CreatePointerCast(arg_val, i8ptr);
          return B.CreateCall(rt_print, {arg_val, stream_val});
        }
        {
          Function* to_str = M->getFunction("rt_to_str_i64");
          if (!to_str) return nullptr;
          if (arg_val->getType() != B.getInt64Ty())
            arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
          Value* s = B.CreateCall(to_str, arg_val, "to_str");
          return B.CreateCall(rt_print, {s, stream_val});
        }
      }
      if (expr->callee == "println") {
        if (expr->args.size() != 1 && expr->args.size() != 2) return nullptr;
        Value* arg_val = emit_expr(env, expr->args[0].get());
        if (!arg_val) return nullptr;
        Value* stream_val = expr->args.size() >= 2 ? emit_expr(env, expr->args[1].get()) : B.getInt64(0);
        if (!stream_val) return nullptr;
        if (stream_val->getType() != B.getInt64Ty()) stream_val = B.CreateIntCast(stream_val, B.getInt64Ty(), true);
        FfiType arg_ty = FfiType::Void;
        if (expr->args[0]->kind == Expr::Kind::Index) {
          arg_ty = array_element_type_from_expr(expr->args[0]->left.get(), env);
        }
        if (arg_ty == FfiType::Void)
          arg_ty = expr_type(expr->args[0].get(), prog, env.var_types);
        Function* rt_print = M->getFunction("rt_print_cstring");
        Function* rt_concat = M->getFunction("rt_str_concat");
        if (!rt_print || !rt_concat) return nullptr;
        Value* newline = B.CreateGlobalStringPtr("\n", "newline");
        if (arg_ty == FfiType::F64) {
          Function* to_str = M->getFunction("rt_to_str_f64");
          if (!to_str) return nullptr;
          if (arg_val->getType() != B.getDoubleTy())
            arg_val = B.CreateSIToFP(arg_val, B.getDoubleTy());
          Value* s = B.CreateCall(to_str, arg_val, "to_str");
          Value* with_nl = B.CreateCall(rt_concat, {s, newline}, "with_nl");
          return B.CreateCall(rt_print, {with_nl, stream_val});
        }
        if (arg_ty == FfiType::Ptr) {
          Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
          if (arg_val->getType() != i8ptr) arg_val = B.CreatePointerCast(arg_val, i8ptr);
          Value* with_nl = B.CreateCall(rt_concat, {arg_val, newline}, "with_nl");
          return B.CreateCall(rt_print, {with_nl, stream_val});
        }
        {
          Function* to_str = M->getFunction("rt_to_str_i64");
          if (!to_str) return nullptr;
          if (arg_val->getType() != B.getInt64Ty())
            arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
          Value* s = B.CreateCall(to_str, arg_val, "to_str");
          Value* with_nl = B.CreateCall(rt_concat, {s, newline}, "with_nl");
          return B.CreateCall(rt_print, {with_nl, stream_val});
        }
      }
      if (expr->callee == "read_line") {
        Function* fn = M->getFunction("rt_read_line");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {}, "read_line");
      }
      if (expr->callee == "read_key") {
        Function* fn = M->getFunction("rt_read_key");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {}, "read_key");
      }
      if (expr->callee == "terminal_height") {
        Function* fn = M->getFunction("rt_terminal_height");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {}, "terminal_height");
      }
      if (expr->callee == "terminal_width") {
        Function* fn = M->getFunction("rt_terminal_width");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {}, "terminal_width");
      }
      if (expr->callee == "flush") {
        Function* fn = M->getFunction("rt_flush");
        if (!fn) return nullptr;
        Value* stream_val = emit_expr(env, expr->args[0].get());
        if (!stream_val) return nullptr;
        if (stream_val->getType() != B.getInt64Ty())
          stream_val = B.CreateIntCast(stream_val, B.getInt64Ty(), true);
        B.CreateCall(fn, {stream_val});
        return B.getInt64(0);
      }
      if (expr->callee == "sleep") {
        Function* fn = M->getFunction("rt_sleep");
        if (!fn) return nullptr;
        Value* ms_val = emit_expr(env, expr->args[0].get());
        if (!ms_val) return nullptr;
        if (ms_val->getType() != B.getInt64Ty())
          ms_val = B.CreateIntCast(ms_val, B.getInt64Ty(), true);
        B.CreateCall(fn, {ms_val});
        return B.getInt64(0);
      }
      if (expr->callee == "chr") {
        Value* arg_val = emit_expr(env, expr->args[0].get());
        if (!arg_val) return nullptr;
        Function* fn = M->getFunction("rt_chr");
        if (!fn) return nullptr;
        return B.CreateCall(fn, arg_val, "chr");
      }
      if (expr->callee == "to_str") {
        FfiType t = FfiType::Void;
        if (expr->args[0]->kind == Expr::Kind::Index) {
          t = array_element_type_from_expr(expr->args[0]->left.get(), env);
        }
        if (t == FfiType::Void)
          t = expr_type(expr->args[0].get(), prog, env.var_types);
        Value* arg_val = emit_expr(env, expr->args[0].get());
        if (!arg_val) return nullptr;
        Function* fn = (t == FfiType::F64) ? M->getFunction("rt_to_str_f64") : M->getFunction("rt_to_str_i64");
        if (!fn) return nullptr;
        if (t == FfiType::F64 && arg_val->getType() != B.getDoubleTy())
          arg_val = B.CreateSIToFP(arg_val, B.getDoubleTy());
        else if (t != FfiType::F64 && arg_val->getType() != B.getInt64Ty())
          arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
        return B.CreateCall(fn, arg_val, "to_str");
      }
      if (expr->callee == "from_str") {
        Value* s_val = emit_expr(env, expr->args[0].get());
        if (!s_val) return nullptr;
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        if (s_val->getType() != i8ptr) s_val = B.CreatePointerCast(s_val, i8ptr);
        if (expr->call_type_arg == "i64") {
          Function* fn = M->getFunction("rt_from_str_i64");
          if (!fn) return nullptr;
          return B.CreateCall(fn, s_val, "from_str_i64");
        }
        if (expr->call_type_arg == "f64") {
          Function* fn = M->getFunction("rt_from_str_f64");
          if (!fn) return nullptr;
          return B.CreateCall(fn, s_val, "from_str_f64");
        }
        return nullptr;
      }
      if (expr->callee == "open") {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* path = emit_expr(env, expr->args[0].get());
        Value* mode = emit_expr(env, expr->args[1].get());
        if (!path || !mode) return nullptr;
        if (path->getType() != i8ptr) path = B.CreatePointerCast(path, i8ptr);
        if (mode->getType() != i8ptr) mode = B.CreatePointerCast(mode, i8ptr);
        Function* fn = M->getFunction("rt_open");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {path, mode}, "open");
      }
      if (expr->callee == "close") {
        Value* h = emit_expr(env, expr->args[0].get());
        if (!h) return nullptr;
        Function* fn = M->getFunction("rt_close");
        if (!fn) return nullptr;
        return B.CreateCall(fn, h);
      }
      if (expr->callee == "read_line_file") {
        Value* h = emit_expr(env, expr->args[0].get());
        if (!h) return nullptr;
        Function* fn = M->getFunction("rt_read_line_file");
        if (!fn) return nullptr;
        return B.CreateCall(fn, h, "read_line_file");
      }
      if (expr->callee == "str_dup") {
        Value* s = emit_expr(env, expr->args[0].get());
        if (!s) return nullptr;
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        if (s->getType() != i8ptr) s = B.CreatePointerCast(s, i8ptr);
        Function* fn = M->getFunction("rt_str_dup");
        if (!fn) return nullptr;
        return B.CreateCall(fn, s, "str_dup");
      }
      if (expr->callee == "str_upper" || expr->callee == "str_lower" || expr->callee == "str_strip") {
        Value* s = emit_expr(env, expr->args[0].get());
        if (!s) return nullptr;
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        if (s->getType() != i8ptr) s = B.CreatePointerCast(s, i8ptr);
        Function* fn = M->getFunction("rt_" + expr->callee);
        if (!fn) return nullptr;
        return B.CreateCall(fn, s, expr->callee);
      }
      if (expr->callee == "str_contains" || expr->callee == "str_find") {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* a = emit_expr(env, expr->args[0].get());
        Value* b = emit_expr(env, expr->args[1].get());
        if (!a || !b) return nullptr;
        if (a->getType() != i8ptr) a = B.CreatePointerCast(a, i8ptr);
        if (b->getType() != i8ptr) b = B.CreatePointerCast(b, i8ptr);
        Function* fn = M->getFunction("rt_" + expr->callee);
        if (!fn) return nullptr;
        return B.CreateCall(fn, {a, b}, expr->callee);
      }
      if (expr->callee == "str_split") {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* a = emit_expr(env, expr->args[0].get());
        Value* b = emit_expr(env, expr->args[1].get());
        if (!a || !b) return nullptr;
        if (a->getType() != i8ptr) a = B.CreatePointerCast(a, i8ptr);
        if (b->getType() != i8ptr) b = B.CreatePointerCast(b, i8ptr);
        Function* fn = M->getFunction("rt_str_split");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {a, b}, "str_split");
      }
      if (expr->callee == "http_request") {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* method = emit_expr(env, expr->args[0].get());
        Value* url = emit_expr(env, expr->args[1].get());
        Value* body = emit_expr(env, expr->args[2].get());
        if (!method || !url || !body) return nullptr;
        if (method->getType() != i8ptr) method = B.CreatePointerCast(method, i8ptr);
        if (url->getType() != i8ptr) url = B.CreatePointerCast(url, i8ptr);
        if (body->getType() != i8ptr) body = B.CreatePointerCast(body, i8ptr);
        Function* fn = M->getFunction("rt_http_request");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {method, url, body}, "http_request");
      }
      if (expr->callee == "http_status") {
        Function* fn = M->getFunction("rt_http_status");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {}, "http_status");
      }
      if (expr->callee == "write_file") {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* h = emit_expr(env, expr->args[0].get());
        Value* x = emit_expr(env, expr->args[1].get());
        if (!h || !x) return nullptr;
        FfiType val_ty = expr_type(expr->args[1].get(), prog, env.var_types);
        Function* fn_write = M->getFunction("rt_write_file_ptr");
        if (!fn_write) return nullptr;
        if (val_ty == FfiType::I64) {
          Function* to_str = M->getFunction("rt_to_str_i64");
          Function* concat = M->getFunction("rt_str_concat");
          if (!to_str || !concat) return nullptr;
          if (x->getType() != B.getInt64Ty()) x = B.CreateFPToSI(x, B.getInt64Ty());
          Value* s = B.CreateCall(to_str, x, "to_str");
          Value* nl = B.CreateGlobalStringPtr("\n", "newline");
          Value* with_nl = B.CreateCall(concat, {s, nl}, "with_nl");
          return B.CreateCall(fn_write, {h, with_nl});
        } else if (val_ty == FfiType::F64) {
          Function* to_str = M->getFunction("rt_to_str_f64");
          Function* concat = M->getFunction("rt_str_concat");
          if (!to_str || !concat) return nullptr;
          if (x->getType() != B.getDoubleTy()) x = B.CreateSIToFP(x, B.getDoubleTy());
          Value* s = B.CreateCall(to_str, x, "to_str");
          Value* nl = B.CreateGlobalStringPtr("\n", "newline");
          Value* with_nl = B.CreateCall(concat, {s, nl}, "with_nl");
          return B.CreateCall(fn_write, {h, with_nl});
        } else {
          if (x->getType() != i8ptr) x = B.CreatePointerCast(x, i8ptr);
          return B.CreateCall(fn_write, {h, x});
        }
      }
      if (expr->callee == "write_bytes") {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* h = emit_expr(env, expr->args[0].get());
        Value* buf = emit_expr(env, expr->args[1].get());
        Value* n = emit_expr(env, expr->args[2].get());
        if (!h || !buf || !n) return nullptr;
        if (buf->getType() != i8ptr) buf = B.CreatePointerCast(buf, i8ptr);
        if (n->getType() != B.getInt64Ty()) n = B.CreateIntCast(n, B.getInt64Ty(), true);
        Function* fn = M->getFunction("rt_write_bytes");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {h, buf, n}, "write_bytes");
      }
      if (expr->callee == "read_bytes") {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* h = emit_expr(env, expr->args[0].get());
        Value* buf = emit_expr(env, expr->args[1].get());
        Value* n = emit_expr(env, expr->args[2].get());
        if (!h || !buf || !n) return nullptr;
        if (buf->getType() != i8ptr) buf = B.CreatePointerCast(buf, i8ptr);
        if (n->getType() != B.getInt64Ty()) n = B.CreateIntCast(n, B.getInt64Ty(), true);
        Function* fn = M->getFunction("rt_read_bytes");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {h, buf, n}, "read_bytes");
      }
      if (expr->callee == "eof_file") {
        Value* h = emit_expr(env, expr->args[0].get());
        if (!h) return nullptr;
        Function* fn = M->getFunction("rt_eof_file");
        if (!fn) return nullptr;
        return B.CreateCall(fn, h, "eof_file");
      }
      if (expr->callee == "line_count_file") {
        Value* h = emit_expr(env, expr->args[0].get());
        if (!h) return nullptr;
        Function* fn = M->getFunction("rt_line_count_file");
        if (!fn) return nullptr;
        return B.CreateCall(fn, h, "line_count_file");
      }
      if (expr->callee == "len") {
        Value* elem_ptr = emit_expr(env, expr->args[0].get());
        if (!elem_ptr) return nullptr;
        Type* i8ptr_ty = PointerType::get(Type::getInt8Ty(ctx), 0);
        if (elem_ptr->getType() != i8ptr_ty) elem_ptr = B.CreatePointerCast(elem_ptr, i8ptr_ty);
        std::string elem_name = array_elem_type_name(expr->args[0].get(), env);
        size_t H = array_header_size(elem_name, env.layout_map);
        Value* base = B.CreateGEP(B.getInt8Ty(), elem_ptr, B.getInt64(-static_cast<int64_t>(H)));
        Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
        return B.CreateLoad(B.getInt64Ty(), len_ptr, "len");
      }
      /* User fn call */
      auto uf_it = env.user_fns.find(expr->callee);
      if (uf_it != env.user_fns.end()) {
        Function* fn = uf_it->second;
        std::vector<Value*> args;
        for (size_t j = 0; j < expr->args.size(); ++j) {
          Value* arg_val = emit_expr(env, expr->args[j].get());
          if (!arg_val) {
            if (s_codegen_error.empty())
              s_codegen_error = "call to '" + expr->callee + "': argument " + std::to_string(j) + " failed";
            return nullptr;
          }
          Type* param_ty = fn->getArg(j)->getType();
          if (arg_val->getType() != param_ty) {
            if (param_ty == B.getDoubleTy() && arg_val->getType() == B.getInt64Ty())
              arg_val = B.CreateSIToFP(arg_val, B.getDoubleTy());
            else if (param_ty == B.getInt64Ty() && arg_val->getType() == B.getDoubleTy())
              arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
            else if (param_ty->isPointerTy() && arg_val->getType()->isPointerTy())
              arg_val = B.CreatePointerCast(arg_val, param_ty);
            else if (param_ty->isPointerTy() && arg_val->getType() == B.getInt64Ty())
              arg_val = B.CreateIntToPtr(arg_val, param_ty);
            else {
              if (s_codegen_error.empty())
                s_codegen_error = "call to '" + expr->callee + "': argument " + std::to_string(j) + " type mismatch";
              return nullptr;
            }
          }
          args.push_back(arg_val);
        }
        if (fn->getReturnType()->isVoidTy()) {
          B.CreateCall(fn, args);
          return nullptr;
        }
        Value* ret = B.CreateCall(fn, args, "call." + expr->callee);
        return ret;
      }
      /* Extern fn call */
      const ExternFn* ext = nullptr;
      if (prog) {
        for (const ExternFn& e : prog->extern_fns)
          if (e.name == expr->callee) { ext = &e; break; }
      }
      if (!ext || prog->libs.empty()) {
        s_codegen_error = "extern fn '" + expr->callee + "' not found or no libs";
        return nullptr;
      }

      Value* handle = env.lib_handles[ext->lib_name];
      if (!handle) {
        s_codegen_error = "extern fn '" + expr->callee + "' lib handle not found (lib_name='" + ext->lib_name + "')";
        return nullptr;
      }
      handle = B.CreateLoad(PointerType::get(Type::getInt8Ty(ctx), 0), handle, true, "lib_handle");

      Function* rt_dlsym_fn = M->getFunction("rt_dlsym");
      Function* rt_panic_fn = M->getFunction("rt_panic");
      Function* rt_dlerror_fn = M->getFunction("rt_dlerror_last");
      Function* rt_ffi_sig_create_fn = M->getFunction("rt_ffi_sig_create");
      Function* rt_ffi_call_fn = M->getFunction("rt_ffi_call");
      Function* rt_ffi_error_fn = M->getFunction("rt_ffi_error_last");
      if (!rt_dlsym_fn || !rt_panic_fn || !rt_ffi_sig_create_fn || !rt_ffi_call_fn) {
        s_codegen_error = "runtime FFI symbols (rt_dlsym/rt_ffi_sig_create/rt_ffi_call) not found";
        return nullptr;
      }

      /* Symbol name on stack to avoid GlobalVariable */
      Type* sym_array_ty = ArrayType::get(Type::getInt8Ty(ctx), expr->callee.size() + 1);
      Value* sym_buf = B.CreateAlloca(sym_array_ty, nullptr, "sym");
      Constant* sym_const = ConstantDataArray::getString(ctx, expr->callee, true);
      B.CreateStore(sym_const, sym_buf);
      Value* sym_name_ptr = B.CreatePointerCast(sym_buf, PointerType::get(Type::getInt8Ty(ctx), 0));
      Value* fnptr = B.CreateCall(rt_dlsym_fn, {handle, sym_name_ptr});
      Value* is_null = B.CreateIsNull(fnptr);
      BasicBlock* cont_bb = BasicBlock::Create(ctx, "ffi.cont", B.GetInsertBlock()->getParent());
      BasicBlock* panic_bb = BasicBlock::Create(ctx, "ffi.panic_dlsym", B.GetInsertBlock()->getParent());
      B.CreateCondBr(is_null, panic_bb, cont_bb);
      B.SetInsertPoint(panic_bb);
      Value* err_msg = B.CreateCall(rt_dlerror_fn);
      B.CreateCall(rt_panic_fn, err_msg);
      B.CreateUnreachable();
      B.SetInsertPoint(cont_bb);

      unsigned nargs = ext->params.size();
      Value* arg_kinds_ptr = B.CreateAlloca(B.getInt32Ty(), B.getInt32(nargs), "arg_kinds");
      for (size_t k = 0; k < nargs; ++k) {
        Value* slot = B.CreateGEP(B.getInt32Ty(), arg_kinds_ptr, B.getInt32(k));
        B.CreateStore(B.getInt32(ffi_type_to_kind(ext->params[k].second)), slot);
      }

      Value* ret_kind_const = B.getInt32(ffi_type_to_kind(ext->return_type));
      Value* nargs_const = B.getInt32(nargs);
      Value* sig = B.CreateCall(rt_ffi_sig_create_fn, {ret_kind_const, nargs_const, arg_kinds_ptr});
      Value* sig_null = B.CreateIsNull(sig);
      BasicBlock* cont2_bb = BasicBlock::Create(ctx, "ffi.cont2", B.GetInsertBlock()->getParent());
      BasicBlock* panic2_bb = BasicBlock::Create(ctx, "ffi.panic_sig", B.GetInsertBlock()->getParent());
      B.CreateCondBr(sig_null, panic2_bb, cont2_bb);
      B.SetInsertPoint(panic2_bb);
      if (rt_ffi_error_fn) {
        Value* err_msg2 = B.CreateCall(rt_ffi_error_fn);
        B.CreateCall(rt_panic_fn, err_msg2);
      } else {
        const char* msg2 = "rt_ffi_sig_create failed";
        Type* msg2_ty = ArrayType::get(Type::getInt8Ty(ctx), strlen(msg2) + 1);
        Value* msg2_buf = B.CreateAlloca(msg2_ty, nullptr, "panic_msg");
        B.CreateStore(ConstantDataArray::getString(ctx, msg2, true), msg2_buf);
        B.CreateCall(rt_panic_fn, B.CreatePointerCast(msg2_buf, PointerType::get(Type::getInt8Ty(ctx), 0)));
      }
      B.CreateUnreachable();
      B.SetInsertPoint(cont2_bb);

      const unsigned slot_size = 8;
      Value* args_buf = B.CreateAlloca(B.getInt8Ty(), B.getInt32(nargs * slot_size), "args_buf");
      args_buf = B.CreatePointerCast(args_buf, PointerType::get(Type::getInt8Ty(ctx), 0));
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      for (size_t j = 0; j < nargs; ++j) {
        Value* arg_val = emit_expr(env, expr->args[j].get());
        if (!arg_val) return nullptr;
        Value* slot = B.CreateGEP(B.getInt8Ty(), args_buf, B.getInt32(j * slot_size));
        if (ext->params[j].second == FfiType::F64) {
          slot = B.CreatePointerCast(slot, B.getDoubleTy()->getPointerTo());
          if (arg_val->getType() != B.getDoubleTy())
            arg_val = B.CreateSIToFP(arg_val, B.getDoubleTy());
        } else if (ext->params[j].second == FfiType::Ptr) {
          slot = B.CreatePointerCast(slot, B.getInt64Ty()->getPointerTo());
          if (arg_val->getType() == i8ptr || arg_val->getType()->isPointerTy())
            arg_val = B.CreatePtrToInt(arg_val, B.getInt64Ty());
        } else {
          slot = B.CreatePointerCast(slot, B.getInt64Ty()->getPointerTo());
          if (arg_val->getType() == B.getDoubleTy())
            arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
        }
        B.CreateStore(arg_val, slot);
      }

      Value* ret_buf = B.CreateAlloca(B.getInt8Ty(), B.getInt32(slot_size), "ret_buf");
      ret_buf = B.CreatePointerCast(ret_buf, PointerType::get(Type::getInt8Ty(ctx), 0));
      Value* call_ok = B.CreateCall(rt_ffi_call_fn, {sig, fnptr, args_buf, ret_buf});
      Value* call_fail = B.CreateICmpNE(call_ok, B.getInt32(0));
      BasicBlock* cont3_bb = BasicBlock::Create(ctx, "ffi.cont3", B.GetInsertBlock()->getParent());
      BasicBlock* panic3_bb = BasicBlock::Create(ctx, "ffi.panic_call", B.GetInsertBlock()->getParent());
      B.CreateCondBr(call_fail, panic3_bb, cont3_bb);
      B.SetInsertPoint(panic3_bb);
      if (rt_ffi_error_fn) {
        Value* err_msg3 = B.CreateCall(rt_ffi_error_fn);
        B.CreateCall(rt_panic_fn, err_msg3);
      } else {
        const char* msg3 = "rt_ffi_call failed";
        Type* msg3_ty = ArrayType::get(Type::getInt8Ty(ctx), strlen(msg3) + 1);
        Value* msg3_buf = B.CreateAlloca(msg3_ty, nullptr, "panic_msg");
        B.CreateStore(ConstantDataArray::getString(ctx, msg3, true), msg3_buf);
        B.CreateCall(rt_panic_fn, B.CreatePointerCast(msg3_buf, PointerType::get(Type::getInt8Ty(ctx), 0)));
      }
      B.CreateUnreachable();
      B.SetInsertPoint(cont3_bb);

      if (ext->return_type == FfiType::Void) return B.getInt64(0);
      Value* ret_val;
      if (ext->return_type == FfiType::F64) {
        Value* ret_ptr = B.CreatePointerCast(ret_buf, B.getDoubleTy()->getPointerTo());
        ret_val = B.CreateLoad(B.getDoubleTy(), ret_ptr);
      } else if (ext->return_type == FfiType::Ptr) {
        Value* ret_ptr = B.CreatePointerCast(ret_buf, B.getInt64Ty()->getPointerTo());
        Value* ret_i64 = B.CreateLoad(B.getInt64Ty(), ret_ptr);
        ret_val = B.CreateIntToPtr(ret_i64, PointerType::get(Type::getInt8Ty(ctx), 0));
      } else {
        Value* ret_ptr = B.CreatePointerCast(ret_buf, B.getInt64Ty()->getPointerTo());
        ret_val = B.CreateLoad(B.getInt64Ty(), ret_ptr);
      }
      return ret_val;
    }
    case Expr::Kind::StackAlloc: {
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      const std::string& tn = expr->var_name;
      if (tn == "i32") {
        Value* slot = B.CreateAlloca(B.getInt32Ty(), nullptr, "stack.i32");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (tn == "i64") {
        Value* slot = B.CreateAlloca(B.getInt64Ty(), nullptr, "stack.i64");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (tn == "f32") {
        Value* slot = B.CreateAlloca(B.getFloatTy(), nullptr, "stack.f32");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (tn == "f64") {
        Value* slot = B.CreateAlloca(B.getDoubleTy(), nullptr, "stack.f64");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (tn == "ptr") {
        Value* slot = B.CreateAlloca(i8ptr, nullptr, "stack.ptr");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (env.layout_map) {
        auto it = env.layout_map->find(tn);
        if (it != env.layout_map->end() && it->second.size > 0) {
          Value* slot = B.CreateAlloca(ArrayType::get(B.getInt8Ty(), it->second.size), nullptr, "stack.struct");
          return B.CreatePointerCast(slot, i8ptr);
        }
      }
      return nullptr;
    }
    case Expr::Kind::HeapAlloc: {
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      const std::string& tn = expr->var_name;
      size_t sz = 8;
      if (tn == "i32" || tn == "f32") sz = 4;
      else if (tn == "i64" || tn == "f64" || tn == "ptr") sz = 8;
      else if (env.layout_map) {
        auto it = env.layout_map->find(tn);
        if (it != env.layout_map->end()) sz = it->second.size;
      }
      Function* malloc_fn = M->getFunction("malloc");
      if (!malloc_fn) {
        FunctionType* malloc_ty = FunctionType::get(i8ptr, {B.getInt64Ty()}, false);
        malloc_fn = Function::Create(malloc_ty, GlobalValue::ExternalLinkage, "malloc", M);
      }
      Value* raw = B.CreateCall(malloc_fn, B.getInt64(sz), "heap.alloc");
      return B.CreatePointerCast(raw, i8ptr);
    }
    case Expr::Kind::StackArray: {
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      Value* count_val = emit_expr(env, expr->left.get());
      if (!count_val) return nullptr;
      if (count_val->getType() != B.getInt64Ty())
        count_val = B.CreateIntCast(count_val, B.getInt64Ty(), true);
      std::string elem_name = expr->var_name;
      if (elem_name.size() > 4 && elem_name.substr(0,4) == "ptr[") elem_name = "ptr";
      size_t H = array_header_size(elem_name, env.layout_map);
      size_t elem_size = elem_size_from_type(elem_name, env.layout_map);
      Value* total_bytes = B.CreateAdd(B.getInt64(H), B.CreateMul(count_val, B.getInt64(elem_size)), "stack_arr.total");
      Value* base = B.CreateAlloca(B.getInt8Ty(), total_bytes, "stack_arr");
      base = B.CreatePointerCast(base, i8ptr);
      Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
      B.CreateStore(count_val, len_ptr);
      Value* elem_ptr = B.CreateGEP(B.getInt8Ty(), base, B.getInt64(H));
      return B.CreatePointerCast(elem_ptr, i8ptr);
    }
    case Expr::Kind::HeapArray: {
      Value* count_val = emit_expr(env, expr->left.get());
      if (!count_val) return nullptr;
      if (count_val->getType() != B.getInt64Ty())
        count_val = B.CreateIntCast(count_val, B.getInt64Ty(), true);
      std::string elem_name = expr->var_name;
      if (elem_name.size() > 4 && elem_name.substr(0,4) == "ptr[") elem_name = "ptr";
      size_t H = array_header_size(elem_name, env.layout_map);
      size_t elem_size = elem_size_from_type(elem_name, env.layout_map);
      Value* total_bytes = B.CreateAdd(B.getInt64(H), B.CreateMul(count_val, B.getInt64(elem_size)), "heap_arr.total");
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      Function* malloc_fn = M->getFunction("malloc");
      if (!malloc_fn) {
        FunctionType* malloc_ty = FunctionType::get(i8ptr, {B.getInt64Ty()}, false);
        malloc_fn = Function::Create(malloc_ty, GlobalValue::ExternalLinkage, "malloc", M);
      }
      Value* block = B.CreateCall(malloc_fn, total_bytes, "heap_array");
      Value* base = B.CreatePointerCast(block, i8ptr);
      Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
      B.CreateStore(count_val, len_ptr);
      Value* elem_ptr = B.CreateGEP(B.getInt8Ty(), base, B.getInt64(H));
      return B.CreatePointerCast(elem_ptr, i8ptr);
    }
    case Expr::Kind::Free: {
      Value* ptr = emit_expr(env, expr->left.get());
      if (!ptr) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (ptr->getType() != i8ptr) ptr = B.CreatePointerCast(ptr, i8ptr);
      Function* free_fn = M->getFunction("free");
      if (!free_fn) {
        FunctionType* free_ty = FunctionType::get(B.getVoidTy(), {i8ptr}, false);
        free_fn = Function::Create(free_ty, GlobalValue::ExternalLinkage, "free", M);
      }
      B.CreateCall(free_fn, ptr);
      return nullptr;
    }
    case Expr::Kind::FreeArray: {
      Value* elem_ptr = emit_expr(env, expr->left.get());
      if (!elem_ptr) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (elem_ptr->getType() != i8ptr) elem_ptr = B.CreatePointerCast(elem_ptr, i8ptr);
      std::string elem_name = (expr->left->kind == Expr::Kind::AsArray && !expr->left->var_name.empty())
        ? expr->left->var_name : array_elem_type_name(expr->left.get(), env);
      size_t H = array_header_size(elem_name, env.layout_map);
      Value* base = B.CreateGEP(B.getInt8Ty(), elem_ptr, B.getInt64(-static_cast<int64_t>(H)));
      Function* free_fn = M->getFunction("free");
      if (!free_fn) {
        FunctionType* free_ty = FunctionType::get(B.getVoidTy(), {i8ptr}, false);
        free_fn = Function::Create(free_ty, GlobalValue::ExternalLinkage, "free", M);
      }
      B.CreateCall(free_fn, base);
      return nullptr;
    }
    case Expr::Kind::AsHeap:
      return emit_expr(env, expr->left.get());
    case Expr::Kind::AsArray:
      return emit_expr(env, expr->left.get());
    case Expr::Kind::AddrOf: {
      if (!expr->left || expr->left->kind != Expr::Kind::VarRef) return nullptr;
      Value* alloca = vars_lookup(env, expr->left->var_name);
      if (!alloca) return nullptr;
      return B.CreatePointerCast(alloca, PointerType::get(Type::getInt8Ty(ctx), 0));
    }
    case Expr::Kind::Load: {
      Value* ptr = emit_expr(env, expr->left.get());
      if (!ptr) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (ptr->getType() != i8ptr) ptr = B.CreatePointerCast(ptr, i8ptr);
      Value* as_i64 = B.CreatePointerCast(ptr, B.getInt64Ty()->getPointerTo());
      return B.CreateLoad(B.getInt64Ty(), as_i64, "load");
    }
    case Expr::Kind::LoadI32: {
      Value* ptr = emit_expr(env, expr->left.get());
      if (!ptr) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (ptr->getType() != i8ptr) ptr = B.CreatePointerCast(ptr, i8ptr);
      Value* as_i32 = B.CreatePointerCast(ptr, B.getInt32Ty()->getPointerTo());
      Value* v32 = B.CreateLoad(B.getInt32Ty(), as_i32, "load_i32");
      return B.CreateZExt(v32, B.getInt64Ty());
    }
    case Expr::Kind::LoadF64: {
      Value* ptr = emit_expr(env, expr->left.get());
      if (!ptr) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (ptr->getType() != i8ptr) ptr = B.CreatePointerCast(ptr, i8ptr);
      Value* as_f64 = B.CreatePointerCast(ptr, B.getDoubleTy()->getPointerTo());
      return B.CreateLoad(B.getDoubleTy(), as_f64, "load_f64");
    }
    case Expr::Kind::LoadPtr: {
      Value* ptr = emit_expr(env, expr->left.get());
      if (!ptr) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      Value* as_i64ptr = B.CreatePointerCast(ptr, B.getInt64Ty()->getPointerTo());
      Value* val = B.CreateLoad(B.getInt64Ty(), as_i64ptr, "load_ptr");
      return B.CreateIntToPtr(val, i8ptr);
    }
    case Expr::Kind::Store: {
      Value* ptr = emit_expr(env, expr->left.get());
      Value* val = emit_expr(env, expr->right.get());
      if (!ptr || !val) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (ptr->getType() != i8ptr) ptr = B.CreatePointerCast(ptr, i8ptr);
      FfiType val_ty = expr_type(expr->right.get(), prog, env.var_types);
      if (val_ty == FfiType::F64) {
        Value* as_f64 = B.CreatePointerCast(ptr, B.getDoubleTy()->getPointerTo());
        B.CreateStore(val, as_f64);
      } else if (val_ty == FfiType::Ptr) {
        Value* as_i64 = B.CreatePointerCast(ptr, B.getInt64Ty()->getPointerTo());
        Value* val_i64 = val->getType()->isPointerTy() ? B.CreatePtrToInt(val, B.getInt64Ty()) : val;
        B.CreateStore(val_i64, as_i64);
      } else {
        Value* as_i64 = B.CreatePointerCast(ptr, B.getInt64Ty()->getPointerTo());
        if (val->getType() == B.getDoubleTy()) val = B.CreateFPToSI(val, B.getInt64Ty());
        B.CreateStore(val, as_i64);
      }
      return B.getInt64(0);
    }
    case Expr::Kind::LoadField: {
      if (!env.layout_map) { s_codegen_error = "load_field: no struct layout map"; return nullptr; }
      Value* base = emit_expr(env, expr->left.get());
      if (!base) {
        if (s_codegen_error.empty()) s_codegen_error = "load_field: base expression failed";
        return nullptr;
      }
      FfiType field_ty = FfiType::Void;
      Value* field_ptr = emit_field_address(env, base, expr->load_field_struct,
                                            {expr->load_field_field}, &field_ty);
      if (!field_ptr || field_ty == FfiType::Void) {
        if (s_codegen_error.empty())
          s_codegen_error = "load_field: struct '" + expr->load_field_struct +
                            "' field '" + expr->load_field_field + "' not found";
        return nullptr;
      }
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (field_ty == FfiType::F64) {
        field_ptr = B.CreatePointerCast(field_ptr, B.getDoubleTy()->getPointerTo());
        return B.CreateLoad(B.getDoubleTy(), field_ptr, "load_field");
      }
      if (field_ty == FfiType::Ptr) {
        field_ptr = B.CreatePointerCast(field_ptr, B.getInt64Ty()->getPointerTo());
        Value* v = B.CreateLoad(B.getInt64Ty(), field_ptr);
        return B.CreateIntToPtr(v, i8ptr);
      }
      field_ptr = B.CreatePointerCast(field_ptr, B.getInt64Ty()->getPointerTo());
      return B.CreateLoad(B.getInt64Ty(), field_ptr, "load_field");
    }
    case Expr::Kind::StoreField: {
      if (!env.layout_map) return nullptr;
      Value* base = emit_expr(env, expr->left.get());
      Value* val = emit_expr(env, expr->right.get());
      if (!base || !val) return nullptr;
      FfiType field_ty = FfiType::Void;
      Value* field_ptr = emit_field_address(env, base, expr->load_field_struct,
                                            {expr->load_field_field}, &field_ty);
      if (!field_ptr || field_ty == FfiType::Void) return nullptr;
      if (field_ty == FfiType::F64) {
        field_ptr = B.CreatePointerCast(field_ptr, B.getDoubleTy()->getPointerTo());
        if (val->getType() != B.getDoubleTy()) val = B.CreateSIToFP(val, B.getDoubleTy());
        B.CreateStore(val, field_ptr);
      } else if (field_ty == FfiType::Ptr) {
        field_ptr = B.CreatePointerCast(field_ptr, B.getInt64Ty()->getPointerTo());
        Value* val_i64 = val->getType()->isPointerTy() ? B.CreatePtrToInt(val, B.getInt64Ty()) : val;
        B.CreateStore(val_i64, field_ptr);
      } else {
        field_ptr = B.CreatePointerCast(field_ptr, B.getInt64Ty()->getPointerTo());
        if (val->getType()->isPointerTy()) val = B.CreatePtrToInt(val, B.getInt64Ty());
        else if (val->getType() == B.getDoubleTy()) val = B.CreateFPToSI(val, B.getInt64Ty());
        B.CreateStore(val, field_ptr);
      }
      return B.getInt64(0);
    }
    case Expr::Kind::FieldAccess: {
      if (!expr->left || expr->field_chain.empty() || expr->load_field_struct.empty() || !env.layout_map)
        return nullptr;
      Value* base_ptr = emit_expr(env, expr->left.get());
      if (!base_ptr) return nullptr;
      FfiType field_ty = FfiType::Void;
      Value* field_ptr = emit_field_address(env, base_ptr, expr->load_field_struct, expr->field_chain, &field_ty);
      if (!field_ptr || field_ty == FfiType::Void) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (field_ty == FfiType::F64) {
        field_ptr = B.CreatePointerCast(field_ptr, B.getDoubleTy()->getPointerTo());
        return B.CreateLoad(B.getDoubleTy(), field_ptr, "field.load");
      }
      if (field_ty == FfiType::Ptr) {
        field_ptr = B.CreatePointerCast(field_ptr, B.getInt64Ty()->getPointerTo());
        Value* v = B.CreateLoad(B.getInt64Ty(), field_ptr);
        return B.CreateIntToPtr(v, i8ptr);
      }
      field_ptr = B.CreatePointerCast(field_ptr, B.getInt64Ty()->getPointerTo());
      return B.CreateLoad(B.getInt64Ty(), field_ptr, "field.load");
    }
    case Expr::Kind::Index: {
      Value* elem_base = emit_expr(env, expr->left.get());
      Value* index_val = emit_expr(env, expr->right.get());
      if (!elem_base || !index_val) {
        if (s_codegen_error.empty())
          s_codegen_error = "array index: base or index expression failed";
        return nullptr;
      }
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (elem_base->getType() != i8ptr) elem_base = B.CreatePointerCast(elem_base, i8ptr);
      if (index_val->getType() != B.getInt64Ty())
        index_val = B.CreateIntCast(index_val, B.getInt64Ty(), true);
      std::string elem_name = array_elem_type_name(expr->left.get(), env);
      size_t H = array_header_size(elem_name, env.layout_map);
      size_t elem_size = elem_size_from_type(elem_name, env.layout_map);
      FfiType elem_ty = array_element_type_from_expr(expr->left.get(), env);
      if (elem_ty == FfiType::Void) elem_ty = FfiType::I64;
      /* Only do bounds checking for tracked arrays (those with a header from
         heap_array/stack_array).  Raw pointers from casts (e.g. `str as ptr[char]`)
         have no header — reading ptr-8 would access invalid memory. */
      bool is_tracked_array = (expr->left->kind == Expr::Kind::VarRef &&
         array_elem_lookup(env, expr->left->var_name) != FfiType::Void &&
         env.raw_ptr_vars.find(expr->left->var_name) == env.raw_ptr_vars.end()) ||
         expr->left->kind == Expr::Kind::HeapArray ||
         expr->left->kind == Expr::Kind::StackArray ||
         expr->left->kind == Expr::Kind::FieldAccess;
      if (is_tracked_array) {
        Value* base = B.CreateGEP(B.getInt8Ty(), elem_base, B.getInt64(-static_cast<int64_t>(H)));
        Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
        Value* len = B.CreateLoad(B.getInt64Ty(), len_ptr, "arr.len");
        Function* rt_panic_fn = M->getFunction("rt_panic");
        if (!rt_panic_fn) {
          s_codegen_error = "array index: rt_panic not found in module";
          return nullptr;
        }
        Value* oob = B.CreateOr(
          B.CreateICmpSLT(index_val, B.getInt64(0)),
          B.CreateICmpSGE(index_val, len), "index.oob");
        BasicBlock* cont_bb = BasicBlock::Create(ctx, "index.cont", B.GetInsertBlock()->getParent());
        BasicBlock* panic_bb = BasicBlock::Create(ctx, "index.panic", B.GetInsertBlock()->getParent());
        B.CreateCondBr(oob, panic_bb, cont_bb);
        B.SetInsertPoint(panic_bb);
        const char* msg = "index out of bounds";
        Type* msg_ty = ArrayType::get(Type::getInt8Ty(ctx), strlen(msg) + 1);
        Value* msg_buf = B.CreateAlloca(msg_ty, nullptr, "panic_msg");
        B.CreateStore(ConstantDataArray::getString(ctx, msg, true), msg_buf);
        B.CreateCall(rt_panic_fn, B.CreatePointerCast(msg_buf, i8ptr));
        B.CreateUnreachable();
        B.SetInsertPoint(cont_bb);
      }
      Value* offset = B.CreateMul(index_val, B.getInt64(elem_size), "elem.offset");
      Value* elem_ptr = B.CreateGEP(B.getInt8Ty(), elem_base, offset);
      if (env.layout_map && env.layout_map->count(elem_name))
        return B.CreatePointerCast(elem_ptr, i8ptr);
      if (elem_ty == FfiType::F64) {
        elem_ptr = B.CreatePointerCast(elem_ptr, B.getDoubleTy()->getPointerTo());
        return B.CreateLoad(B.getDoubleTy(), elem_ptr, "index.load");
      }
      if (elem_ty == FfiType::Ptr) {
        elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt64Ty()->getPointerTo());
        Value* v = B.CreateLoad(B.getInt64Ty(), elem_ptr);
        return B.CreateIntToPtr(v, i8ptr);
      }
      if (elem_ty == FfiType::I32) {
        elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt32Ty()->getPointerTo());
        Value* v = B.CreateLoad(B.getInt32Ty(), elem_ptr);
        return B.CreateZExt(v, B.getInt64Ty());
      }
      if (elem_name == "i8") {
        Value* v = B.CreateLoad(B.getInt8Ty(), elem_ptr, "index.load.i8");
        return B.CreateZExt(v, B.getInt64Ty());
      }
      elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt64Ty()->getPointerTo());
      return B.CreateLoad(B.getInt64Ty(), elem_ptr, "index.load");
    }
    case Expr::Kind::Cast: {
      Value* v = emit_expr(env, expr->left.get());
      if (!v) return nullptr;
      const std::string& to = expr->var_name;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (to == "ptr" || to == "char") {
        if (v->getType()->isPointerTy()) return v;
        return nullptr;
      }
      if (to == "f64") {
        if (v->getType() == B.getDoubleTy()) return v;
        if (v->getType() == B.getInt64Ty()) return B.CreateSIToFP(v, B.getDoubleTy());
        if (v->getType() == B.getInt32Ty()) return B.CreateSIToFP(v, B.getDoubleTy());
        if (v->getType() == B.getFloatTy()) return B.CreateFPExt(v, B.getDoubleTy());
        return nullptr;
      }
      if (to == "f32") {
        if (v->getType() == B.getFloatTy()) return v;
        if (v->getType() == B.getDoubleTy()) return B.CreateFPTrunc(v, B.getFloatTy());
        if (v->getType() == B.getInt64Ty()) return B.CreateSIToFP(v, B.getFloatTy());
        if (v->getType() == B.getInt32Ty()) return B.CreateSIToFP(v, B.getFloatTy());
        return nullptr;
      }
      if (to == "i64") {
        if (v->getType() == B.getInt64Ty()) return v;
        if (v->getType() == B.getDoubleTy() || v->getType() == B.getFloatTy())
          return B.CreateFPToSI(v, B.getInt64Ty());
        if (v->getType() == B.getInt32Ty()) return B.CreateSExt(v, B.getInt64Ty());
        if (v->getType() == B.getInt8Ty()) return B.CreateSExt(v, B.getInt64Ty());
        return nullptr;
      }
      if (to == "i32") {
        if (v->getType() == B.getInt32Ty()) return v;
        if (v->getType() == B.getInt64Ty()) return B.CreateTrunc(v, B.getInt32Ty());
        if (v->getType() == B.getDoubleTy() || v->getType() == B.getFloatTy())
          return B.CreateFPToSI(v, B.getInt32Ty());
        return nullptr;
      }
      /* Cast to struct: ptr -> struct* is a no-op at runtime */
      for (const auto& s : prog->struct_defs) {
        if (s.name == to && v->getType()->isPointerTy()) return v;
      }
      return v;
    }
    case Expr::Kind::Compare: {
      Value* L = emit_expr(env, expr->left.get());
      Value* R = emit_expr(env, expr->right.get());
      if (!L || !R) {
        if (s_codegen_error.empty())
          s_codegen_error = "comparison: left or right expression failed";
        return nullptr;
      }
      FfiType tyL = expr_type(expr->left.get(), prog, env.var_types);
      FfiType tyR = expr_type(expr->right.get(), prog, env.var_types);
      if (tyL == FfiType::Ptr && tyR == FfiType::Ptr) {
        Type* i8ptr_ty = PointerType::get(Type::getInt8Ty(ctx), 0);
        if (L->getType() != i8ptr_ty) L = B.CreatePointerCast(L, i8ptr_ty);
        if (R->getType() != i8ptr_ty) R = B.CreatePointerCast(R, i8ptr_ty);

        // String content comparison when both sides are ptr[char]
        if (expr->left->inferred_ptr_element == "char" &&
            expr->right->inferred_ptr_element == "char") {
            Function* fn = M->getFunction("rt_str_eq");
            Value* result = B.CreateCall(fn, {L, R}, "str_eq");
            if (expr->compare_op == CompareOp::Eq)
                return B.CreateICmpNE(result, ConstantInt::get(B.getInt64Ty(), 0), "streq");
            else
                return B.CreateICmpEQ(result, ConstantInt::get(B.getInt64Ty(), 0), "strne");
        }

        // Pointer identity comparison for non-string pointers
        CmpInst::Predicate pred = (expr->compare_op == CompareOp::Eq) ? CmpInst::ICMP_EQ : CmpInst::ICMP_NE;
        return B.CreateICmp(pred, L, R, "cmp");
      }
      bool is_float = (tyL == FfiType::F64 || tyR == FfiType::F64);
      if (is_float) {
        if (L->getType() != B.getDoubleTy()) L = B.CreateSIToFP(L, B.getDoubleTy());
        if (R->getType() != B.getDoubleTy()) R = B.CreateSIToFP(R, B.getDoubleTy());
        CmpInst::Predicate pred;
        switch (expr->compare_op) {
          case CompareOp::Eq: pred = CmpInst::FCMP_OEQ; break;
          case CompareOp::Ne: pred = CmpInst::FCMP_ONE; break;
          case CompareOp::Lt: pred = CmpInst::FCMP_OLT; break;
          case CompareOp::Le: pred = CmpInst::FCMP_OLE; break;
          case CompareOp::Gt: pred = CmpInst::FCMP_OGT; break;
          case CompareOp::Ge: pred = CmpInst::FCMP_OGE; break;
        }
        return B.CreateFCmp(pred, L, R, "cmp");
      }
      if (L->getType() != B.getInt64Ty()) {
        Value* c = coerce_value_to_type(env, L, tyL, B.getInt64Ty());
        if (!c) { s_codegen_error = "comparison: cannot coerce left to i64"; return nullptr; }
        L = c;
      }
      if (R->getType() != B.getInt64Ty()) {
        Value* c = coerce_value_to_type(env, R, tyR, B.getInt64Ty());
        if (!c) { s_codegen_error = "comparison: cannot coerce right to i64"; return nullptr; }
        R = c;
      }
      CmpInst::Predicate pred;
      switch (expr->compare_op) {
        case CompareOp::Eq: pred = CmpInst::ICMP_EQ; break;
        case CompareOp::Ne: pred = CmpInst::ICMP_NE; break;
        case CompareOp::Lt: pred = CmpInst::ICMP_SLT; break;
        case CompareOp::Le: pred = CmpInst::ICMP_SLE; break;
        case CompareOp::Gt: pred = CmpInst::ICMP_SGT; break;
        case CompareOp::Ge: pred = CmpInst::ICMP_SGE; break;
      }
      return B.CreateICmp(pred, L, R, "cmp");
    }
  }
  if (expr && s_codegen_error.empty())
    s_codegen_error = "unknown or unsupported expression kind in emit_expr";
  return nullptr;
}

static bool emit_stmt(CodegenEnv& env, FnDef& def, Function* fn, Stmt* stmt);

/* Collect (name, type) of Let in the direct loop body only; skip loop-var name. Used for hoisting. */
static void collect_loop_body_let_types(const std::vector<StmtPtr>& body, Program* program,
    const std::unordered_map<std::string, FfiType>* var_types,
    CodegenEnv& env,
    const std::string& for_init_name,
    std::unordered_map<std::string, FfiType>& out) {
  std::unordered_map<std::string, FfiType> combined;
  if (var_types)
    for (const auto& p : *var_types) combined[p.first] = p.second;
  for (const StmtPtr& s : body) {
    if (!s) continue;
    if (s->kind == Stmt::Kind::Let) {
      if (!for_init_name.empty() && s->name == for_init_name) continue;
      FfiType ty = s->init ? expr_type_proper(s->init.get(), program, &combined, env) : FfiType::Void;
      if (ty != FfiType::Void && out.find(s->name) == out.end()) {
        out[s->name] = ty;
        combined[s->name] = ty;  /* so later lets in same body see this type */
      }
    }
  }
}

static bool emit_stmt(CodegenEnv& env, FnDef& def, Function* fn, Stmt* stmt) {
  if (!stmt) return false;
  LLVMContext& ctx = env.builder->getContext();
  IRBuilder<>& B = *env.builder;
  switch (stmt->kind) {
    case Stmt::Kind::Return: {
      Value* val = emit_expr(env, stmt->expr.get());
      if (!val) return false;
      if (def.return_type == FfiType::Void) {
        B.CreateRetVoid();
      } else {
        Type* ret_ty = ffi_type_to_llvm(def.return_type, ctx, B);
        if (val->getType() != ret_ty) {
          if (ret_ty == B.getDoubleTy() && val->getType() == B.getInt64Ty())
            val = B.CreateSIToFP(val, B.getDoubleTy());
          else if (ret_ty == B.getInt64Ty() && val->getType() == B.getDoubleTy())
            val = B.CreateFPToSI(val, B.getInt64Ty());
          else if (ret_ty->isPointerTy() && val->getType()->isPointerTy())
            val = B.CreatePointerCast(val, ret_ty);
          else
            return false;
        }
        B.CreateRet(val);
      }
      return true;
    }
    case Stmt::Kind::Let: {
      FfiType let_ty = expr_type_proper(stmt->init.get(), env.program, env.var_types, env);
      if (env.fn_var_types)
        (*env.fn_var_types)[stmt->name] = let_ty;
      AllocaInst* slot = nullptr;
      if (!env.vars_scope_stack.empty()) {
        auto it = env.vars_scope_stack.back().find(stmt->name);
        if (it != env.vars_scope_stack.back().end())
          slot = dyn_cast<AllocaInst>(it->second);
      }
      if (slot) {
        Value* init_val = emit_expr(env, stmt->init.get());
        if (!init_val) {
          if (s_codegen_error.empty())
            s_codegen_error = "let '" + stmt->name + "': init expression failed";
          return false;
        }
        Type* alloc_ty = slot->getAllocatedType();
        if (init_val->getType() != alloc_ty) {
          if (alloc_ty == B.getDoubleTy() && init_val->getType() == B.getInt64Ty())
            init_val = B.CreateSIToFP(init_val, B.getDoubleTy());
          else if (alloc_ty == B.getInt64Ty() && init_val->getType() == B.getDoubleTy())
            init_val = B.CreateFPToSI(init_val, B.getInt64Ty());
          else if (alloc_ty->isPointerTy() && init_val->getType()->isPointerTy())
            init_val = B.CreatePointerCast(init_val, alloc_ty);
          else {
            if (s_codegen_error.empty())
              s_codegen_error = "let '" + stmt->name + "': type mismatch between init and slot";
            return false;
          }
        }
        B.CreateStore(init_val, slot);
      } else {
        Type* slot_ty = (let_ty != FfiType::Void) ? ffi_type_to_llvm(let_ty, ctx, B) : nullptr;
        if (slot_ty) {
          slot = B.CreateAlloca(slot_ty, nullptr, stmt->name);
          env.vars_scope_stack.back()[stmt->name] = slot;
        } else {
          Value* init_val = emit_expr(env, stmt->init.get());
          if (!init_val) {
            if (s_codegen_error.empty())
              s_codegen_error = "let '" + stmt->name + "': init expression failed";
            return false;
          }
          slot = B.CreateAlloca(init_val->getType(), nullptr, stmt->name);
          env.vars_scope_stack.back()[stmt->name] = slot;
          B.CreateStore(init_val, slot);
          FfiType elem_ty = array_element_type_from_expr(stmt->init.get(), env);
          if (elem_ty != FfiType::Void) {
            env.array_element_scope_stack.back()[stmt->name] = elem_ty;
          } else if (stmt->init->kind == Expr::Kind::LoadField && env.layout_map) {
            Expr* e = stmt->init.get();
            auto it = env.layout_map->find(e->load_field_struct);
            if (it != env.layout_map->end()) {
              for (const auto& f : it->second.fields)
                if (f.first == e->load_field_field && f.second.type == FfiType::Ptr) {
                  env.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
                  break;
                }
            }
          } else if (stmt->init->kind == Expr::Kind::Call && init_val->getType()->isPointerTy())
            env.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
          else if (stmt->init->kind == Expr::Kind::Cast && init_val->getType()->isPointerTy()) {
            const std::string& ct = stmt->init->var_name;
            if (ct == "char") env.array_element_scope_stack.back()[stmt->name] = FfiType::I8;
            else if (ct == "i32") env.array_element_scope_stack.back()[stmt->name] = FfiType::I32;
            else if (ct == "f32") env.array_element_scope_stack.back()[stmt->name] = FfiType::F32;
            else if (ct == "f64") env.array_element_scope_stack.back()[stmt->name] = FfiType::F64;
            else env.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
            env.raw_ptr_vars.insert(stmt->name);
          }
          return true;
        }
        Value* init_val = emit_expr(env, stmt->init.get());
        if (!init_val) {
          if (s_codegen_error.empty())
            s_codegen_error = "let '" + stmt->name + "': init expression failed";
          return false;
        }
        B.CreateStore(init_val, slot);
      }
      if (let_ty == FfiType::Ptr) {
        FnPtrSig sig;
        if (codegen_lookup_fnptr_sig(env, stmt->init.get(), &sig))
          env.fnptr_scope_stack.back()[stmt->name] = sig;
      }
      FfiType elem_ty = array_element_type_from_expr(stmt->init.get(), env);
      if (elem_ty != FfiType::Void) {
        env.array_element_scope_stack.back()[stmt->name] = elem_ty;
      } else if (stmt->init->kind == Expr::Kind::LoadField && env.layout_map) {
        Expr* e = stmt->init.get();
        auto it = env.layout_map->find(e->load_field_struct);
        if (it != env.layout_map->end()) {
          for (const auto& f : it->second.fields)
            if (f.first == e->load_field_field && f.second.type == FfiType::Ptr) {
              env.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
              break;
            }
        }
      } else if (let_ty == FfiType::Ptr && stmt->init->kind == Expr::Kind::Call)
        env.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
      else if (stmt->init->kind == Expr::Kind::Cast && let_ty == FfiType::Ptr) {
        const std::string& ct = stmt->init->var_name;
        if (ct == "char") env.array_element_scope_stack.back()[stmt->name] = FfiType::I8;
        else if (ct == "i32") env.array_element_scope_stack.back()[stmt->name] = FfiType::I32;
        else if (ct == "f32") env.array_element_scope_stack.back()[stmt->name] = FfiType::F32;
        else if (ct == "f64") env.array_element_scope_stack.back()[stmt->name] = FfiType::F64;
        else env.array_element_scope_stack.back()[stmt->name] = FfiType::Ptr;
        env.raw_ptr_vars.insert(stmt->name);
      }
      if (let_ty == FfiType::Ptr && (stmt->init->kind == Expr::Kind::HeapArray || stmt->init->kind == Expr::Kind::StackArray) &&
          env.layout_map && !stmt->init->var_name.empty() && env.layout_map->count(stmt->init->var_name))
        env.array_struct_scope_stack.back()[stmt->name] = stmt->init->var_name;
      return true;
    }
    case Stmt::Kind::Expr: {
      Value* v = emit_expr(env, stmt->expr.get());
      (void)v;
      return true;
    }
    case Stmt::Kind::If: {
      Value* cond_val = emit_expr(env, stmt->cond.get());
      if (!cond_val) return false;
      if (cond_val->getType() != B.getInt1Ty()) {
        if (cond_val->getType() == B.getInt64Ty())
          cond_val = B.CreateICmpNE(cond_val, B.getInt64(0), "cond");
        else if (cond_val->getType() == B.getDoubleTy())
          cond_val = B.CreateFCmpONE(cond_val, ConstantFP::get(B.getDoubleTy(), 0.0), "cond");
        else
          return false;
      }
      BasicBlock* then_bb = BasicBlock::Create(ctx, "if.then", fn);
      BasicBlock* else_bb = BasicBlock::Create(ctx, "if.else", fn);
      BasicBlock* merge_bb = BasicBlock::Create(ctx, "if.merge", fn);
      B.CreateCondBr(cond_val, then_bb, else_bb);

      B.SetInsertPoint(then_bb);
      env.vars_scope_stack.push_back({});
      env.array_element_scope_stack.push_back({});
      env.array_struct_scope_stack.push_back({});
      env.fnptr_scope_stack.push_back({});
      for (StmtPtr& s : stmt->then_body) {
        if (!emit_stmt(env, def, fn, s.get())) return false;
      }
      env.vars_scope_stack.pop_back();
      env.array_element_scope_stack.pop_back();
      env.array_struct_scope_stack.pop_back();
      env.fnptr_scope_stack.pop_back();
      if (!B.GetInsertBlock()->getTerminator())
        B.CreateBr(merge_bb);

      B.SetInsertPoint(else_bb);
      if (stmt->else_body.empty()) {
        B.CreateBr(merge_bb);
      } else {
        env.vars_scope_stack.push_back({});
        env.array_element_scope_stack.push_back({});
        env.array_struct_scope_stack.push_back({});
        env.fnptr_scope_stack.push_back({});
        for (StmtPtr& s : stmt->else_body) {
          if (!emit_stmt(env, def, fn, s.get())) return false;
        }
        env.vars_scope_stack.pop_back();
        env.array_element_scope_stack.pop_back();
        env.array_struct_scope_stack.pop_back();
        env.fnptr_scope_stack.pop_back();
        if (!B.GetInsertBlock()->getTerminator())
          B.CreateBr(merge_bb);
      }
      B.SetInsertPoint(merge_bb);
      return true;
    }
    case Stmt::Kind::Assign: {
      if (!stmt->expr || !stmt->init) return false;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      Value* val = emit_expr(env, stmt->init.get());
      if (!val) {
        if (s_codegen_error.empty())
          s_codegen_error = "assignment: right-hand side expression failed";
        return false;
      }
      if (stmt->expr->kind == Expr::Kind::VarRef) {
        Value* alloca_val = vars_lookup(env, stmt->expr->var_name);
        if (!alloca_val) {
          if (s_codegen_error.empty())
            s_codegen_error = "assignment to unknown variable '" + stmt->expr->var_name + "'";
          return false;
        }
        AllocaInst* alloca = cast<AllocaInst>(alloca_val);
        if (val->getType() != alloca->getAllocatedType()) {
          if (alloca->getAllocatedType() == B.getDoubleTy() && val->getType() == B.getInt64Ty())
            val = B.CreateSIToFP(val, B.getDoubleTy());
          else if (alloca->getAllocatedType() == B.getInt64Ty() && val->getType() == B.getDoubleTy())
            val = B.CreateFPToSI(val, B.getInt64Ty());
          else if (alloca->getAllocatedType() == i8ptr && val->getType()->isPointerTy())
            val = B.CreatePointerCast(val, i8ptr);
          else if (alloca->getAllocatedType() == B.getInt64Ty() && val->getType()->isPointerTy())
            val = B.CreatePtrToInt(val, B.getInt64Ty());
          else {
            if (s_codegen_error.empty())
              s_codegen_error = "assignment to '" + stmt->expr->var_name + "': type mismatch";
            return false;
          }
        }
        B.CreateStore(val, alloca);
        if (alloca->getAllocatedType() == i8ptr && !env.fnptr_scope_stack.empty()) {
          FnPtrSig sig;
          if (codegen_lookup_fnptr_sig(env, stmt->init.get(), &sig))
            env.fnptr_scope_stack.back()[stmt->expr->var_name] = sig;
        }
        return true;
      }
      if (stmt->expr->kind == Expr::Kind::Index) {
        Expr* base_expr = stmt->expr->left.get();
        Value* elem_base = emit_expr(env, base_expr);
        Value* index_val = emit_expr(env, stmt->expr->right.get());
        if (!elem_base || !index_val) {
          if (s_codegen_error.empty())
            s_codegen_error = "assign to array element: base or index expression failed";
          return false;
        }
        if (elem_base->getType() != i8ptr) elem_base = B.CreatePointerCast(elem_base, i8ptr);
        if (index_val->getType() != B.getInt64Ty())
          index_val = B.CreateIntCast(index_val, B.getInt64Ty(), true);
        std::string elem_name = array_elem_type_name(base_expr, env);
        size_t H = array_header_size(elem_name, env.layout_map);
        size_t elem_size = elem_size_from_type(elem_name, env.layout_map);
        FfiType elem_ty = array_element_type_from_expr(base_expr, env);
        if (elem_ty == FfiType::Void) elem_ty = FfiType::I64;
        /* Only bounds-check tracked arrays with headers */
        bool is_tracked_array = (base_expr->kind == Expr::Kind::VarRef &&
           array_elem_lookup(env, base_expr->var_name) != FfiType::Void &&
           env.raw_ptr_vars.find(base_expr->var_name) == env.raw_ptr_vars.end()) ||
           base_expr->kind == Expr::Kind::HeapArray ||
           base_expr->kind == Expr::Kind::StackArray ||
           base_expr->kind == Expr::Kind::FieldAccess;
        if (is_tracked_array) {
          Value* base = B.CreateGEP(B.getInt8Ty(), elem_base, B.getInt64(-static_cast<int64_t>(H)));
          Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
          Value* len = B.CreateLoad(B.getInt64Ty(), len_ptr, "arr.len");
          Function* rt_panic_fn = env.module->getFunction("rt_panic");
          if (!rt_panic_fn) {
            if (s_codegen_error.empty())
              s_codegen_error = "assign to array element: rt_panic not found in module";
            return false;
          }
          Value* oob = B.CreateOr(
            B.CreateICmpSLT(index_val, B.getInt64(0)),
            B.CreateICmpSGE(index_val, len), "assign.oob");
          BasicBlock* cont_bb = BasicBlock::Create(ctx, "assign.cont", fn);
          BasicBlock* panic_bb = BasicBlock::Create(ctx, "assign.panic", fn);
          B.CreateCondBr(oob, panic_bb, cont_bb);
          B.SetInsertPoint(panic_bb);
          const char* msg = "index out of bounds";
          Type* msg_ty = ArrayType::get(Type::getInt8Ty(ctx), strlen(msg) + 1);
          Value* msg_buf = B.CreateAlloca(msg_ty, nullptr, "panic_msg");
          B.CreateStore(ConstantDataArray::getString(ctx, msg, true), msg_buf);
          B.CreateCall(rt_panic_fn, B.CreatePointerCast(msg_buf, i8ptr));
          B.CreateUnreachable();
          B.SetInsertPoint(cont_bb);
        }
        Value* offset = B.CreateMul(index_val, B.getInt64(elem_size), "elem.offset");
        Value* elem_ptr = B.CreateGEP(B.getInt8Ty(), elem_base, offset);
        if (elem_name == "i8") {
          if (val->getType() != B.getInt8Ty()) val = B.CreateTrunc(B.CreateIntCast(val, B.getInt64Ty(), true), B.getInt8Ty());
          B.CreateStore(val, elem_ptr);
        } else if (env.layout_map && env.layout_map->count(elem_name) && val->getType()->isPointerTy()) {
          /* Array of struct: RHS is pointer to struct; copy struct by bytes. */
          Value* src = (val->getType() != i8ptr) ? B.CreatePointerCast(val, i8ptr) : val;
          Type* i64_ptr = B.getInt64Ty()->getPointerTo();
          for (size_t off = 0; off + 8 <= elem_size; off += 8) {
            Value* d = (off == 0) ? elem_ptr : B.CreateGEP(B.getInt8Ty(), elem_ptr, B.getInt64(off));
            Value* s = (off == 0) ? src : B.CreateGEP(B.getInt8Ty(), src, B.getInt64(off));
            d = B.CreatePointerCast(d, i64_ptr);
            s = B.CreatePointerCast(s, i64_ptr);
            B.CreateStore(B.CreateLoad(B.getInt64Ty(), s), d);
          }
        } else if (val->getType()->isPointerTy() && stmt->init->kind == Expr::Kind::Call && env.program && env.layout_map) {
          /* RHS is Call to user fn returning struct: copy struct by value so element gets full struct, not just ptr. */
          std::string struct_name;
          for (const FnDef& d : env.program->user_fns)
            if (d.name == stmt->init->callee && d.return_type == FfiType::Ptr && !d.return_type_name.empty() &&
                env.layout_map->count(d.return_type_name)) {
              struct_name = d.return_type_name;
              break;
            }
          if (!struct_name.empty()) {
            size_t sz = elem_size_from_type(struct_name, env.layout_map);
            if (sz > 0) {
              Value* src = (val->getType() != i8ptr) ? B.CreatePointerCast(val, i8ptr) : val;
              /* Use struct size for destination stride (elem_size from LHS may be wrong if scope wasn't set). */
              Value* offset_bytes = B.CreateMul(index_val, B.getInt64(sz), "struct.elem.offset");
              Value* dst_ptr = B.CreateGEP(B.getInt8Ty(), elem_base, offset_bytes);
              Type* i64_ptr = B.getInt64Ty()->getPointerTo();
              for (size_t off = 0; off + 8 <= sz; off += 8) {
                Value* d = (off == 0) ? dst_ptr : B.CreateGEP(B.getInt8Ty(), dst_ptr, B.getInt64(off));
                Value* s = (off == 0) ? src : B.CreateGEP(B.getInt8Ty(), src, B.getInt64(off));
                d = B.CreatePointerCast(d, i64_ptr);
                s = B.CreatePointerCast(s, i64_ptr);
                B.CreateStore(B.CreateLoad(B.getInt64Ty(), s), d);
              }
              Function* free_fn = env.module->getFunction("free");
              if (!free_fn) {
                FunctionType* free_ty = FunctionType::get(B.getVoidTy(), {i8ptr}, false);
                free_fn = Function::Create(free_ty, GlobalValue::ExternalLinkage, "free", env.module);
              }
              B.CreateCall(free_fn, src);
              return true;
            }
          }
        }
        if (elem_ty == FfiType::F64) {
          elem_ptr = B.CreatePointerCast(elem_ptr, B.getDoubleTy()->getPointerTo());
          if (val->getType() != B.getDoubleTy()) val = B.CreateSIToFP(val, B.getDoubleTy());
          B.CreateStore(val, elem_ptr);
        } else if (elem_ty == FfiType::Ptr) {
          elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt64Ty()->getPointerTo());
          Value* val_i64 = val->getType()->isPointerTy() ? B.CreatePtrToInt(val, B.getInt64Ty()) : val;
          B.CreateStore(val_i64, elem_ptr);
        } else {
          elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt64Ty()->getPointerTo());
          if (val->getType() == B.getDoubleTy()) val = B.CreateFPToSI(val, B.getInt64Ty());
          else if (val->getType()->isPointerTy()) val = B.CreatePtrToInt(val, B.getInt64Ty());
          B.CreateStore(val, elem_ptr);
        }
        return true;
      }
      if (stmt->expr->kind == Expr::Kind::FieldAccess) {
        Expr* fa = stmt->expr.get();
        if (!fa->left || fa->field_chain.empty() || fa->load_field_struct.empty() || !env.layout_map)
          return false;
        Value* base_ptr = emit_expr(env, fa->left.get());
        if (!base_ptr) return false;
        FfiType field_ty = FfiType::Void;
        Value* field_ptr = emit_field_address(env, base_ptr, fa->load_field_struct, fa->field_chain, &field_ty);
        if (!field_ptr || field_ty == FfiType::Void) return false;
        if (field_ty == FfiType::F64) {
          field_ptr = B.CreatePointerCast(field_ptr, B.getDoubleTy()->getPointerTo());
          if (val->getType() != B.getDoubleTy()) val = B.CreateSIToFP(val, B.getDoubleTy());
          B.CreateStore(val, field_ptr);
        } else if (field_ty == FfiType::Ptr) {
          field_ptr = B.CreatePointerCast(field_ptr, B.getInt64Ty()->getPointerTo());
          Value* val_i64 = val->getType()->isPointerTy() ? B.CreatePtrToInt(val, B.getInt64Ty()) : val;
          B.CreateStore(val_i64, field_ptr);
        } else {
          field_ptr = B.CreatePointerCast(field_ptr, B.getInt64Ty()->getPointerTo());
          if (val->getType()->isPointerTy()) val = B.CreatePtrToInt(val, B.getInt64Ty());
          else if (val->getType() == B.getDoubleTy()) val = B.CreateFPToSI(val, B.getInt64Ty());
          B.CreateStore(val, field_ptr);
        }
        return true;
      }
      return false;
    }
    case Stmt::Kind::For: {
      if (!stmt->cond) return false;
      env.vars_scope_stack.push_back({});
      env.array_element_scope_stack.push_back({});
      env.array_struct_scope_stack.push_back({});
      env.fnptr_scope_stack.push_back({});
      if (stmt->for_init) {
        if (!emit_stmt(env, def, fn, stmt->for_init.get())) {
          if (s_codegen_error.empty())
            s_codegen_error = "for loop: init statement failed";
          env.vars_scope_stack.pop_back();
          env.array_element_scope_stack.pop_back();
          env.array_struct_scope_stack.pop_back();
          env.fnptr_scope_stack.pop_back();
          return false;
        }
      }
      std::string for_init_name;
      if (stmt->for_init && stmt->for_init->kind == Stmt::Kind::Let)
        for_init_name = stmt->for_init->name;
      std::unordered_map<std::string, FfiType> hoisted;
      collect_loop_body_let_types(stmt->body, env.program, env.var_types, env, for_init_name, hoisted);
      BasicBlock* preheader_bb = BasicBlock::Create(ctx, "for.preheader", fn);
      BasicBlock* cond_bb = BasicBlock::Create(ctx, "for.cond", fn);
      BasicBlock* body_bb = BasicBlock::Create(ctx, "for.body", fn);
      BasicBlock* exit_bb = BasicBlock::Create(ctx, "for.exit", fn);
      B.CreateBr(preheader_bb);
      B.SetInsertPoint(preheader_bb);
      for (const auto& p : hoisted) {
        Type* ty = ffi_type_to_llvm(p.second, ctx, B);
        AllocaInst* a = B.CreateAlloca(ty, nullptr, p.first + ".loop");
        env.vars_scope_stack.back()[p.first] = a;
      }
      B.CreateBr(cond_bb);
      B.SetInsertPoint(cond_bb);
      Value* cond_val = emit_expr(env, stmt->cond.get());
      if (!cond_val) {
        if (s_codegen_error.empty())
          s_codegen_error = "for loop: condition expression failed";
        env.vars_scope_stack.pop_back();
        env.array_element_scope_stack.pop_back();
        env.array_struct_scope_stack.pop_back();
        env.fnptr_scope_stack.pop_back();
        return false;
      }
      if (cond_val->getType() != B.getInt1Ty()) {
        if (cond_val->getType() == B.getInt64Ty())
          cond_val = B.CreateICmpNE(cond_val, B.getInt64(0), "for.cond");
        else if (cond_val->getType() == B.getDoubleTy())
          cond_val = B.CreateFCmpONE(cond_val, ConstantFP::get(B.getDoubleTy(), 0.0), "for.cond");
        else {
          if (s_codegen_error.empty())
            s_codegen_error = "for loop: condition must be i64, f64, or bool";
          env.vars_scope_stack.pop_back();
          env.array_element_scope_stack.pop_back();
          env.array_struct_scope_stack.pop_back();
          env.fnptr_scope_stack.pop_back();
          return false;
        }
      }
      B.CreateCondBr(cond_val, body_bb, exit_bb);
      B.SetInsertPoint(body_bb);
      for (size_t bi = 0; bi < stmt->body.size(); ++bi) {
        Stmt* body_stmt = stmt->body[bi].get();
        if (!emit_stmt(env, def, fn, body_stmt)) {
          if (s_codegen_error.empty()) {
            const char* kind_str = "statement";
            if (body_stmt) {
              switch (body_stmt->kind) {
                case Stmt::Kind::Let: kind_str = "let"; break;
                case Stmt::Kind::Assign: kind_str = "assign"; break;
                case Stmt::Kind::Expr: kind_str = "expr"; break;
                case Stmt::Kind::For: kind_str = "for"; break;
                case Stmt::Kind::If: kind_str = "if"; break;
                case Stmt::Kind::Return: kind_str = "return"; break;
              }
            }
            s_codegen_error = "inside for loop, body " + std::string(kind_str) + " at index " + std::to_string(bi) + " failed (no detail)";
          } else
            s_codegen_error = "inside for loop, body statement at index " + std::to_string(bi) + ": " + s_codegen_error;
          env.vars_scope_stack.pop_back();
          env.array_element_scope_stack.pop_back();
          env.array_struct_scope_stack.pop_back();
          env.fnptr_scope_stack.pop_back();
          return false;
        }
      }
      if (stmt->for_update) {
        if (!emit_stmt(env, def, fn, stmt->for_update.get())) {
          env.vars_scope_stack.pop_back();
          env.array_element_scope_stack.pop_back();
          env.array_struct_scope_stack.pop_back();
          env.fnptr_scope_stack.pop_back();
          return false;
        }
      }
      B.CreateBr(cond_bb);
      B.SetInsertPoint(exit_bb);
      env.vars_scope_stack.pop_back();
      env.array_element_scope_stack.pop_back();
      env.array_struct_scope_stack.pop_back();
      env.fnptr_scope_stack.pop_back();
      return true;
    }
  }
  if (stmt && s_codegen_error.empty())
    s_codegen_error = "unknown or unsupported statement kind in emit_stmt";
  return false;
}

static bool emit_user_fn_body(CodegenEnv& env, FnDef& def, Function* fn) {
  LLVMContext& ctx = env.builder->getContext();
  IRBuilder<>& B = *env.builder;
  Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
  BasicBlock* entry = &fn->getEntryBlock();
  B.SetInsertPoint(entry);
  std::vector<std::unordered_map<std::string, Value*>> saved_vars_stack = std::move(env.vars_scope_stack);
  std::vector<std::unordered_map<std::string, FfiType>> saved_array_stack = std::move(env.array_element_scope_stack);
  std::vector<std::unordered_map<std::string, std::string>> saved_array_struct_stack = std::move(env.array_struct_scope_stack);
  std::vector<std::unordered_map<std::string, FnPtrSig>> saved_fnptr_stack = std::move(env.fnptr_scope_stack);
  std::unordered_set<std::string> saved_raw_ptr_vars = std::move(env.raw_ptr_vars);
  env.vars_scope_stack.push_back({});
  env.array_element_scope_stack.push_back({});
  env.array_struct_scope_stack.push_back({});
  env.fnptr_scope_stack.push_back({});
  env.raw_ptr_vars.clear();
  std::unordered_map<std::string, FfiType> fn_var_types;
  for (const auto& p : def.params)
    fn_var_types[p.first] = p.second;
  const std::unordered_map<std::string, FfiType>* saved_var_types = env.var_types;
  std::unordered_map<std::string, FfiType>* saved_fn_var_types = env.fn_var_types;
  env.var_types = &fn_var_types;
  env.fn_var_types = &fn_var_types;
  auto arg_it = fn->arg_begin();
  for (size_t j = 0; j < def.params.size(); ++j, ++arg_it) {
    Type* ty = ffi_type_to_llvm(def.params[j].second, ctx, B);
    AllocaInst* alloca = B.CreateAlloca(ty, nullptr, def.params[j].first + ".param");
    B.CreateStore(arg_it, alloca);
    env.vars_scope_stack.back()[def.params[j].first] = alloca;
    if (def.params[j].second == FfiType::Ptr)
      env.array_element_scope_stack.back()[def.params[j].first] = FfiType::Ptr;
  }
  for (StmtPtr& stmt : def.body) {
    if (!emit_stmt(env, def, fn, stmt.get())) return false;
  }
  if (!B.GetInsertBlock()->getTerminator()) {
    if (def.return_type == FfiType::Void)
      B.CreateRetVoid();
    else
      B.CreateUnreachable();  /* non-void: merge after if/elif/else is dead when all branches return */
  }
  env.var_types = saved_var_types;
  env.fn_var_types = saved_fn_var_types;
  env.vars_scope_stack = std::move(saved_vars_stack);
  env.array_element_scope_stack = std::move(saved_array_stack);
  env.array_struct_scope_stack = std::move(saved_array_struct_stack);
  env.fnptr_scope_stack = std::move(saved_fnptr_stack);
  env.raw_ptr_vars = std::move(saved_raw_ptr_vars);
  return true;
}

static void hoist_allocas_to_entry(llvm::Function* fn) {
  using namespace llvm;
  BasicBlock& entry = fn->getEntryBlock();
  // Insert hoisted allocas after any existing allocas at the start of the entry block.
  Instruction* insert_before = entry.getFirstNonPHI();
  while (insert_before && isa<AllocaInst>(insert_before))
    insert_before = insert_before->getNextNode();
  if (!insert_before)
    insert_before = entry.getTerminator();

  for (BasicBlock& bb : *fn) {
    if (&bb == &entry) continue;
    SmallVector<AllocaInst*, 16> to_hoist;
    for (Instruction& inst : bb)
      if (auto* ai = dyn_cast<AllocaInst>(&inst))
        if (isa<ConstantInt>(ai->getArraySize()))
          to_hoist.push_back(ai);
    for (AllocaInst* ai : to_hoist)
      ai->moveBefore(insert_before);
  }
}

std::unique_ptr<llvm::Module> codegen(llvm::LLVMContext& ctx, Program* program) {
  s_codegen_error.clear();
  auto module = std::make_unique<Module>("fusion", ctx);
  IRBuilder<> builder(ctx);
  Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
  LayoutMap layout_map;
  if (program && !program->struct_defs.empty())
    layout_map = build_layout_map(program->struct_defs);
  CodegenEnv env;
  env.program = program;
  env.module = module.get();
  env.builder = &builder;
  env.layout_map = layout_map.empty() ? nullptr : &layout_map;

  /* Declare runtime functions */
  FunctionType* void_ty = FunctionType::get(builder.getVoidTy(), false);
  FunctionType* print_cstring_ty = FunctionType::get(builder.getVoidTy(), {i8ptr, builder.getInt64Ty()}, false);
  FunctionType* panic_ty = FunctionType::get(builder.getVoidTy(), i8ptr, false);
  FunctionType* dlopen_ty = FunctionType::get(i8ptr, i8ptr, false);
  FunctionType* dlsym_ty = FunctionType::get(i8ptr, {i8ptr, i8ptr}, false);
  FunctionType* dlerror_ty = FunctionType::get(i8ptr, false);
  FunctionType* ffi_sig_create_ty = FunctionType::get(i8ptr,
      {builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty()->getPointerTo()}, false);
  FunctionType* ffi_call_ty = FunctionType::get(builder.getInt32Ty(),
      {i8ptr, i8ptr, i8ptr, i8ptr}, false);
  FunctionType* ffi_error_ty = FunctionType::get(i8ptr, false);

  Function::Create(print_cstring_ty, GlobalValue::ExternalLinkage, "rt_print_cstring", module.get());
  Function::Create(FunctionType::get(i8ptr, false), GlobalValue::ExternalLinkage, "rt_read_line", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), false), GlobalValue::ExternalLinkage, "rt_read_key", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), false), GlobalValue::ExternalLinkage, "rt_terminal_height", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), false), GlobalValue::ExternalLinkage, "rt_terminal_width", module.get());
  Function::Create(FunctionType::get(builder.getVoidTy(), {builder.getInt64Ty()}, false), GlobalValue::ExternalLinkage, "rt_flush", module.get());
  Function::Create(FunctionType::get(builder.getVoidTy(), {builder.getInt64Ty()}, false), GlobalValue::ExternalLinkage, "rt_sleep", module.get());
  Function::Create(FunctionType::get(i8ptr, builder.getInt64Ty(), false), GlobalValue::ExternalLinkage, "rt_chr", module.get());
  Function::Create(FunctionType::get(i8ptr, builder.getInt64Ty(), false), GlobalValue::ExternalLinkage, "rt_to_str_i64", module.get());
  Function::Create(FunctionType::get(i8ptr, builder.getDoubleTy(), false), GlobalValue::ExternalLinkage, "rt_to_str_f64", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_from_str_i64", module.get());
  Function::Create(FunctionType::get(builder.getDoubleTy(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_from_str_f64", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_concat", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_dup", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_open", module.get());
  Function::Create(FunctionType::get(builder.getVoidTy(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_close", module.get());
  Function::Create(FunctionType::get(i8ptr, i8ptr, false), GlobalValue::ExternalLinkage, "rt_read_line_file", module.get());
  Function::Create(FunctionType::get(builder.getVoidTy(), {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_write_file_ptr", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), {i8ptr, i8ptr, builder.getInt64Ty()}, false), GlobalValue::ExternalLinkage, "rt_write_bytes", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), {i8ptr, i8ptr, builder.getInt64Ty()}, false), GlobalValue::ExternalLinkage, "rt_read_bytes", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_eof_file", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_line_count_file", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr, i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_http_request", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), false), GlobalValue::ExternalLinkage, "rt_http_status", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_upper", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_lower", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_contains", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_strip", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_find", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_split", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_str_eq", module.get());
  Function::Create(panic_ty, GlobalValue::ExternalLinkage, "rt_panic", module.get());
  Function::Create(dlopen_ty, GlobalValue::ExternalLinkage, "rt_dlopen", module.get());
  Function::Create(dlsym_ty, GlobalValue::ExternalLinkage, "rt_dlsym", module.get());
  Function::Create(dlerror_ty, GlobalValue::ExternalLinkage, "rt_dlerror_last", module.get());
  Function::Create(ffi_sig_create_ty, GlobalValue::ExternalLinkage, "rt_ffi_sig_create", module.get());
  Function::Create(ffi_call_ty, GlobalValue::ExternalLinkage, "rt_ffi_call", module.get());
  Function::Create(ffi_error_ty, GlobalValue::ExternalLinkage, "rt_ffi_error_last", module.get());

  /* Create one global per lib for the dlopen handle (visible to all functions; filled in main) */
  if (program) {
    for (size_t idx = 0; idx < program->libs.size(); ++idx) {
      const ExternLib& lib = program->libs[idx];
      GlobalVariable* gv = new GlobalVariable(i8ptr, false, GlobalValue::InternalLinkage,
          ConstantPointerNull::get(cast<PointerType>(i8ptr)),
          "fusion.lib_handle_" + std::to_string(idx));
      module->insertGlobalVariable(gv);
      gv->setSection(".data");
      env.lib_handles[lib.name] = gv;
    }
  }

  /* Create user functions and emit bodies */
  if (program) {
    for (FnDef& def : program->user_fns) {
      std::vector<Type*> param_types;
      for (const auto& p : def.params)
        param_types.push_back(ffi_type_to_llvm(p.second, ctx, builder));
      Type* ret_ty = ffi_type_to_llvm(def.return_type, ctx, builder);
      FunctionType* fn_ty = FunctionType::get(ret_ty, param_types, false);
      Function* fn = Function::Create(fn_ty, GlobalValue::InternalLinkage, def.name, module.get());
      BasicBlock::Create(ctx, "entry", fn);
      env.user_fns[def.name] = fn;
    }
    for (FnDef& def : program->user_fns) {
      Function* fn = env.user_fns[def.name];
      if (!fn || !emit_user_fn_body(env, def, fn)) return nullptr;
      hoist_allocas_to_entry(fn);
    }
  }

  FunctionType* main_ty = FunctionType::get(builder.getVoidTy(), false);
  Function* main_fn = Function::Create(main_ty, GlobalValue::ExternalLinkage, "fusion_main", module.get());
  BasicBlock* entry = BasicBlock::Create(ctx, "entry", main_fn);
  builder.SetInsertPoint(entry);

  /* Emit dlopen + null check for each lib (store into globals created above) */
  for (size_t idx = 0; idx < (program ? program->libs.size() : 0); ++idx) {
    const ExternLib& lib = program->libs[idx];
    Value* handle_slot = env.lib_handles[lib.name];

    /* Path string on stack to avoid GlobalVariable (LLVM 18 CreateGlobalStringPtr can crash) */
    Type* path_array_ty = ArrayType::get(Type::getInt8Ty(ctx), lib.path.size() + 1);
    Value* path_buf = builder.CreateAlloca(path_array_ty, nullptr, "lib_path");
    Constant* path_const = ConstantDataArray::getString(ctx, lib.path, true);
    builder.CreateStore(path_const, path_buf);
    Value* path_ptr = builder.CreatePointerCast(path_buf, i8ptr);
    Function* rt_dlopen = module->getFunction("rt_dlopen");
    Function* rt_panic = module->getFunction("rt_panic");
    Function* rt_dlerror = module->getFunction("rt_dlerror_last");
    if (!rt_dlopen || !rt_panic || !rt_dlerror) {
      s_codegen_error = "rt_dlopen/rt_panic/rt_dlerror not found";
      return nullptr;
    }
    Value* h = builder.CreateCall(rt_dlopen, path_ptr);
    builder.CreateStore(h, handle_slot, true);
    Value* is_null = builder.CreateIsNull(h);
    BasicBlock* ok_bb = BasicBlock::Create(ctx, "dlopen.ok", main_fn);
    BasicBlock* fail_bb = BasicBlock::Create(ctx, "dlopen.fail", main_fn);
    builder.CreateCondBr(is_null, fail_bb, ok_bb);
    builder.SetInsertPoint(fail_bb);
    Value* err = builder.CreateCall(rt_dlerror);
    builder.CreateCall(rt_panic, err);
    builder.CreateUnreachable();
    builder.SetInsertPoint(ok_bb);
  }

  /* Emit top-level items in order: let = alloca + store, stmt = emit_stmt, expr = emit */
  if (program) {
    std::unordered_map<std::string, FfiType> top_var_types;
    const std::unordered_map<std::string, FfiType>* saved_var_types = env.var_types;
    std::unordered_map<std::string, FfiType>* saved_fn_var_types = env.fn_var_types;
    env.var_types = &top_var_types;
    env.fn_var_types = &top_var_types;
    env.vars_scope_stack.push_back({});
    env.array_element_scope_stack.push_back({});
    env.array_struct_scope_stack.push_back({});
    env.fnptr_scope_stack.push_back({});
    FnDef dummy_main;
    dummy_main.return_type = FfiType::Void;
    for (size_t idx = 0; idx < program->top_level.size(); ++idx) {
      const TopLevelItem& item = program->top_level[idx];
      if (const LetBinding* binding = std::get_if<LetBinding>(&item)) {
        FfiType ty = binding_type(*binding, program);
        Value* init_val = emit_expr(env, binding->init.get());
        if (!init_val) {
          if (s_codegen_error.empty())
            s_codegen_error = "top-level let init expression failed for '" + binding->name + "'";
          return nullptr;
        }
        Type* llvm_ty;
        if (ty == FfiType::F64) llvm_ty = builder.getDoubleTy();
        else if (ty == FfiType::Ptr) llvm_ty = i8ptr;
        else if (ty != FfiType::Void && !init_val->getType()->isPointerTy()) llvm_ty = builder.getInt64Ty();
        else llvm_ty = init_val->getType();
        if (ty == FfiType::Void || (ty == FfiType::I64 && init_val->getType()->isPointerTy()))
          ty = (llvm_ty == builder.getDoubleTy()) ? FfiType::F64 : (llvm_ty->isPointerTy()) ? FfiType::Ptr : FfiType::I64;
        top_var_types[binding->name] = ty;
        Value* slot = builder.CreateAlloca(llvm_ty, nullptr, binding->name);
        if (ty == FfiType::F64 && init_val->getType() != builder.getDoubleTy())
          init_val = builder.CreateSIToFP(init_val, builder.getDoubleTy());
        else if (ty == FfiType::Ptr && init_val->getType() != i8ptr && init_val->getType()->isPointerTy())
          init_val = builder.CreatePointerCast(init_val, i8ptr);
        else if (ty != FfiType::F64 && ty != FfiType::Ptr && init_val->getType() == builder.getDoubleTy())
          init_val = builder.CreateFPToSI(init_val, builder.getInt64Ty());
        builder.CreateStore(init_val, slot);
        env.vars_scope_stack.back()[binding->name] = slot;
        if (ty == FfiType::Ptr) {
          FnPtrSig sig;
          if (codegen_lookup_fnptr_sig(env, binding->init.get(), &sig))
            env.fnptr_scope_stack.back()[binding->name] = sig;
        }
        FfiType elem_ty = array_element_type_from_expr(binding->init.get(), env);
        if (elem_ty != FfiType::Void)
          env.array_element_scope_stack.back()[binding->name] = elem_ty;
        else if (ty == FfiType::Ptr && binding->init->kind == Expr::Kind::Call)
          env.array_element_scope_stack.back()[binding->name] = FfiType::Ptr;
        else if (binding->init->kind == Expr::Kind::Cast && init_val->getType()->isPointerTy()) {
          const std::string& ct = binding->init->var_name;
          if (ct == "char") env.array_element_scope_stack.back()[binding->name] = FfiType::I8;
          else if (ct == "i32") env.array_element_scope_stack.back()[binding->name] = FfiType::I32;
          else if (ct == "f32") env.array_element_scope_stack.back()[binding->name] = FfiType::F32;
          else if (ct == "f64") env.array_element_scope_stack.back()[binding->name] = FfiType::F64;
          else env.array_element_scope_stack.back()[binding->name] = FfiType::Ptr;
          env.raw_ptr_vars.insert(binding->name);
        }
      } else if (const StmtPtr* stmt = std::get_if<StmtPtr>(&item)) {
        if (!emit_stmt(env, dummy_main, main_fn, stmt->get())) {
          if (s_codegen_error.empty()) {
            const Stmt* s = stmt->get();
            const char* kind_str = "statement";
            switch (s->kind) {
              case Stmt::Kind::For: kind_str = "for"; break;
              case Stmt::Kind::If: kind_str = "if"; break;
              case Stmt::Kind::Let: kind_str = "let"; break;
              case Stmt::Kind::Expr: kind_str = "expr"; break;
              case Stmt::Kind::Assign: kind_str = "assign"; break;
              case Stmt::Kind::Return: kind_str = "return"; break;
            }
            s_codegen_error = "top-level " + std::string(kind_str) + " statement";
            s_codegen_error += " at index " + std::to_string(idx) + " emit failed";
          }
          return nullptr;
        }
      } else {
        const ExprPtr& expr = std::get<ExprPtr>(item);
        Value* v = emit_expr(env, expr.get());
        if (!v && !s_codegen_error.empty())
          return nullptr;
        /* nullptr with no error is valid for void expressions (e.g. backward(loss)) */
      }
    }
    env.var_types = saved_var_types;
    env.fn_var_types = saved_fn_var_types;
  }
  builder.CreateRetVoid();
  hoist_allocas_to_entry(main_fn);
  return module;
}

struct JitThreadArgs {
  void (*entry)();
  std::string* error_out;
};

static void* jit_thread_main(void* p) {
  auto* args = static_cast<JitThreadArgs*>(p);
  try {
    args->entry();
  } catch (const std::exception& e) {
    if (args->error_out) *args->error_out = e.what();
  } catch (...) {
    if (args->error_out) *args->error_out = "unknown exception from JIT entry";
  }
  return nullptr;
}

static const size_t kJitStackBytes = 64ULL * 1024ULL * 1024ULL;

static bool run_with_big_stack(void (*entry)(), size_t stack_bytes, std::string* err) {
  stack_bytes = std::max(stack_bytes, static_cast<size_t>(PTHREAD_STACK_MIN));
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) {
    if (err) *err = "pthread_attr_init failed";
    return false;
  }
  if (pthread_attr_setstacksize(&attr, stack_bytes) != 0) {
    if (err) *err = "pthread_attr_setstacksize failed";
    pthread_attr_destroy(&attr);
    return false;
  }
  pthread_t t;
  JitThreadArgs args{entry, err};
  if (pthread_create(&t, &attr, &jit_thread_main, &args) != 0) {
    if (err) *err = "pthread_create failed";
    pthread_attr_destroy(&attr);
    return false;
  }
  pthread_attr_destroy(&attr);
  void* ret = nullptr;
  if (pthread_join(t, &ret) != 0) {
    if (err) *err = "pthread_join failed";
    return false;
  }
  return true;
}

CodegenResult run_jit(std::unique_ptr<llvm::Module> module,
                      std::unique_ptr<llvm::LLVMContext> ctx) {
  CodegenResult r;
  if (!module) {
    r.error = "no module";
    return r;
  }
  if (getenv("FUSION_DUMP_IR"))
    module->print(llvm::errs(), nullptr);
  if (verifyModule(*module, &llvm::errs())) {
    r.error = "module verification failed";
    return r;
  }
  static bool native_target_init = []() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    return true;
  }();
  (void)native_target_init;
  auto JITOrErr = LLJITBuilder().create();
  if (!JITOrErr) {
    r.error = "failed to create LLJIT: " + llvm::toString(JITOrErr.takeError());
    return r;
  }
  std::unique_ptr<LLJIT> JIT = std::move(*JITOrErr);
  auto genOrErr = DynamicLibrarySearchGenerator::GetForCurrentProcess(
      JIT->getDataLayout().getGlobalPrefix());
  if (!genOrErr) {
    r.error = "failed to create process symbol generator";
    return r;
  }
  JIT->getMainJITDylib().addGenerator(std::move(*genOrErr));

  /* Resolve runtime symbols: RTLD_DEFAULT can miss main-exe symbols on some PIE setups; try main exe explicitly. */
  void* main_handle = nullptr;
  auto add_sym = [&](const char* name) {
    void* addr = dlsym(RTLD_DEFAULT, name);
    if (!addr && !main_handle) {
      main_handle = dlopen(nullptr, RTLD_NOW);
    }
    if (!addr && main_handle) {
      addr = dlsym(main_handle, name);
    }
    if (!addr) return false;
    SymbolMap syms;
    syms[JIT->mangleAndIntern(name)] = ExecutorSymbolDef(
        ExecutorAddr::fromPtr(addr), JITSymbolFlags::Callable | JITSymbolFlags::Exported);
    if (auto err = JIT->getMainJITDylib().define(absoluteSymbols(std::move(syms)))) return false;
    return true;
  };
  const char* first_missing = nullptr;
  auto check_sym = [&](const char* name) {
    if (!add_sym(name)) {
      if (!first_missing) first_missing = name;
      return false;
    }
    return true;
  };
  if (!check_sym("rt_print_cstring") || !check_sym("rt_panic") ||
      !check_sym("rt_read_line") || !check_sym("rt_read_key") || !check_sym("rt_terminal_height") || !check_sym("rt_terminal_width") || !check_sym("rt_flush") || !check_sym("rt_chr") || !check_sym("rt_to_str_i64") || !check_sym("rt_to_str_f64") ||
      !check_sym("rt_from_str_i64") || !check_sym("rt_from_str_f64") || !check_sym("rt_str_concat") || !check_sym("rt_str_dup") ||
      !check_sym("rt_str_upper") || !check_sym("rt_str_lower") || !check_sym("rt_str_contains") ||
      !check_sym("rt_str_strip") || !check_sym("rt_str_find") || !check_sym("rt_str_split") || !check_sym("rt_str_eq") ||
      !check_sym("rt_open") || !check_sym("rt_close") || !check_sym("rt_read_line_file") ||
      !check_sym("rt_write_file_ptr") ||
      !check_sym("rt_write_bytes") || !check_sym("rt_read_bytes") ||
      !check_sym("rt_eof_file") || !check_sym("rt_line_count_file") ||
      !check_sym("rt_http_request") || !check_sym("rt_http_status") ||
      !check_sym("rt_dlopen") || !check_sym("rt_dlsym") || !check_sym("rt_dlerror_last") ||
      !check_sym("rt_ffi_sig_create") || !check_sym("rt_ffi_call") || !check_sym("rt_ffi_error_last")) {
    r.error = "runtime symbols not found (link runtime or use dlsym)";
    if (first_missing) r.error += std::string("; first missing: ") + first_missing;
    const char* err = dlerror();
    if (err) r.error += std::string("; dlerror: ") + err;
    return r;
  }

  ThreadSafeModule tsm(std::move(module), std::move(ctx));
  if (auto err = JIT->addIRModule(std::move(tsm))) {
    r.error = "failed to add IR module";
    return r;
  }
  auto symOrErr = JIT->lookup("fusion_main");
  if (!symOrErr) {
    r.error = "failed to lookup fusion_main";
    return r;
  }
  /* Verify runtime symbols resolve so we don't call null and segfault */
  const char* required[] = {"rt_panic", "rt_dlopen", "rt_dlsym", "rt_ffi_sig_create", "rt_ffi_call"};
  for (const char* name : required) {
    auto reqOrErr = JIT->lookup(name);
    if (!reqOrErr) {
      r.error = std::string("runtime symbol not resolved: ") + name;
      return r;
    }
  }
  using MainFn = void();
  auto entry = symOrErr->template toPtr<MainFn>();
  if (!run_with_big_stack(entry, kJitStackBytes, &r.error)) {
    if (r.error.empty()) r.error = "failed to run JIT entry on big-stack thread";
    return r;
  }
  r.ok = true;
  return r;
}

CodegenResult emit_object(std::unique_ptr<llvm::Module> module,
                           const std::string& output_path) {
  CodegenResult r;
  if (!module) { r.error = "no module"; return r; }
  if (verifyModule(*module, &llvm::errs())) {
    r.error = "module verification failed"; return r;
  }
  static bool init = []() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    return true;
  }();
  (void)init;

  auto triple = llvm::sys::getDefaultTargetTriple();
  module->setTargetTriple(triple);

  std::string err;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!target) { r.error = "no target: " + err; return r; }

  llvm::TargetOptions opt;
  auto* tm = target->createTargetMachine(triple, "generic", "", opt,
                                          std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_));
  module->setDataLayout(tm->createDataLayout());

  // Inject a C main() that calls rt_init → fusion_main → rt_shutdown → return 0
  {
    auto& ctx2 = module->getContext();
    auto* voidTy = llvm::Type::getVoidTy(ctx2);
    auto* i32Ty  = llvm::Type::getInt32Ty(ctx2);
    auto* voidFnTy = llvm::FunctionType::get(voidTy, false);
    auto* mainTy   = llvm::FunctionType::get(i32Ty, false);

    auto* rtInit     = llvm::Function::Create(voidFnTy, llvm::Function::ExternalLinkage, "rt_init", module.get());
    auto* rtShutdown = llvm::Function::Create(voidFnTy, llvm::Function::ExternalLinkage, "rt_shutdown", module.get());
    auto* fusionMain = module->getFunction("fusion_main");

    auto* mainFn = llvm::Function::Create(mainTy, llvm::Function::ExternalLinkage, "main", module.get());
    auto* bb = llvm::BasicBlock::Create(ctx2, "entry", mainFn);
    llvm::IRBuilder<> B(bb);
    B.CreateCall(rtInit);
    if (fusionMain) B.CreateCall(fusionMain);
    B.CreateCall(rtShutdown);
    B.CreateRet(llvm::ConstantInt::get(i32Ty, 0));
  }

  std::error_code ec;
  llvm::raw_fd_ostream dest(output_path, ec, llvm::sys::fs::OF_None);
  if (ec) { r.error = "cannot open output: " + ec.message(); return r; }

  llvm::legacy::PassManager pass;
  if (tm->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    r.error = "cannot emit object file for this target"; return r;
  }
  pass.run(*module);
  dest.flush();
  delete tm;
  r.ok = true;
  return r;
}
#endif

}  // namespace fusion
