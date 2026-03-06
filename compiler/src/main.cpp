#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include "runtime.h"
#ifdef FUSION_HAVE_LLVM
#include <llvm/IR/LLVMContext.h>
#endif

#include "ast.hpp"
#include "codegen.hpp"
#include "lexer.hpp"
#include "multifile.hpp"
#include "parser.hpp"
#include "sema.hpp"

static int run_file(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    std::cerr << "fusion: cannot open '" << path << "'\n";
    return 1;
  }
  std::stringstream buf;
  buf << f.rdbuf();
  std::string source = buf.str();
  if (getenv("FUSION_DEBUG")) {
    std::cerr << "fusion: running " << path << " (first line: ";
    std::cerr << source.substr(0, source.find('\n')) << ")\n";
  }

  auto tokens = fusion::lex(source);
  auto parse_result = fusion::parse(tokens);
  if (!parse_result.ok()) {
    std::cerr << "fusion: parse error at " << parse_result.error.line << ":"
              << parse_result.error.column << " " << parse_result.error.message << "\n";
    return 1;
  }

  std::string merge_err = fusion::resolve_imports_and_merge(path, parse_result.program.get());
  if (!merge_err.empty()) {
    std::cerr << "fusion: " << merge_err << "\n";
    return 1;
  }

  auto sema_result = fusion::check(parse_result.program.get());
  if (!sema_result.ok) {
    std::cerr << "fusion: " << sema_result.error.message << "\n";
    return 1;
  }

#ifdef FUSION_HAVE_LLVM
  rt_init();
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  if (!module) {
    std::cerr << "fusion: codegen failed";
    const std::string& err = fusion::codegen_last_error();
    if (!err.empty()) std::cerr << ": " << err;
    std::cerr << "\n";
    rt_shutdown();
    return 1;
  }
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  if (!jit_result.ok) {
    std::cerr << "fusion: " << jit_result.error << "\n";
    rt_shutdown();
    return 1;
  }
  rt_shutdown();
#else
  std::cerr << "fusion: LLVM not available, cannot run\n";
  return 1;
#endif
  return 0;
}

static int build_file(const std::string& path, const std::string& out_path) {
  std::ifstream f(path);
  if (!f) {
    std::cerr << "fusion: cannot open '" << path << "'\n";
    return 1;
  }
  std::stringstream buf;
  buf << f.rdbuf();
  std::string source = buf.str();

  auto tokens = fusion::lex(source);
  auto parse_result = fusion::parse(tokens);
  if (!parse_result.ok()) {
    std::cerr << "fusion: parse error at " << parse_result.error.line << ":"
              << parse_result.error.column << " " << parse_result.error.message << "\n";
    return 1;
  }

  std::string merge_err = fusion::resolve_imports_and_merge(path, parse_result.program.get());
  if (!merge_err.empty()) {
    std::cerr << "fusion: " << merge_err << "\n";
    return 1;
  }

  auto sema_result = fusion::check(parse_result.program.get());
  if (!sema_result.ok) {
    std::cerr << "fusion: " << sema_result.error.message << "\n";
    return 1;
  }

#ifdef FUSION_HAVE_LLVM
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, parse_result.program.get());
  if (!module) {
    std::cerr << "fusion: codegen failed";
    const std::string& err = fusion::codegen_last_error();
    if (!err.empty()) std::cerr << ": " << err;
    std::cerr << "\n";
    return 1;
  }

  std::string obj_path = out_path + ".o";
  auto obj_result = fusion::emit_object(std::move(module), obj_path);
  if (!obj_result.ok) {
    std::cerr << "fusion: emit_object failed: " << obj_result.error << "\n";
    return 1;
  }

  // Build linker command
  std::string cmd = "cc " + obj_path + " -o " + out_path +
                    " -Wl,--whole-archive " FUSION_RUNTIME_STATIC_PATH " -Wl,--no-whole-archive"
                    " -ldl -lpthread";
#ifdef FUSION_HAVE_LIBFFI
  cmd += " -lffi";
#endif
#ifdef FUSION_HAVE_LIBCURL
  cmd += " -lcurl";
#endif

  int ret = std::system(cmd.c_str());
  std::remove(obj_path.c_str());
  if (ret != 0) {
    std::cerr << "fusion: linker failed (exit " << ret << ")\n";
    return 1;
  }
  return 0;
#else
  std::cerr << "fusion: LLVM not available, cannot build\n";
  return 1;
#endif
}

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Fusion compiler – usage: fusion [options] <input.fusion>\n";
      std::cout << "  --help, -h             Show this help\n";
      std::cout << "  --version, -v          Show compiler and LLVM version\n";
      std::cout << "  run <file>             Compile and JIT-run a .fusion file\n";
      std::cout << "  <file>                 Compile and JIT-run a .fusion file\n";
      std::cout << "  <file> -o <out>        Compile a .fusion file to a native binary\n";
      std::cout << "  build <file> [-o out]  Compile a .fusion file to a native binary\n";
      return 0;
    }
    if (arg == "--version" || arg == "-v") {
#ifdef FUSION_HAVE_LLVM
      std::cout << "Fusion compiler (LLVM " << LLVM_VERSION_STRING << ")\n";
#else
      std::cout << "Fusion compiler (LLVM not linked)\n";
#endif
      return 0;
    }
    if (arg == "run" && i + 1 < argc) {
      return run_file(argv[++i]);
    }
    if (arg == "build" && i + 1 < argc) {
      std::string src = argv[++i];
      std::string out;
      // Check for -o <name>
      if (i + 2 < argc && std::strcmp(argv[i + 1], "-o") == 0) {
        out = argv[i + 2];
        i += 2;
      } else {
        // Strip .fusion extension if present
        out = src;
        if (out.size() > 7 && out.substr(out.size() - 7) == ".fusion")
          out = out.substr(0, out.size() - 7);
      }
      return build_file(src, out);
    }
    if (arg.size() > 0 && arg[0] != '-') {
      // Check for -o <name> following the filename
      if (i + 2 < argc && std::strcmp(argv[i + 1], "-o") == 0) {
        std::string out = argv[i + 2];
        return build_file(arg, out);
      }
      return run_file(arg);
    }
  }
  std::cout << "Fusion compiler – usage: fusion [options] <input.fusion>\n";
  std::cout << "  --help, -h             Show this help\n";
  std::cout << "  --version, -v          Show compiler and LLVM version\n";
  std::cout << "  run <file>             Compile and JIT-run a .fusion file\n";
  std::cout << "  build <file> [-o out]  Compile a .fusion file to a native binary\n";
  return 0;
}
