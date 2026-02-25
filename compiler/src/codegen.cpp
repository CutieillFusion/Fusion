#include "codegen.hpp"
#include "ast.hpp"

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
#include <unordered_map>

using namespace llvm;
using namespace llvm::orc;
#endif

namespace fusion {

#ifdef FUSION_HAVE_LLVM
/* Match rt_ffi_type_kind_t enum in runtime.h */
static int ffi_type_to_kind(FfiType t) {
  switch (t) {
    case FfiType::Void: return 0;
    case FfiType::I32: return 1;
    case FfiType::I64: return 2;
    case FfiType::F32: return 3;
    case FfiType::F64: return 4;
    case FfiType::Ptr: return 5;
    case FfiType::Cstring: return 6;
  }
  return 0;
}

static FfiType expr_type(Expr* expr, Program* program);

static FfiType binding_type(const LetBinding& binding, Program* program) {
  return expr_type(binding.init.get(), program);
}

static FfiType expr_type(Expr* expr, Program* program) {
  if (!expr) return FfiType::Void;
  switch (expr->kind) {
    case Expr::Kind::IntLiteral: return FfiType::I64;
    case Expr::Kind::FloatLiteral: return FfiType::F64;
    case Expr::Kind::StringLiteral: return FfiType::Cstring;
    case Expr::Kind::BinaryOp: return FfiType::I64;
    case Expr::Kind::VarRef: {
      if (!program) return FfiType::Void;
      for (const LetBinding& b : program->bindings)
        if (b.name == expr->var_name) return binding_type(b, program);
      return FfiType::Void;
    }
    case Expr::Kind::Call:
      if (expr->callee == "print") return FfiType::Void;
      if (program) {
        for (const ExternFn& ext : program->extern_fns)
          if (ext.name == expr->callee) return ext.return_type;
      }
      return FfiType::Void;
  }
  return FfiType::Void;
}

struct CodegenEnv {
  Program* program = nullptr;
  Module* module = nullptr;
  IRBuilder<>* builder = nullptr;
  std::unordered_map<std::string, Value*> lib_handles;  // lib name or "" -> global
  std::unordered_map<std::string, Value*> vars;         // variable name -> alloca
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
      FfiType tyL = expr_type(expr->left.get(), prog);
      FfiType tyR = expr_type(expr->right.get(), prog);
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
        FfiType arg_ty = expr_type(expr->args[0].get(), prog);
        Function* rt_print = nullptr;
        if (arg_ty == FfiType::F64) {
          rt_print = M->getFunction("rt_print_f64");
          if (!rt_print) return nullptr;
          return B.CreateCall(rt_print, arg_val);
        }
        if (arg_ty == FfiType::Cstring) {
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
      /* Extern fn call */
      const ExternFn* ext = nullptr;
      if (prog) {
        for (const ExternFn& e : prog->extern_fns)
          if (e.name == expr->callee) { ext = &e; break; }
      }
      if (!ext || prog->libs.empty()) return nullptr;

      Value* handle = env.lib_handles.count("") ? env.lib_handles[""] : env.lib_handles[ext->lib_name];
      if (!handle) handle = env.lib_handles[""];
      if (!handle) return nullptr;
      handle = B.CreateLoad(PointerType::get(Type::getInt8Ty(ctx), 0), handle);

      Function* rt_dlsym_fn = M->getFunction("rt_dlsym");
      Function* rt_panic_fn = M->getFunction("rt_panic");
      Function* rt_dlerror_fn = M->getFunction("rt_dlerror_last");
      Function* rt_ffi_sig_create_fn = M->getFunction("rt_ffi_sig_create");
      Function* rt_ffi_call_fn = M->getFunction("rt_ffi_call");
      Function* rt_ffi_error_fn = M->getFunction("rt_ffi_error_last");
      if (!rt_dlsym_fn || !rt_panic_fn || !rt_ffi_sig_create_fn || !rt_ffi_call_fn) return nullptr;

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
      for (size_t j = 0; j < nargs; ++j) {
        Value* arg_val = emit_expr(env, expr->args[j].get());
        if (!arg_val) return nullptr;
        Value* slot = B.CreateGEP(B.getInt8Ty(), args_buf, B.getInt32(j * slot_size));
        Type* slot_ty = (ext->params[j].second == FfiType::F64) ? B.getDoubleTy() : B.getInt64Ty();
        slot = B.CreatePointerCast(slot, slot_ty->getPointerTo());
        if (ext->params[j].second == FfiType::F64 && arg_val->getType() != B.getDoubleTy())
          arg_val = B.CreateSIToFP(arg_val, B.getDoubleTy());
        else if (ext->params[j].second != FfiType::F64 && arg_val->getType() == B.getDoubleTy())
          arg_val = B.CreateFPToSI(arg_val, B.getInt64Ty());
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
      } else {
        Value* ret_ptr = B.CreatePointerCast(ret_buf, B.getInt64Ty()->getPointerTo());
        ret_val = B.CreateLoad(B.getInt64Ty(), ret_ptr);
      }
      return ret_val;
    }
  }
  return nullptr;
}

