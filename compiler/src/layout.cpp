#include "layout.hpp"

namespace fusion {

StructLayout compute_layout(const StructDef& def) {
  StructLayout out;
  if (def.fields.empty()) return out;

  size_t alignment = 0;
  size_t offset = 0;

  for (const auto& p : def.fields) {
    const std::string& fname = p.first;
    FfiType ty = p.second;
    size_t align = ffi_type_align(ty);
    size_t size = ffi_type_size(ty);
    if (align == 0 || size == 0) continue;
    if (alignment < align) alignment = align;
    /* Next offset = next multiple of align >= offset */
    size_t rem = offset % align;
    if (rem != 0) offset += align - rem;
    FieldLayout fl;
    fl.offset = offset;
    fl.type = ty;
    out.fields.push_back({fname, fl});
    offset += size;
  }

  out.alignment = alignment;
  /* Struct size = round up to alignment */
  size_t rem = offset % alignment;
  if (rem != 0) offset += alignment - rem;
  out.size = offset;
  return out;
}

LayoutMap build_layout_map(const std::vector<StructDef>& struct_defs) {
  LayoutMap map;
  for (const StructDef& def : struct_defs)
    map[def.name] = compute_layout(def);
  return map;
}

}  // namespace fusion
