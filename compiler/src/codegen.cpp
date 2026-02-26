#include "codegen.hpp"
#include "ast.hpp"
#include "layout.hpp"
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
      if (expr->callee == "print") return FfiType::Void;
      if (program) {
        for (const ExternFn& ext : program->extern_fns)
          if (ext.name == expr->callee) return ext.return_type;
        for (const FnDef& def : program->user_fns)
          if (def.name == expr->callee) return def.return_type;
      }
      return FfiType::Void;
    case Expr::Kind::Alloc:
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
      if (expr->var_name == "cstring") return FfiType::Ptr;
      return FfiType::Void;
    case Expr::Kind::Compare:
      return FfiType::I64;  /* condition produces i1 in IR */
  }
  return FfiType::Void;
}

struct CodegenEnv {
  Program* program = nullptr;
  Module* module = nullptr;
  IRBuilder<>* builder = nullptr;
  LayoutMap* layout_map = nullptr;
  std::unordered_map<std::string, Value*> lib_handles;  // lib name or "" -> global
  std::unordered_map<std::string, Value*> vars;         // variable name -> alloca
  std::unordered_map<std::string, Function*> user_fns;   // user fn name -> LLVM Function
  const std::unordered_map<std::string, FfiType>* var_types = nullptr;  // current fn params + locals (for expr_type)
  std::unordered_map<std::string, FfiType>* fn_var_types = nullptr;     // mutable map to add locals in Let
};