std::unique_ptr<llvm::Module> codegen(llvm::LLVMContext& ctx, Program* program) {
  auto module = std::make_unique<Module>("fusion", ctx);
  IRBuilder<> builder(ctx);
  Type* i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
  CodegenEnv env;
  env.program = program;
  env.module = module.get();
  env.builder = &builder;

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

  FunctionType* main_ty = FunctionType::get(builder.getVoidTy(), false);
  Function* main_fn = Function::Create(main_ty, GlobalValue::ExternalLinkage, "fusion_main", module.get());
  BasicBlock* entry = BasicBlock::Create(ctx, "entry", main_fn);
  builder.SetInsertPoint(entry);

  /* Create one alloca per lib for the dlopen handle (entry block so it dominates all uses) */
  for (size_t idx = 0; idx < (program ? program->libs.size() : 0); ++idx) {
    const ExternLib& lib = program->libs[idx];
    Value* handle_slot = builder.CreateAlloca(i8ptr, nullptr, "lib_handle_" + std::to_string(idx));
    env.lib_handles[lib.name] = handle_slot;
    if (lib.name.empty()) env.lib_handles[""] = handle_slot;
  }

  /* Emit dlopen + null check for each lib */
  for (size_t idx = 0; idx < (program ? program->libs.size() : 0); ++idx) {
    const ExternLib& lib = program->libs[idx];
    Value* handle_slot = env.lib_handles.count(lib.name) ? env.lib_handles[lib.name] : env.lib_handles[""];

    /* Path string on stack to avoid GlobalVariable (LLVM 18 CreateGlobalStringPtr can crash) */
    Type* path_array_ty = ArrayType::get(Type::getInt8Ty(ctx), lib.path.size() + 1);
    Value* path_buf = builder.CreateAlloca(path_array_ty, nullptr, "lib_path");
    Constant* path_const = ConstantDataArray::getString(ctx, lib.path, true);
    builder.CreateStore(path_const, path_buf);
    Value* path_ptr = builder.CreatePointerCast(path_buf, i8ptr);
    Function* rt_dlopen = module->getFunction("rt_dlopen");
    Function* rt_panic = module->getFunction("rt_panic");
    Function* rt_dlerror = module->getFunction("rt_dlerror_last");
    if (!rt_dlopen || !rt_panic || !rt_dlerror)
      return nullptr;
    Value* h = builder.CreateCall(rt_dlopen, path_ptr);
    builder.CreateStore(h, handle_slot);
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

  /* Emit let-bindings: alloca, store init value, register in env.vars */
  if (program) {
    for (const LetBinding& binding : program->bindings) {
    FfiType ty = binding_type(binding, program);
    Type* llvm_ty;
    if (ty == FfiType::F64) llvm_ty = builder.getDoubleTy();
    else if (ty == FfiType::Cstring) llvm_ty = i8ptr;
    else llvm_ty = builder.getInt64Ty();
    Value* slot = builder.CreateAlloca(llvm_ty, nullptr, binding.name);
    Value* init_val = emit_expr(env, binding.init.get());
    if (!init_val) return nullptr;
    if (ty == FfiType::F64 && init_val->getType() != builder.getDoubleTy())
      init_val = builder.CreateSIToFP(init_val, builder.getDoubleTy());
    else if (ty == FfiType::Cstring && init_val->getType() != i8ptr)
      init_val = builder.CreatePointerCast(init_val, i8ptr);
    else if (ty != FfiType::F64 && ty != FfiType::Cstring && init_val->getType() == builder.getDoubleTy())
      init_val = builder.CreateFPToSI(init_val, builder.getInt64Ty());
    builder.CreateStore(init_val, slot);
    env.vars[binding.name] = slot;
    }
  }

  if (program) {
    for (const auto& stmt : program->stmts) {
      Value* v = emit_expr(env, stmt.get());
      if (!v) return nullptr;
    }
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
