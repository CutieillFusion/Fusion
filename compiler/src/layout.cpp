#include "layout.hpp"

namespace fusion {

StructLayout compute_layout(const StructDef& def, const LayoutMap& known_layouts) {
  StructLayout out;
  if (def.fields.empty()) return out;

  size_t alignment = 0;
  size_t offset = 0;

  for (size_t i = 0; i < def.fields.size(); ++i) {
    const std::string& fname = def.fields[i].first;
    FfiType ty = def.fields[i].second;
    std::string struct_type = (i < def.field_type_names.size()) ? def.field_type_names[i] : "";

    if (!struct_type.empty()) {
      // Embedded struct field only when the type is a known struct with layout
      auto it = known_layouts.find(struct_type);
      bool is_embedded = (it != known_layouts.end() && it->second.size > 0 &&
                          it->second.alignment != 0);
      if (is_embedded) {
        size_t align = it->second.alignment;
        size_t size = it->second.size;
        if (alignment < align) alignment = align;
        size_t rem = offset % align;
        if (rem != 0) offset += align - rem;
        FieldLayout fl;
        fl.offset = offset;
        fl.type = FfiType::Void;
        fl.struct_name = struct_type;
        out.fields.push_back({fname, fl});
        offset += size;
      } else {
        // ptr[T] where T is not a known struct (e.g. char) or not yet in map
        FfiType layout_ty = (ty == FfiType::Void) ? FfiType::Ptr : ty;
        size_t align = ffi_type_align(layout_ty);
        size_t size = ffi_type_size(layout_ty);
        if (align == 0 || size == 0) continue;
        if (alignment < align) alignment = align;
        size_t rem = offset % align;
        if (rem != 0) offset += align - rem;
        FieldLayout fl;
        fl.offset = offset;
        fl.type = FfiType::Ptr;  /* pointer slot so sema/codegen see Ptr */
        fl.struct_name = struct_type;  /* e.g. Value for ptr[Value], so array element struct is known */
        out.fields.push_back({fname, fl});
        offset += size;
      }
    } else {
      size_t align = ffi_type_align(ty);
      size_t size = ffi_type_size(ty);
      if (align == 0 || size == 0) continue;
      if (alignment < align) alignment = align;
      size_t rem = offset % align;
      if (rem != 0) offset += align - rem;
      FieldLayout fl;
      fl.offset = offset;
      fl.type = ty;
      out.fields.push_back({fname, fl});
      offset += size;
    }
  }

  out.alignment = alignment;
  if (alignment > 0) {
    size_t rem = offset % alignment;
    if (rem != 0) offset += alignment - rem;
  }
  out.size = offset;
  return out;
}

LayoutMap build_layout_map(const std::vector<StructDef>& struct_defs) {
  LayoutMap map;
  // Iterative topological resolution: keep computing until stable
  size_t prev_size = 0;
  while (map.size() < struct_defs.size()) {
    for (const StructDef& def : struct_defs) {
      if (map.count(def.name)) continue;
      // Check all embedded struct fields are already in map
      bool ready = true;
      for (size_t i = 0; i < def.fields.size(); ++i) {
        std::string struct_type = (i < def.field_type_names.size()) ? def.field_type_names[i] : "";
        if (!struct_type.empty() && !map.count(struct_type)) {
          ready = false;
          break;
        }
      }
      if (ready) {
        map[def.name] = compute_layout(def, map);
      }
    }
    if (map.size() == prev_size) break;  // no progress = circular or unresolvable
    prev_size = map.size();
  }
  // Add any remaining structs that couldn't be resolved (e.g., circular or unknown deps)
  for (const StructDef& def : struct_defs) {
    if (!map.count(def.name)) {
      map[def.name] = compute_layout(def, map);
    }
  }
  return map;
}

}  // namespace fusion
