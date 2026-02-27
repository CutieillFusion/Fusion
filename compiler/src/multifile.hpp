#ifndef FUSION_MULTIFILE_HPP
#define FUSION_MULTIFILE_HPP

#include <string>

namespace fusion {

struct Program;

/** Resolve import_libs, load and parse library files (with cache and postorder), merge into main_prog.
 *  Returns empty string on success, or an error message. */
std::string resolve_imports_and_merge(const std::string& main_path, Program* main_prog);

}  // namespace fusion

#endif
