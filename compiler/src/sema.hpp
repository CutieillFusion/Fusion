#ifndef FUSION_SEMA_HPP
#define FUSION_SEMA_HPP

#include "ast.hpp"
#include <string>

namespace fusion {

struct SemaError {
  std::string message;
  size_t line = 0;
  size_t column = 0;
};

struct SemaResult {
  bool ok = false;
  SemaError error;
};

SemaResult check(Program* program);

}  // namespace fusion

#endif
