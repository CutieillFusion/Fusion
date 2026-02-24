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
  ExprPtr expr;
  ParseError error;
  bool ok() const { return expr != nullptr; }
};

ParseResult parse(const std::vector<Token>& tokens);

}  // namespace fusion

#endif
