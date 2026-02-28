#include "ast.hpp"
#include "codegen.hpp"
#include "layout.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <gtest/gtest.h>

#ifdef FUSION_HAVE_LLVM
#include <llvm/IR/LLVMContext.h>

TEST(CodegenTests, EmitsModuleWithFusionMain) {
  auto tokens = fusion::lex("print(1+2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  EXPECT_NE(module->getFunction("fusion_main"), nullptr);
  EXPECT_NE(module->getFunction("rt_print_i64"), nullptr);
}

TEST(CodegenTests, EmitsFreeAndFreeArray) {
  auto tokens = fusion::lex("let p = heap(i64); free(p); let a = heap_array(i64, 3); free_array(a)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok);
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  EXPECT_NE(module->getFunction("free"), nullptr);
}
#endif
