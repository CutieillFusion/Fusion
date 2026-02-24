#include "codegen.hpp"

#ifdef FUSION_HAVE_LLVM
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Verifier.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/TargetSelect.h"
#include <dlfcn.h>

using namespace llvm;
using namespace llvm::orc;
#endif

namespace fusion {

#ifdef FUSION_HAVE_LLVM
static llvm::Value* emit_expr(IRBuilder<>& builder, Expr* expr) {
  if (!expr) return nullptr;
  switch (expr->kind) {
    case Expr::Kind::IntLiteral:
      return builder.getInt64(expr->int_value);
    case Expr::Kind::BinaryOp: {
      if (expr->bin_op != BinOp::Add) return nullptr;
      llvm::Value* L = emit_expr(builder, expr->left.get());
      llvm::Value* R = emit_expr(builder, expr->right.get());
      if (!L || !R) return nullptr;
      return builder.CreateAdd(L, R, "add");
    }
    case Expr::Kind::Call: {
      if (expr->callee != "print" || expr->args.size() != 1) return nullptr;
      llvm::Value* arg_val = emit_expr(builder, expr->args[0].get());
      if (!arg_val) return nullptr;
      Module* M = builder.GetInsertBlock()->getModule();
      Function* rt_print = M->getFunction("rt_print_i64");
      if (!rt_print) return nullptr;
      return builder.CreateCall(rt_print, arg_val);
    }
  }
  return nullptr;
}

std::unique_ptr<llvm::Module> codegen(llvm::LLVMContext& ctx, Expr* expr) {
  auto module = std::make_unique<Module>("fusion", ctx);
  IRBuilder<> builder(ctx);

  FunctionType* main_ty = FunctionType::get(builder.getVoidTy(), false);
  Function* main_fn = Function::Create(main_ty, GlobalValue::ExternalLinkage, "fusion_main", module.get());
  BasicBlock* entry = BasicBlock::Create(ctx, "entry", main_fn);
  builder.SetInsertPoint(entry);

  FunctionType* print_ty = FunctionType::get(builder.getVoidTy(), builder.getInt64Ty(), false);
  Function::Create(print_ty, GlobalValue::ExternalLinkage, "rt_print_i64", module.get());

  llvm::Value* result = emit_expr(builder, expr);
  if (!result) return nullptr;
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
  void *rt_print_addr = dlsym(RTLD_DEFAULT, "rt_print_i64");
  if (!rt_print_addr) {
    r.error = "rt_print_i64 not found (link runtime or use dlsym)";
    return r;
  }
  SymbolMap runtime_syms;
  runtime_syms[JIT->mangleAndIntern("rt_print_i64")] = ExecutorSymbolDef(
      ExecutorAddr::fromPtr(rt_print_addr),
      JITSymbolFlags::Callable | JITSymbolFlags::Exported);
  if (auto err = JIT->getMainJITDylib().define(absoluteSymbols(std::move(runtime_syms)))) {
    r.error = "failed to define runtime symbols";
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
  using MainFn = void();
  auto entry = symOrErr->template toPtr<MainFn>();
  entry();
  r.ok = true;
  return r;
}
#endif

}  // namespace fusion
