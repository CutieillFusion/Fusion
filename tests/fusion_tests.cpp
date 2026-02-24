#include "ast.hpp"
#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

#ifdef FUSION_HAVE_LLVM
#include <llvm/IR/LLVMContext.h>
#endif

static int failed = 0;

#define ASSERT(cond, msg) do { \
  if (!(cond)) { std::cerr << "FAIL: " << (msg) << "\n"; failed = 1; } \
  else { std::cout << "PASS: " << (msg) << "\n"; } \
} while(0)

static void test_lexer_print_1_2() {
  auto tokens = fusion::lex("print(1+2)");
  ASSERT(tokens.size() >= 7, "lexer print(1+2) token count");
  if (tokens.size() >= 7) {
    ASSERT(tokens[0].kind == fusion::TokenKind::Ident && tokens[0].ident == "print", "lex ident print");
    ASSERT(tokens[1].kind == fusion::TokenKind::LParen, "lex lparen");
    ASSERT(tokens[2].kind == fusion::TokenKind::IntLiteral && tokens[2].int_value == 1, "lex 1");
    ASSERT(tokens[3].kind == fusion::TokenKind::Plus, "lex plus");
    ASSERT(tokens[4].kind == fusion::TokenKind::IntLiteral && tokens[4].int_value == 2, "lex 2");
    ASSERT(tokens[5].kind == fusion::TokenKind::RParen, "lex rparen");
  }
}

static void test_lexer_spaces() {
  auto tokens = fusion::lex("1 + 2");
  ASSERT(tokens.size() >= 4, "lexer 1 + 2 token count");
  if (tokens.size() >= 4) {
    ASSERT(tokens[0].kind == fusion::TokenKind::IntLiteral && tokens[0].int_value == 1, "lex 1 with space");
    ASSERT(tokens[1].kind == fusion::TokenKind::Plus, "lex +");
    ASSERT(tokens[2].kind == fusion::TokenKind::IntLiteral && tokens[2].int_value == 2, "lex 2");
  }
}

static void test_parser_print_1_2() {
  auto tokens = fusion::lex("print(1+2)");
  auto result = fusion::parse(tokens);
  ASSERT(result.ok(), "parse print(1+2)");
  if (result.ok() && result.expr) {
    ASSERT(result.expr->kind == fusion::Expr::Kind::Call, "parse root is call");
    ASSERT(result.expr->callee == "print" && result.expr->args.size() == 1, "parse print one arg");
    auto* arg = result.expr->args[0].get();
    ASSERT(arg && arg->kind == fusion::Expr::Kind::BinaryOp, "parse arg is binary op");
    ASSERT(arg->bin_op == fusion::BinOp::Add, "parse arg is add");
    ASSERT(arg->left && arg->left->kind == fusion::Expr::Kind::IntLiteral && arg->left->int_value == 1, "parse left 1");
    ASSERT(arg->right && arg->right->kind == fusion::Expr::Kind::IntLiteral && arg->right->int_value == 2, "parse right 2");
  }
}

static void test_parser_invalid() {
  auto tokens = fusion::lex("print(1+");
  auto result = fusion::parse(tokens);
  ASSERT(!result.ok(), "parse print(1+ fails");
}

static void test_sema_ok() {
  auto tokens = fusion::lex("print(1+2)");
  auto parse_result = fusion::parse(tokens);
  ASSERT(parse_result.ok(), "sema ok parse");
  if (parse_result.ok()) {
    auto sema_result = fusion::check(parse_result.expr.get());
    ASSERT(sema_result.ok, "sema print(1+2) valid");
  }
}

static void test_sema_wrong_arity() {
  auto tokens = fusion::lex("print(1,2)");
  auto parse_result = fusion::parse(tokens);
  if (!parse_result.ok()) return;
  auto sema_result = fusion::check(parse_result.expr.get());
  ASSERT(!sema_result.ok, "sema print(1,2) invalid arity");
}

#ifdef FUSION_HAVE_LLVM
static void test_codegen_module() {
  auto tokens = fusion::lex("print(1+2)");
  auto parse_result = fusion::parse(tokens);
  if (!parse_result.ok()) { ASSERT(false, "codegen parse"); return; }
  auto sema_result = fusion::check(parse_result.expr.get());
  if (!sema_result.ok) { ASSERT(false, "codegen sema"); return; }
  llvm::LLVMContext ctx;
  auto module = fusion::codegen(ctx, parse_result.expr.get());
  ASSERT(module != nullptr, "codegen produces module");
  ASSERT(module->getFunction("fusion_main") != nullptr, "codegen has fusion_main");
  ASSERT(module->getFunction("rt_print_i64") != nullptr, "codegen declares rt_print_i64");
}

static void test_jit_creation() {
  auto tokens = fusion::lex("print(1+2)");
  auto parse_result = fusion::parse(tokens);
  if (!parse_result.ok()) return;
  auto sema_result = fusion::check(parse_result.expr.get());
  if (!sema_result.ok) return;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.expr.get());
  if (!module) return;
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  if (jit_result.ok) {
    std::cout << "PASS: JIT run print(1+2)\n";
  } else {
    std::cerr << "FAIL: JIT run: " << jit_result.error << "\n";
    failed = 1;
  }
}
#endif

int main() {
  test_lexer_print_1_2();
  test_lexer_spaces();
  test_parser_print_1_2();
  test_parser_invalid();
  test_sema_ok();
  test_sema_wrong_arity();
#ifdef FUSION_HAVE_LLVM
  test_codegen_module();
  test_jit_creation();
#endif
  if (failed) {
    std::cerr << "Some tests failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All tests passed.\n";
  return EXIT_SUCCESS;
}
