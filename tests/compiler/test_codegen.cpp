#include "ast.hpp"
#include "codegen.hpp"
#include "layout.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <gtest/gtest.h>
#include <string>

#ifdef FUSION_HAVE_LLVM
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

static std::string module_ir(llvm::Module* module) {
  std::string ir;
  llvm::raw_string_ostream os(ir);
  module->print(os, nullptr);
  os.flush();
  return ir;
}

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

TEST(CodegenTests, EmitsCompareForEqNeq) {
  auto tokens = fusion::lex("let a = 1; let b = 2; if (a == b) { print(1); } if (a != b) { print(0); }");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  std::string ir = module_ir(module.get());
  EXPECT_TRUE(ir.find("icmp") != std::string::npos)
    << "expected icmp instruction in IR for == and != comparisons";
}

TEST(CodegenTests, EmitsStructFieldGEP) {
  auto tokens = fusion::lex(
      "struct P { x: f64; y: f64; }; "
      "let p = heap(P); store_field(p, P, x, 1.0); print(load_field(p, P, x)); free(as_heap(p))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  std::string ir = module_ir(module.get());
  EXPECT_TRUE(ir.find("getelementptr") != std::string::npos)
    << "expected getelementptr instruction in IR for struct field access";
}

TEST(CodegenTests, EmitsHeapArrayMallocCall) {
  auto tokens = fusion::lex("let a = heap_array(i64, 3); free_array(a)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  EXPECT_NE(module->getFunction("malloc"), nullptr)
    << "expected malloc declaration for heap_array";
}

TEST(CodegenTests, EmitsStackAllocaForStackArray) {
  auto tokens = fusion::lex("let a = stack_array(i64, 4); print(len(a))");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  std::string ir = module_ir(module.get());
  EXPECT_TRUE(ir.find("alloca") != std::string::npos)
    << "expected alloca instruction in IR for stack_array";
}

TEST(CodegenTests, EmitsCastFPToSI) {
  /* f64 -> i64 cast should emit fptosi in IR */
  auto tokens = fusion::lex("let x = 3.9; let y = x as i64; print(y)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  std::string ir = module_ir(module.get());
  EXPECT_TRUE(ir.find("fptosi") != std::string::npos)
    << "expected fptosi instruction in IR for f64 as i64 cast";
}

TEST(CodegenTests, EmitsCastSIToFP) {
  /* i64 -> f64 cast should emit sitofp in IR */
  auto tokens = fusion::lex("let x = 3; let y = x as f64; print(y)");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  std::string ir = module_ir(module.get());
  EXPECT_TRUE(ir.find("sitofp") != std::string::npos)
    << "expected sitofp instruction in IR for i64 as f64 cast";
}

TEST(CodegenTests, EmitsStringLiteralGlobal) {
  auto tokens = fusion::lex("print(\"hello\")");
  auto parse_result = fusion::parse(tokens);
  ASSERT_TRUE(parse_result.ok());
  auto sema_result = fusion::check(parse_result.program.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.program.get());
  ASSERT_NE(module, nullptr);
  std::string ir = module_ir(module.get());
  /* String literals are embedded as global constants with their content */
  EXPECT_TRUE(ir.find("hello") != std::string::npos)
    << "expected string literal content in IR for print(\"hello\")";
}
#endif
