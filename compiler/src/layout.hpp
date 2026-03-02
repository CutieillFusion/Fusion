#ifndef FUSION_LAYOUT_HPP
#define FUSION_LAYOUT_HPP

#include "ast.hpp"
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace fusion {

/* C-layout: size and alignment for a single FfiType (primitive). */
inline size_t ffi_type_size(FfiType t) {
  switch (t) {
    case FfiType::I8: return 1;
    case FfiType::I32: return 4;
    case FfiType::I64: return 8;
    case FfiType::F32: return 4;
    case FfiType::F64: return 8;
    case FfiType::Ptr: return 8;
    case FfiType::Void: return 0;
  }
  return 0;
}

inline size_t ffi_type_align(FfiType t) {
  return ffi_type_size(t);
}

struct FieldLayout {
  size_t offset = 0;
  FfiType type = FfiType::Void;
  std::string struct_name;  // non-empty for embedded struct fields
};

struct StructLayout {
  size_t size = 0;
  size_t alignment = 0;
  std::vector<std::pair<std::string, FieldLayout>> fields;
};

/* Map struct name -> layout. Build from Program::struct_defs. */
using LayoutMap = std::unordered_map<std::string, StructLayout>;

/* Compute C layout for a struct. Returns empty layout if def has no fields or invalid. */
/* known_layouts is used for embedded struct field sizes (pass {} if not needed). */
StructLayout compute_layout(const StructDef& def, const LayoutMap& known_layouts = {});
LayoutMap build_layout_map(const std::vector<StructDef>& struct_defs);

}  // namespace fusion

#endif
