#include "multifile.hpp"
#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace fusion {

static std::string get_dir(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return ".";
  return path.substr(0, slash);
}

static std::string resolve_import(const std::string& dir, const std::string& name) {
  std::string p = dir + "/" + name;
  if (p.size() < 7 || p.substr(p.size() - 7) != ".fusion")
    p += ".fusion";
  return p;
}

static std::string canonical_path(const std::string& path) {
  char resolved[4096];
  if (realpath(path.c_str(), resolved) != nullptr)
    return std::string(resolved);
  return path;
}

static bool fndecl_matches_fndef(const FnDecl& decl, const FnDef& def) {
  if (decl.name != def.name) return false;
  if (decl.params.size() != def.params.size()) return false;
  for (size_t j = 0; j < decl.params.size(); ++j) {
    if (decl.params[j].second != def.params[j].second) return false;
    if (decl.param_type_names.size() > j && def.param_type_names.size() > j &&
        decl.param_type_names[j] != def.param_type_names[j])
      return false;
  }
  if (decl.return_type != def.return_type) return false;
  if (decl.return_type_name != def.return_type_name) return false;
  return true;
}

static std::string get_lib_path_by_name(const Program* prog, const std::string& lib_name) {
  for (const auto& lib : prog->libs)
    if (lib.name == lib_name) return lib.path;
  return "";
}

static bool extern_fn_signature_equal(const ExternFn& a, const ExternFn& b) {
  if (a.name != b.name) return false;
  if (a.return_type != b.return_type || a.return_type_name != b.return_type_name) return false;
  if (a.params.size() != b.params.size()) return false;
  for (size_t i = 0; i < a.params.size(); ++i) {
    if (a.params[i].second != b.params[i].second) return false;
    if (a.param_type_names[i] != b.param_type_names[i]) return false;
  }
  return true;
}

static bool lib_has_path(const Program* prog, const std::string& path) {
  for (const auto& lib : prog->libs)
    if (lib.path == path) return true;
  return false;
}

static const ExternFn* find_extern_fn(const Program* prog, const std::string& name) {
  for (const auto& e : prog->extern_fns)
    if (e.name == name) return &e;
  return nullptr;
}

static std::string load_and_build_postorder(
    const std::string& file_path,
    const ImportLib* request,
    std::map<std::string, ProgramPtr>& cache,
    std::set<std::string>& loading,
    std::vector<std::pair<Program*, const ImportLib*>>& postorder) {
  std::string canon = canonical_path(file_path);
  if (loading.count(canon)) return "circular import involving '" + canon + "'";
  Program* prog = nullptr;
  if (cache.count(canon)) {
    prog = cache[canon].get();
  } else {
    std::ifstream f(file_path);
    if (!f) return "cannot open '" + file_path + "' (resolved from import)";
    std::stringstream buf;
    buf << f.rdbuf();
    std::string source = buf.str();
    auto tokens = lex(source);
    auto pr = parse(tokens);
    if (!pr.ok()) return "parse error in '" + file_path + "': " + pr.error.message;
    cache[canon] = std::move(pr.program);
    prog = cache[canon].get();
    loading.insert(canon);
    std::string dir = get_dir(file_path);
    for (const ImportLib& il : prog->import_libs) {
      std::string dep_path = resolve_import(dir, il.name);
      std::string err = load_and_build_postorder(dep_path, &il, cache, loading, postorder);
      if (!err.empty()) return err;
    }
    loading.erase(canon);
  }
  postorder.push_back({prog, request});
  return "";
}

static std::string merge_library_into_main(Program* main_prog, Program* lib_prog, const ImportLib* request) {
  for (const std::string& sname : request->struct_names) {
    const StructDef* sdef = nullptr;
    for (const auto& s : lib_prog->struct_defs)
      if (s.exported && s.name == sname) { sdef = &s; break; }
    if (!sdef) return "import lib '" + request->name + "': missing exported struct " + sname;
    for (const auto& s : main_prog->struct_defs)
      if (s.name == sname) return "duplicate symbol '" + sname + "': exported by lib '" + request->name + "' and already defined";
    main_prog->struct_defs.push_back(*sdef);
  }
  for (const FnDecl& fdecl : request->fn_decls) {
    const FnDef* fdef = nullptr;
    for (const auto& f : lib_prog->user_fns)
      if (f.exported && fndecl_matches_fndef(fdecl, f)) { fdef = &f; break; }
    if (!fdef) return "import lib '" + request->name + "': missing or signature mismatch for exported fn " + fdecl.name;
    for (const auto& f : main_prog->user_fns)
      if (f.name == fdecl.name) return "duplicate symbol '" + fdecl.name + "': exported by lib '" + request->name + "' and already defined";
    main_prog->user_fns.push_back(fdef->clone());
  }
  std::map<std::string, std::string> path_to_main_lib_name;
  for (const ExternLib& lib : lib_prog->libs) {
    if (!lib_has_path(main_prog, lib.path)) {
      std::string main_name = "__lib" + std::to_string(main_prog->libs.size());
      main_prog->libs.push_back(lib);
      main_prog->libs.back().name = main_name;
      path_to_main_lib_name[lib.path] = main_name;
    } else {
      for (const auto& m : main_prog->libs)
        if (m.path == lib.path) { path_to_main_lib_name[lib.path] = m.name; break; }
    }
  }
  for (const ExternFn& ext : lib_prog->extern_fns) {
    std::string ext_path = get_lib_path_by_name(lib_prog, ext.lib_name);
    const ExternFn* existing = find_extern_fn(main_prog, ext.name);
    if (existing) {
      std::string existing_path = get_lib_path_by_name(main_prog, existing->lib_name);
      if (existing_path != ext_path || !extern_fn_signature_equal(*existing, ext))
        return "extern fn '" + ext.name + "' declared by lib '" + request->name + "' conflicts (different signature or lib)";
    } else {
      ExternFn e2 = ext;
      auto it = path_to_main_lib_name.find(ext_path);
      if (it != path_to_main_lib_name.end()) e2.lib_name = it->second;
      main_prog->extern_fns.push_back(std::move(e2));
    }
  }
  return "";
}

std::string resolve_imports_and_merge(const std::string& main_path, Program* main_prog) {
  if (main_prog->import_libs.empty()) return "";
  std::map<std::string, ProgramPtr> cache;
  std::set<std::string> loading;
  std::vector<std::pair<Program*, const ImportLib*>> postorder;
  std::string main_dir = get_dir(main_path);
  for (const ImportLib& il : main_prog->import_libs) {
    std::string path = resolve_import(main_dir, il.name);
    std::string err = load_and_build_postorder(path, &il, cache, loading, postorder);
    if (!err.empty()) return err;
  }
  for (const auto& p : postorder) {
    std::string err = merge_library_into_main(main_prog, p.first, p.second);
    if (!err.empty()) return err;
  }
  return "";
}

}  // namespace fusion
