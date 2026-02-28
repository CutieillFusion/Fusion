#include "codegen.hpp"
#include "ast.hpp"
#include "layout.hpp"
#include "sema.hpp"
#include <variant>

#ifdef FUSION_HAVE_LLVM
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/TargetSelect.h"
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <unordered_map>
#include <vector>

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
      if (expr->callee == "get_func_ptr") return FfiType::Ptr;
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
      if (expr->callee == "print") return FfiType::Void;
      if (expr->callee == "range") return FfiType::Ptr;
      if (expr->callee == "read_line" || expr->callee == "read_line_file" || expr->callee == "to_str") return FfiType::Ptr;
      if (expr->callee == "from_str") {
        if (expr->call_type_arg == "i64") return FfiType::I64;
        if (expr->call_type_arg == "f64") return FfiType::F64;
        return FfiType::Void;
      }
      if (expr->callee == "open") return FfiType::Ptr;
      if (expr->callee == "close" || expr->callee == "write_file") return FfiType::Void;
      if (expr->callee == "eof_file" || expr->callee == "line_count_file") return FfiType::I64;
      if (program) {
        for (const ExternFn& ext : program->extern_fns)
          if (ext.name == expr->callee) return ext.return_type;
        for (const FnDef& def : program->user_fns)
          if (def.name == expr->callee) return def.return_type;
      }
      return FfiType::Void;
    case Expr::Kind::Alloc:
    case Expr::Kind::AllocArray:
    case Expr::Kind::AllocBytes:
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
  if (expr->kind == Expr::Kind::Call && expr->callee == "range") {
    if (!expr->call_type_arg.empty()) {
      if (expr->call_type_arg == "i32") return FfiType::I32;
      if (expr->call_type_arg == "i64") return FfiType::I64;
      if (expr->call_type_arg == "f32") return FfiType::F32;
      if (expr->call_type_arg == "f64") return FfiType::F64;
    }
    return FfiType::I64;
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
      if (!alloca_val) return nullptr;
      AllocaInst* alloca = cast<AllocaInst>(alloca_val);
      return B.CreateLoad(alloca->getAllocatedType(), alloca, expr->var_name + ".load");
    }
    case Expr::Kind::IntLiteral:
      return B.getInt64(expr->int_value);
    case Expr::Kind::FloatLiteral:
      return llvm::ConstantFP::get(B.getDoubleTy(), expr->float_value);
    case Expr::Kind::StringLiteral: {
      /* String on stack to avoid GlobalVariable (JIT/relocation issues with globals). */
      std::string s = expr->str_value + '\0';
      Constant* str_const = ConstantDataArray::getString(ctx, s, false);
      Type* str_ty = str_const->getType();
      Value* str_buf = B.CreateAlloca(str_ty, nullptr, "str");
      B.CreateStore(str_const, str_buf);
      return B.CreatePointerCast(str_buf, PointerType::get(Type::getInt8Ty(ctx), 0));
    }
    case Expr::Kind::BinaryOp: {
      Value* L = emit_expr(env, expr->left.get());
      Value* R = emit_expr(env, expr->right.get());
      if (!L || !R) return nullptr;
      FfiType tyL = expr_type(expr->left.get(), prog, env.var_types);
      FfiType tyR = expr_type(expr->right.get(), prog, env.var_types);
      bool is_f64 = (tyL == FfiType::F64 || tyR == FfiType::F64);
      if (is_f64) {
        if (L->getType() != B.getDoubleTy()) L = B.CreateSIToFP(L, B.getDoubleTy());
        if (R->getType() != B.getDoubleTy()) R = B.CreateSIToFP(R, B.getDoubleTy());
      } else {
        if (L->getType() != B.getInt64Ty()) L = B.CreateFPToSI(L, B.getInt64Ty());
        if (R->getType() != B.getInt64Ty()) R = B.CreateFPToSI(R, B.getInt64Ty());
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
        FfiType arg_ty = expr_type(expr->args[0].get(), prog, env.var_types);
        Function* rt_print = nullptr;
        if (arg_ty == FfiType::F64) {
          rt_print = M->getFunction("rt_print_f64");
          if (!rt_print) return nullptr;
          return B.CreateCall(rt_print, {arg_val, stream_val});
        }
        if (arg_ty == FfiType::Ptr) {
          rt_print = M->getFunction("rt_print_cstring");
          if (!rt_print) return nullptr;
          Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
          if (arg_val->getType() != i8ptr) arg_val = B.CreatePointerCast(arg_val, i8ptr);
          return B.CreateCall(rt_print, {arg_val, stream_val});
        }
        rt_print = M->getFunction("rt_print_i64");
        if (!rt_print) return nullptr;
        if (arg_val->getType() != B.getInt64Ty())
          arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
        return B.CreateCall(rt_print, {arg_val, stream_val});
      }
      if (expr->callee == "read_line") {
        Function* fn = M->getFunction("rt_read_line");
        if (!fn) return nullptr;
        return B.CreateCall(fn, {}, "read_line");
      }
      if (expr->callee == "to_str") {
        FfiType t = expr_type(expr->args[0].get(), prog, env.var_types);
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
      if (expr->callee == "write_file") {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        Value* h = emit_expr(env, expr->args[0].get());
        Value* x = emit_expr(env, expr->args[1].get());
        if (!h || !x) return nullptr;
        FfiType val_ty = expr_type(expr->args[1].get(), prog, env.var_types);
        Function* fn = nullptr;
        if (val_ty == FfiType::I64) {
          fn = M->getFunction("rt_write_file_i64");
          if (fn && x->getType() != B.getInt64Ty()) x = B.CreateFPToSI(x, B.getInt64Ty());
        } else if (val_ty == FfiType::F64) {
          fn = M->getFunction("rt_write_file_f64");
          if (fn && x->getType() != B.getDoubleTy()) x = B.CreateSIToFP(x, B.getDoubleTy());
        } else {
          fn = M->getFunction("rt_write_file_ptr");
          if (fn && x->getType() != i8ptr) x = B.CreatePointerCast(x, i8ptr);
        }
        if (!fn) return nullptr;
        return B.CreateCall(fn, {h, x});
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
      if (expr->callee == "range") {
        FfiType elem_ty = FfiType::I64;
        if (!expr->call_type_arg.empty()) {
          if (expr->call_type_arg == "i32") elem_ty = FfiType::I32;
          else if (expr->call_type_arg == "f32") elem_ty = FfiType::F32;
          else if (expr->call_type_arg == "f64") elem_ty = FfiType::F64;
        }
        size_t elem_size = (elem_ty == FfiType::I32 || elem_ty == FfiType::F32) ? 4 : 8;
        Value* start_val = expr->args.size() >= 1 ? emit_expr(env, expr->args[0].get()) : nullptr;
        Value* end_val = expr->args.size() == 2 ? emit_expr(env, expr->args[1].get()) : nullptr;
        if (!start_val) return nullptr;
        if (start_val->getType() != B.getInt64Ty()) start_val = B.CreateIntCast(start_val, B.getInt64Ty(), true);
        Value* n_val = start_val;
        Value* start_i64 = B.getInt64(0);
        if (end_val) {
          if (end_val->getType() != B.getInt64Ty()) end_val = B.CreateIntCast(end_val, B.getInt64Ty(), true);
          n_val = B.CreateSub(end_val, start_val, "range.n");
          start_i64 = start_val;
        }
        Value* total = B.CreateAdd(B.getInt64(8), B.CreateMul(n_val, B.getInt64(elem_size)), "range.total");
        Value* block = B.CreateAlloca(B.getInt8Ty(), total, "range");
        Value* base = B.CreatePointerCast(block, PointerType::get(Type::getInt8Ty(ctx), 0));
        Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
        B.CreateStore(n_val, len_ptr);
        Function* cur_fn = B.GetInsertBlock()->getParent();
        BasicBlock* loop_cond = BasicBlock::Create(ctx, "range.cond", cur_fn);
        BasicBlock* loop_body = BasicBlock::Create(ctx, "range.body", cur_fn);
        BasicBlock* loop_exit = BasicBlock::Create(ctx, "range.exit", cur_fn);
        AllocaInst* i_alloca = B.CreateAlloca(B.getInt64Ty(), nullptr, "range.i");
        B.CreateStore(B.getInt64(0), i_alloca);
        B.CreateBr(loop_cond);
        B.SetInsertPoint(loop_cond);
        Value* i_val = B.CreateLoad(B.getInt64Ty(), i_alloca, "i");
        Value* cond = B.CreateICmpSLT(i_val, n_val, "range.cond");
        B.CreateCondBr(cond, loop_body, loop_exit);
        B.SetInsertPoint(loop_body);
        Value* val_to_store = end_val ? B.CreateAdd(start_i64, i_val, "range.val") : i_val;
        Value* elem_offset = B.CreateAdd(B.getInt64(8), B.CreateMul(i_val, B.getInt64(elem_size)), "range.elem_off");
        Value* elem_ptr = B.CreateGEP(B.getInt8Ty(), base, elem_offset);
        if (elem_ty == FfiType::F64) {
          Value* v = B.CreateSIToFP(val_to_store, B.getDoubleTy());
          elem_ptr = B.CreatePointerCast(elem_ptr, B.getDoubleTy()->getPointerTo());
          B.CreateStore(v, elem_ptr);
        } else if (elem_ty == FfiType::F32) {
          Value* v = B.CreateSIToFP(val_to_store, B.getFloatTy());
          elem_ptr = B.CreatePointerCast(elem_ptr, B.getFloatTy()->getPointerTo());
          B.CreateStore(v, elem_ptr);
        } else if (elem_ty == FfiType::I32) {
          Value* v = B.CreateTrunc(val_to_store, B.getInt32Ty());
          elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt32Ty()->getPointerTo());
          B.CreateStore(v, elem_ptr);
        } else {
          elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt64Ty()->getPointerTo());
          B.CreateStore(val_to_store, elem_ptr);
        }
        B.CreateStore(B.CreateAdd(i_val, B.getInt64(1)), i_alloca);
        B.CreateBr(loop_cond);
        B.SetInsertPoint(loop_exit);
        return base;
      }
      /* User fn call */
      auto uf_it = env.user_fns.find(expr->callee);
      if (uf_it != env.user_fns.end()) {
        Function* fn = uf_it->second;
        std::vector<Value*> args;
        for (size_t j = 0; j < expr->args.size(); ++j) {
          Value* arg_val = emit_expr(env, expr->args[j].get());
          if (!arg_val) return nullptr;
          Type* param_ty = fn->getArg(j)->getType();
          if (arg_val->getType() != param_ty) {
            if (param_ty == B.getDoubleTy() && arg_val->getType() == B.getInt64Ty())
              arg_val = B.CreateSIToFP(arg_val, B.getDoubleTy());
            else if (param_ty == B.getInt64Ty() && arg_val->getType() == B.getDoubleTy())
              arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
            else if (param_ty->isPointerTy() && arg_val->getType()->isPointerTy())
              arg_val = B.CreatePointerCast(arg_val, param_ty);
            else
              return nullptr;
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
    case Expr::Kind::Alloc: {
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      const std::string& tn = expr->var_name;
      if (tn == "i32") {
        Value* slot = B.CreateAlloca(B.getInt32Ty(), nullptr, "alloc.i32");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (tn == "i64") {
        Value* slot = B.CreateAlloca(B.getInt64Ty(), nullptr, "alloc.i64");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (tn == "f32") {
        Value* slot = B.CreateAlloca(B.getFloatTy(), nullptr, "alloc.f32");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (tn == "f64") {
        Value* slot = B.CreateAlloca(B.getDoubleTy(), nullptr, "alloc.f64");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (tn == "ptr") {
        Value* slot = B.CreateAlloca(i8ptr, nullptr, "alloc.ptr");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (prog && env.layout_map) {
        auto it = env.layout_map->find(tn);
        if (it != env.layout_map->end() && it->second.size > 0) {
          /* Heap-allocate structs so pointers remain valid across calls (stack reuse would invalidate them). */
          Function* malloc_fn = M->getFunction("malloc");
          if (!malloc_fn) {
            FunctionType* malloc_ty = FunctionType::get(i8ptr, {B.getInt64Ty()}, false);
            malloc_fn = Function::Create(malloc_ty, GlobalValue::ExternalLinkage, "malloc", M);
          }
          Value* size_val = B.getInt64(static_cast<uint64_t>(it->second.size));
          Value* raw_ptr = B.CreateCall(malloc_fn, size_val, "alloc.struct");
          return B.CreatePointerCast(raw_ptr, i8ptr);
        }
      }
      return nullptr;
    }
    case Expr::Kind::AllocArray: {
      Value* count_val = emit_expr(env, expr->left.get());
      if (!count_val) return nullptr;
      if (count_val->getType() != B.getInt64Ty())
        count_val = B.CreateIntCast(count_val, B.getInt64Ty(), true);
      const std::string& elem_name = expr->var_name;
      size_t elem_size = 8;
      if (elem_name == "i32" || elem_name == "f32") elem_size = 4;
      else if (elem_name == "i64" || elem_name == "f64" || elem_name == "ptr") elem_size = 8;
      else if (prog && env.layout_map) {
        auto it = env.layout_map->find(elem_name);
        if (it != env.layout_map->end()) elem_size = it->second.size;
      }
      Value* total_bytes = B.CreateAdd(B.getInt64(8), B.CreateMul(count_val, B.getInt64(elem_size)), "array.total");
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      /* Heap-allocate arrays so pointers stored in structs (or otherwise escaping) remain valid across calls. */
      Function* malloc_fn = M->getFunction("malloc");
      if (!malloc_fn) {
        FunctionType* malloc_ty = FunctionType::get(i8ptr, {B.getInt64Ty()}, false);
        malloc_fn = Function::Create(malloc_ty, GlobalValue::ExternalLinkage, "malloc", M);
      }
      Value* block = B.CreateCall(malloc_fn, total_bytes, "alloc_array");
      Value* base = B.CreatePointerCast(block, i8ptr);
      Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
      B.CreateStore(count_val, len_ptr);
      return base;
    }
    case Expr::Kind::AllocBytes: {
      Value* size_val = emit_expr(env, expr->left.get());
      if (!size_val) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (size_val->getType() != B.getInt64Ty())
        size_val = B.CreateIntCast(size_val, B.getInt64Ty(), true);
      Value* slot = B.CreateAlloca(B.getInt8Ty(), size_val, "alloc_bytes");
      return B.CreatePointerCast(slot, i8ptr);
    }
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
      if (!env.layout_map) return nullptr;
      auto it = env.layout_map->find(expr->load_field_struct);
      if (it == env.layout_map->end()) return nullptr;
      size_t offset = 0;
      FfiType field_ty = FfiType::Void;
      for (const auto& f : it->second.fields) {
        if (f.first == expr->load_field_field) {
          offset = f.second.offset;
          field_ty = f.second.type;
          break;
        }
      }
      if (field_ty == FfiType::Void) return nullptr;
      Value* base = emit_expr(env, expr->left.get());
      if (!base) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (base->getType() != i8ptr) {
        if (base->getType() == B.getInt64Ty())
          base = B.CreateIntToPtr(base, i8ptr);
        else
          base = B.CreatePointerCast(base, i8ptr);
      }
      Value* field_ptr = B.CreateGEP(B.getInt8Ty(), base, B.getInt64(offset));
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
      auto it = env.layout_map->find(expr->load_field_struct);
      if (it == env.layout_map->end()) return nullptr;
      size_t offset = 0;
      FfiType field_ty = FfiType::Void;
      for (const auto& f : it->second.fields) {
        if (f.first == expr->load_field_field) {
          offset = f.second.offset;
          field_ty = f.second.type;
          break;
        }
      }
      if (field_ty == FfiType::Void) return nullptr;
      Value* base = emit_expr(env, expr->left.get());
      Value* val = emit_expr(env, expr->right.get());
      if (!base || !val) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (base->getType() != i8ptr) {
        if (base->getType() == B.getInt64Ty())
          base = B.CreateIntToPtr(base, i8ptr);
        else
          base = B.CreatePointerCast(base, i8ptr);
      }
      Value* field_ptr = B.CreateGEP(B.getInt8Ty(), base, B.getInt64(offset));
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
    case Expr::Kind::Index: {
      Value* base = emit_expr(env, expr->left.get());
      Value* index_val = emit_expr(env, expr->right.get());
      if (!base || !index_val) return nullptr;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (base->getType() != i8ptr) base = B.CreatePointerCast(base, i8ptr);
      if (index_val->getType() != B.getInt64Ty())
        index_val = B.CreateIntCast(index_val, B.getInt64Ty(), true);
      Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
      Value* len = B.CreateLoad(B.getInt64Ty(), len_ptr, "arr.len");
      FfiType elem_ty = array_element_type_from_expr(expr->left.get(), env);
      if (elem_ty == FfiType::Void) elem_ty = FfiType::I64;
      size_t elem_size = (elem_ty == FfiType::I32 || elem_ty == FfiType::F32) ? 4 : 8;
      Function* rt_panic_fn = M->getFunction("rt_panic");
      if (!rt_panic_fn) return nullptr;
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
      Value* offset = B.CreateAdd(B.getInt64(8), B.CreateMul(index_val, B.getInt64(elem_size)), "elem.offset");
      Value* elem_ptr = B.CreateGEP(B.getInt8Ty(), base, offset);
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
      elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt64Ty()->getPointerTo());
      return B.CreateLoad(B.getInt64Ty(), elem_ptr, "index.load");
    }
    case Expr::Kind::Cast: {
      Value* v = emit_expr(env, expr->left.get());
      if (!v) return nullptr;
      const std::string& to = expr->var_name;
      Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (to == "ptr") {
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
        return nullptr;
      }
      if (to == "i32") {
        if (v->getType() == B.getInt32Ty()) return v;
        if (v->getType() == B.getInt64Ty()) return B.CreateTrunc(v, B.getInt32Ty());
        if (v->getType() == B.getDoubleTy() || v->getType() == B.getFloatTy())
          return B.CreateFPToSI(v, B.getInt32Ty());
        return nullptr;
      }
      return v;
    }
    case Expr::Kind::Compare: {
      Value* L = emit_expr(env, expr->left.get());
      Value* R = emit_expr(env, expr->right.get());
      if (!L || !R) return nullptr;
      FfiType tyL = expr_type(expr->left.get(), prog, env.var_types);
      FfiType tyR = expr_type(expr->right.get(), prog, env.var_types);
      if (tyL == FfiType::Ptr && tyR == FfiType::Ptr) {
        Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
        if (L->getType() != i8ptr) L = B.CreatePointerCast(L, i8ptr);
        if (R->getType() != i8ptr) R = B.CreatePointerCast(R, i8ptr);
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
      if (L->getType() != B.getInt64Ty()) L = B.CreateFPToSI(L, B.getInt64Ty());
      if (R->getType() != B.getInt64Ty()) R = B.CreateFPToSI(R, B.getInt64Ty());
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
  return nullptr;
}

static bool emit_stmt(CodegenEnv& env, FnDef& def, Function* fn, Stmt* stmt);

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
      FfiType let_ty = expr_type(stmt->init.get(), env.program, env.var_types);
      if (env.fn_var_types)
        (*env.fn_var_types)[stmt->name] = let_ty;
      Type* slot_ty = (let_ty != FfiType::Void) ? ffi_type_to_llvm(let_ty, ctx, B) : nullptr;
      AllocaInst* slot;
      if (slot_ty) {
        slot = B.CreateAlloca(slot_ty, nullptr, stmt->name);
        env.vars_scope_stack.back()[stmt->name] = slot;
      } else {
        Value* init_val = emit_expr(env, stmt->init.get());
        if (!init_val) return false;
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
        return true;
      }
      Value* init_val = emit_expr(env, stmt->init.get());
      if (!init_val) return false;
      B.CreateStore(init_val, slot);
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
      env.fnptr_scope_stack.push_back({});
      for (StmtPtr& s : stmt->then_body) {
        if (!emit_stmt(env, def, fn, s.get())) return false;
      }
      env.vars_scope_stack.pop_back();
      env.array_element_scope_stack.pop_back();
      env.fnptr_scope_stack.pop_back();
      if (!B.GetInsertBlock()->getTerminator())
        B.CreateBr(merge_bb);

      B.SetInsertPoint(else_bb);
      if (stmt->else_body.empty()) {
        B.CreateBr(merge_bb);
      } else {
        env.vars_scope_stack.push_back({});
        env.array_element_scope_stack.push_back({});
        env.fnptr_scope_stack.push_back({});
        for (StmtPtr& s : stmt->else_body) {
          if (!emit_stmt(env, def, fn, s.get())) return false;
        }
        env.vars_scope_stack.pop_back();
        env.array_element_scope_stack.pop_back();
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
      if (!val) return false;
      if (stmt->expr->kind == Expr::Kind::VarRef) {
        Value* alloca_val = vars_lookup(env, stmt->expr->var_name);
        if (!alloca_val) return false;
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
          else
            return false;
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
        Value* base = emit_expr(env, base_expr);
        Value* index_val = emit_expr(env, stmt->expr->right.get());
        if (!base || !index_val) return false;
        if (base->getType() != i8ptr) base = B.CreatePointerCast(base, i8ptr);
        if (index_val->getType() != B.getInt64Ty())
          index_val = B.CreateIntCast(index_val, B.getInt64Ty(), true);
        Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
        Value* len = B.CreateLoad(B.getInt64Ty(), len_ptr, "arr.len");
        FfiType elem_ty = array_element_type_from_expr(base_expr, env);
        if (elem_ty == FfiType::Void) elem_ty = FfiType::I64;
        size_t elem_size = (elem_ty == FfiType::I32 || elem_ty == FfiType::F32) ? 4 : 8;
        Function* rt_panic_fn = env.module->getFunction("rt_panic");
        if (!rt_panic_fn) return false;
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
        Value* offset = B.CreateAdd(B.getInt64(8), B.CreateMul(index_val, B.getInt64(elem_size)), "elem.offset");
        Value* elem_ptr = B.CreateGEP(B.getInt8Ty(), base, offset);
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
      return false;
    }
    case Stmt::Kind::For: {
      if (!stmt->iterable) return false;
      Value* base = emit_expr(env, stmt->iterable.get());
      if (!base) return false;
      Type* i8ptr_ty = PointerType::get(Type::getInt8Ty(ctx), 0);
      if (base->getType() != i8ptr_ty) base = B.CreatePointerCast(base, i8ptr_ty);
      Value* len_ptr = B.CreatePointerCast(base, B.getInt64Ty()->getPointerTo());
      Value* len = B.CreateLoad(B.getInt64Ty(), len_ptr, "for.len");
      FfiType elem_ty = array_element_type_from_expr(stmt->iterable.get(), env);
      if (elem_ty == FfiType::Void) elem_ty = FfiType::I64;
      size_t elem_size = (elem_ty == FfiType::I32 || elem_ty == FfiType::F32) ? 4 : 8;
      Type* elem_llvm_ty = ffi_type_to_llvm(elem_ty, ctx, B);
      AllocaInst* index_alloca = B.CreateAlloca(B.getInt64Ty(), nullptr, "for.idx");
      AllocaInst* loop_var_alloca = B.CreateAlloca(elem_llvm_ty, nullptr, stmt->name);
      B.CreateStore(B.getInt64(0), index_alloca);
      BasicBlock* cond_bb = BasicBlock::Create(ctx, "for.cond", fn);
      BasicBlock* body_bb = BasicBlock::Create(ctx, "for.body", fn);
      BasicBlock* exit_bb = BasicBlock::Create(ctx, "for.exit", fn);
      B.CreateBr(cond_bb);
      B.SetInsertPoint(cond_bb);
      Value* idx = B.CreateLoad(B.getInt64Ty(), index_alloca, "idx");
      Value* cond = B.CreateICmpSLT(idx, len, "for.cond");
      B.CreateCondBr(cond, body_bb, exit_bb);
      B.SetInsertPoint(body_bb);
      Value* elem_offset = B.CreateAdd(B.getInt64(8), B.CreateMul(idx, B.getInt64(elem_size)), "for.off");
      Value* elem_ptr = B.CreateGEP(B.getInt8Ty(), base, elem_offset);
      if (elem_ty == FfiType::F64) {
        elem_ptr = B.CreatePointerCast(elem_ptr, B.getDoubleTy()->getPointerTo());
        Value* loaded = B.CreateLoad(B.getDoubleTy(), elem_ptr);
        B.CreateStore(loaded, loop_var_alloca);
      } else if (elem_ty == FfiType::Ptr) {
        elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt64Ty()->getPointerTo());
        Value* loaded = B.CreateLoad(B.getInt64Ty(), elem_ptr);
        B.CreateStore(B.CreateIntToPtr(loaded, i8ptr_ty), loop_var_alloca);
      } else if (elem_ty == FfiType::I32) {
        elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt32Ty()->getPointerTo());
        Value* loaded = B.CreateLoad(B.getInt32Ty(), elem_ptr);
        B.CreateStore(loaded, loop_var_alloca);
      } else {
        elem_ptr = B.CreatePointerCast(elem_ptr, B.getInt64Ty()->getPointerTo());
        Value* loaded = B.CreateLoad(B.getInt64Ty(), elem_ptr);
        B.CreateStore(loaded, loop_var_alloca);
      }
      env.vars_scope_stack.push_back({});
      env.array_element_scope_stack.push_back({});
      env.fnptr_scope_stack.push_back({});
      env.vars_scope_stack.back()[stmt->name] = loop_var_alloca;
      env.array_element_scope_stack.back()[stmt->name] = elem_ty;
      for (StmtPtr& s : stmt->body) {
        if (!emit_stmt(env, def, fn, s.get())) return false;
      }
      env.vars_scope_stack.pop_back();
      env.array_element_scope_stack.pop_back();
      env.fnptr_scope_stack.pop_back();
      B.CreateStore(B.CreateAdd(idx, B.getInt64(1)), index_alloca);
      B.CreateBr(cond_bb);
      B.SetInsertPoint(exit_bb);
      return true;
    }
  }
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
  std::vector<std::unordered_map<std::string, FnPtrSig>> saved_fnptr_stack = std::move(env.fnptr_scope_stack);
  env.vars_scope_stack.push_back({});
  env.array_element_scope_stack.push_back({});
  env.fnptr_scope_stack.push_back({});
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
  env.fnptr_scope_stack = std::move(saved_fnptr_stack);
  return true;
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
  FunctionType* print_i64_ty = FunctionType::get(builder.getVoidTy(), {builder.getInt64Ty(), builder.getInt64Ty()}, false);
  FunctionType* print_f64_ty = FunctionType::get(builder.getVoidTy(), {builder.getDoubleTy(), builder.getInt64Ty()}, false);
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

  Function::Create(print_i64_ty, GlobalValue::ExternalLinkage, "rt_print_i64", module.get());
  Function::Create(print_f64_ty, GlobalValue::ExternalLinkage, "rt_print_f64", module.get());
  Function::Create(print_cstring_ty, GlobalValue::ExternalLinkage, "rt_print_cstring", module.get());
  Function::Create(FunctionType::get(i8ptr, false), GlobalValue::ExternalLinkage, "rt_read_line", module.get());
  Function::Create(FunctionType::get(i8ptr, builder.getInt64Ty(), false), GlobalValue::ExternalLinkage, "rt_to_str_i64", module.get());
  Function::Create(FunctionType::get(i8ptr, builder.getDoubleTy(), false), GlobalValue::ExternalLinkage, "rt_to_str_f64", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_from_str_i64", module.get());
  Function::Create(FunctionType::get(builder.getDoubleTy(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_from_str_f64", module.get());
  Function::Create(FunctionType::get(i8ptr, {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_open", module.get());
  Function::Create(FunctionType::get(builder.getVoidTy(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_close", module.get());
  Function::Create(FunctionType::get(i8ptr, i8ptr, false), GlobalValue::ExternalLinkage, "rt_read_line_file", module.get());
  Function::Create(FunctionType::get(builder.getVoidTy(), {i8ptr, builder.getInt64Ty()}, false), GlobalValue::ExternalLinkage, "rt_write_file_i64", module.get());
  Function::Create(FunctionType::get(builder.getVoidTy(), {i8ptr, builder.getDoubleTy()}, false), GlobalValue::ExternalLinkage, "rt_write_file_f64", module.get());
  Function::Create(FunctionType::get(builder.getVoidTy(), {i8ptr, i8ptr}, false), GlobalValue::ExternalLinkage, "rt_write_file_ptr", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_eof_file", module.get());
  Function::Create(FunctionType::get(builder.getInt64Ty(), i8ptr, false), GlobalValue::ExternalLinkage, "rt_line_count_file", module.get());
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
    env.fnptr_scope_stack.push_back({});
    FnDef dummy_main;
    dummy_main.return_type = FfiType::Void;
    for (const TopLevelItem& item : program->top_level) {
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
        else if (ty != FfiType::Void) llvm_ty = builder.getInt64Ty();
        else llvm_ty = init_val->getType();
        if (ty == FfiType::Void) ty = (llvm_ty == builder.getDoubleTy()) ? FfiType::F64 : (llvm_ty == i8ptr) ? FfiType::Ptr : FfiType::I64;
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
      } else if (const StmtPtr* stmt = std::get_if<StmtPtr>(&item)) {
        if (!emit_stmt(env, dummy_main, main_fn, stmt->get())) {
          if (s_codegen_error.empty())
            s_codegen_error = "top-level if/statement emit failed";
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
  return module;
}

CodegenResult run_jit(std::unique_ptr<llvm::Module> module,
                      std::unique_ptr<llvm::LLVMContext> ctx) {
  CodegenResult r;
  if (!module) {
    r.error = "no module";
    return r;
  }
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

  auto add_sym = [&](const char* name) {
    void* addr = dlsym(RTLD_DEFAULT, name);
    if (!addr) return false;
    SymbolMap syms;
    syms[JIT->mangleAndIntern(name)] = ExecutorSymbolDef(
        ExecutorAddr::fromPtr(addr), JITSymbolFlags::Callable | JITSymbolFlags::Exported);
    if (auto err = JIT->getMainJITDylib().define(absoluteSymbols(std::move(syms)))) return false;
    return true;
  };
  if (!add_sym("rt_print_i64") || !add_sym("rt_print_f64") || !add_sym("rt_print_cstring") || !add_sym("rt_panic") ||
      !add_sym("rt_read_line") || !add_sym("rt_to_str_i64") || !add_sym("rt_to_str_f64") ||
      !add_sym("rt_from_str_i64") || !add_sym("rt_from_str_f64") ||
      !add_sym("rt_open") || !add_sym("rt_close") || !add_sym("rt_read_line_file") ||
      !add_sym("rt_write_file_i64") || !add_sym("rt_write_file_f64") || !add_sym("rt_write_file_ptr") ||
      !add_sym("rt_eof_file") || !add_sym("rt_line_count_file") ||
      !add_sym("rt_dlopen") || !add_sym("rt_dlsym") || !add_sym("rt_dlerror_last") ||
      !add_sym("rt_ffi_sig_create") || !add_sym("rt_ffi_call") || !add_sym("rt_ffi_error_last")) {
    r.error = "runtime symbols not found (link runtime or use dlsym)";
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
  entry();
  r.ok = true;
  return r;
}
#endif

}  // namespace fusion
