#ifndef FUSION_SEMA_HPP
#define FUSION_SEMA_HPP

#include "ast.hpp"
#include <string>
#include <vector>

namespace fusion {

/* Function pointer signature; used by sema and codegen. */
struct FnPtrSig {
  std::vector<FfiType> params;
  FfiType result;
};

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
