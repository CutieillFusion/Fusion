#include <iostream>
#include <string>

#ifdef FUSION_HAVE_LLVM
#include "llvm/Config/llvm-config.h"
#endif

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Fusion compiler – usage: fusion [options] <input.fusion>\n";
      std::cout << "  --help, -h       Show this help\n";
      std::cout << "  --version, -v   Show compiler and LLVM version\n";
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
  }
  std::cout << "Fusion compiler – usage: fusion [options] <input.fusion>\n";
  std::cout << "  --help, -h       Show this help\n";
  std::cout << "  --version, -v   Show compiler and LLVM version\n";
  return 0;
}