static Value* emit_expr(CodegenEnv& env, Expr* expr) {
  if (!expr) return nullptr;
  LLVMContext& ctx = env.builder->getContext();
  IRBuilder<>& B = *env.builder;
  Module* M = env.module;
  Program* prog = env.program;

  switch (expr->kind) {
    case Expr::Kind::VarRef: {
      auto it = env.vars.find(expr->var_name);
      if (it == env.vars.end()) return nullptr;
      AllocaInst* alloca = cast<AllocaInst>(it->second);
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
      if (expr->bin_op != BinOp::Add) return nullptr;
      Value* L = emit_expr(env, expr->left.get());
      Value* R = emit_expr(env, expr->right.get());
      if (!L || !R) return nullptr;
      FfiType tyL = expr_type(expr->left.get(), prog, env.var_types);
      FfiType tyR = expr_type(expr->right.get(), prog, env.var_types);
      if (tyL == FfiType::F64 || tyR == FfiType::F64) {
        if (L->getType() != B.getDoubleTy()) L = B.CreateSIToFP(L, B.getDoubleTy());
        if (R->getType() != B.getDoubleTy()) R = B.CreateSIToFP(R, B.getDoubleTy());
        return B.CreateFAdd(L, R, "add");
      }
      if (L->getType() != B.getInt64Ty()) L = B.CreateFPToSI(L, B.getInt64Ty());
      if (R->getType() != B.getInt64Ty()) R = B.CreateFPToSI(R, B.getInt64Ty());
      return B.CreateAdd(L, R, "add");
    }
    case Expr::Kind::Call: {
      if (expr->callee == "print") {
        if (expr->args.size() != 1) return nullptr;
        Value* arg_val = emit_expr(env, expr->args[0].get());
        if (!arg_val) return nullptr;
        FfiType arg_ty = expr_type(expr->args[0].get(), prog, env.var_types);
        Function* rt_print = nullptr;
        if (arg_ty == FfiType::F64) {
          rt_print = M->getFunction("rt_print_f64");
          if (!rt_print) return nullptr;
          return B.CreateCall(rt_print, arg_val);
        }
        if (arg_ty == FfiType::Ptr) {
          rt_print = M->getFunction("rt_print_cstring");
          if (!rt_print) return nullptr;
          Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
          if (arg_val->getType() != i8ptr) arg_val = B.CreatePointerCast(arg_val, i8ptr);
          return B.CreateCall(rt_print, arg_val);
        }
        rt_print = M->getFunction("rt_print_i64");
        if (!rt_print) return nullptr;
        if (arg_val->getType() != B.getInt64Ty())
          arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
        return B.CreateCall(rt_print, arg_val);
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
        Value* ret = B.CreateCall(fn, args, "call." + expr->callee);
        if (fn->getReturnType()->isVoidTy()) return nullptr;
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
      if (tn == "ptr" || tn == "cstring") {
        Value* slot = B.CreateAlloca(i8ptr, nullptr, "alloc.ptr");
        return B.CreatePointerCast(slot, i8ptr);
      }
      if (prog && env.layout_map) {
        auto it = env.layout_map->find(tn);
        if (it != env.layout_map->end() && it->second.size > 0) {
          Value* slot = B.CreateAlloca(B.getInt8Ty(), B.getInt32(it->second.size), "alloc.struct");
          return B.CreatePointerCast(slot, i8ptr);
        }
      }
      return nullptr;
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
      auto it = env.vars.find(expr->left->var_name);
      if (it == env.vars.end()) return nullptr;
      Value* alloca = it->second;
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
      if (base->getType() != i8ptr) base = B.CreatePointerCast(base, i8ptr);
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
      if (base->getType() != i8ptr) base = B.CreatePointerCast(base, i8ptr);
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
    case Expr::Kind::Cast:
      return emit_expr(env, expr->left.get());
    case Expr::Kind::Compare: {
      Value* L = emit_expr(env, expr->left.get());
      Value* R = emit_expr(env, expr->right.get());
      if (!L || !R) return nullptr;
      FfiType tyL = expr_type(expr->left.get(), prog, env.var_types);
      FfiType tyR = expr_type(expr->right.get(), prog, env.var_types);
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
      if (env.fn_var_types)
        (*env.fn_var_types)[stmt->name] = expr_type(stmt->init.get(), env.program, env.var_types);
      Value* init_val = emit_expr(env, stmt->init.get());
      if (!init_val) return false;
      AllocaInst* slot = B.CreateAlloca(init_val->getType(), nullptr, stmt->name);
      B.CreateStore(init_val, slot);
      env.vars[stmt->name] = slot;
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
      for (StmtPtr& s : stmt->then_body) {
        if (!emit_stmt(env, def, fn, s.get())) return false;
      }
      if (!B.GetInsertBlock()->getTerminator())
        B.CreateBr(merge_bb);

      B.SetInsertPoint(else_bb);
      if (stmt->else_body.empty()) {
        B.CreateBr(merge_bb);
      } else {
        for (StmtPtr& s : stmt->else_body) {
          if (!emit_stmt(env, def, fn, s.get())) return false;
        }
        if (!B.GetInsertBlock()->getTerminator())
          B.CreateBr(merge_bb);
      }
      B.SetInsertPoint(merge_bb);
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
  std::unordered_map<std::string, Value*> saved_vars = std::move(env.vars);
  env.vars.clear();
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
    env.vars[def.params[j].first] = alloca;
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
  env.vars = std::move(saved_vars);
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
  FunctionType* print_i64_ty = FunctionType::get(builder.getVoidTy(), builder.getInt64Ty(), false);
  FunctionType* print_f64_ty = FunctionType::get(builder.getVoidTy(), builder.getDoubleTy(), false);
  FunctionType* print_cstring_ty = FunctionType::get(builder.getVoidTy(), i8ptr, false);
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
    FnDef dummy_main;
    dummy_main.return_type = FfiType::Void;
    for (const TopLevelItem& item : program->top_level) {
      if (const LetBinding* binding = std::get_if<LetBinding>(&item)) {
        FfiType ty = binding_type(*binding, program);
        top_var_types[binding->name] = ty;
        Type* llvm_ty;
        if (ty == FfiType::F64) llvm_ty = builder.getDoubleTy();
        else if (ty == FfiType::Ptr) llvm_ty = i8ptr;
        else llvm_ty = builder.getInt64Ty();
        Value* slot = builder.CreateAlloca(llvm_ty, nullptr, binding->name);
        Value* init_val = emit_expr(env, binding->init.get());
        if (!init_val) {
          if (s_codegen_error.empty())
            s_codegen_error = "top-level let init expression failed for '" + binding->name + "'";
          return nullptr;
        }
        if (ty == FfiType::F64 && init_val->getType() != builder.getDoubleTy())
          init_val = builder.CreateSIToFP(init_val, builder.getDoubleTy());
        else if (ty == FfiType::Ptr && init_val->getType() != i8ptr && init_val->getType()->isPointerTy())
          init_val = builder.CreatePointerCast(init_val, i8ptr);
        else if (ty != FfiType::F64 && ty != FfiType::Ptr && init_val->getType() == builder.getDoubleTy())
          init_val = builder.CreateFPToSI(init_val, builder.getInt64Ty());
        builder.CreateStore(init_val, slot);
        env.vars[binding->name] = slot;
      } else if (const StmtPtr* stmt = std::get_if<StmtPtr>(&item)) {
        if (!emit_stmt(env, dummy_main, main_fn, stmt->get())) {
          if (s_codegen_error.empty())
            s_codegen_error = "top-level if/statement emit failed";
          return nullptr;
        }
      } else {
        const ExprPtr& expr = std::get<ExprPtr>(item);
        Value* v = emit_expr(env, expr.get());
        if (!v) {
          if (s_codegen_error.empty())
            s_codegen_error = "top-level expression emit failed";
          return nullptr;
        }
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
