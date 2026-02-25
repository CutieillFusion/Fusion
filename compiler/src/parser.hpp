#ifndef FUSION_PARSER_HPP
#define FUSION_PARSER_HPP

#include "ast.hpp"
#include "lexer.hpp"
#include <string>
#include <vector>

namespace fusion {

struct ParseError {
  std::string message;
  size_t line = 0;
  size_t column = 0;
};

struct ParseResult {
  ProgramPtr program;
  ParseError error;
  bool ok() const { return program != nullptr; }
  Expr* root_expr() const { return program ? program->root_expr.get() : nullptr; }
};

ParseResult parse(const std::vector<Token>& tokens);

}  // namespace fusion

#endif
