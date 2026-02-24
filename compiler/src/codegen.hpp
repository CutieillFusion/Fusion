#ifndef FUSION_CODEGEN_HPP
#define FUSION_CODEGEN_HPP

#include "ast.hpp"
#include <memory>
#include <string>

#ifdef FUSION_HAVE_LLVM
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#endif

namespace fusion {

struct CodegenResult {
  bool ok = false;
  std::string error;
};

#ifdef FUSION_HAVE_LLVM
std::unique_ptr<llvm::Module> codegen(llvm::LLVMContext& ctx, Expr* expr);
CodegenResult run_jit(std::unique_ptr<llvm::Module> module,
                      std::unique_ptr<llvm::LLVMContext> ctx);
#endif

}  // namespace fusion

#endif
