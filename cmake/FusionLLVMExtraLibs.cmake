# FusionLLVMExtraLibs.cmake
# LLVM static libs need explicit system deps (terminfo, zlib). Sets FUSION_LLVM_EXTRA_SYSTEM_LIBS
# for use by LinkFusionLLVM.cmake. Include from root CMakeLists.txt after LLVM is found.

set(FUSION_LLVM_EXTRA_SYSTEM_LIBS "")

# Terminfo/ncurses for LLVM Support (apt: libncurses-dev)
find_library(FUSION_TERMINFO_LIB NAMES tinfo ncurses)
if(FUSION_TERMINFO_LIB)
  message(STATUS "Using terminfo/ncurses runtime lib: ${FUSION_TERMINFO_LIB}")
  list(APPEND FUSION_LLVM_EXTRA_SYSTEM_LIBS "${FUSION_TERMINFO_LIB}")
else()
  message(WARNING "No usable terminfo/ncurses runtime library found; LLVM Support may fail to link")
endif()

# Zlib for LLVM Support (apt: zlib1g-dev)
find_package(ZLIB REQUIRED)
list(APPEND FUSION_LLVM_EXTRA_SYSTEM_LIBS ZLIB::ZLIB)

# Zstd for LLVM Support compression (apt: libzstd-dev)
find_library(FUSION_ZSTD_LIB NAMES zstd)
if(FUSION_ZSTD_LIB)
  list(APPEND FUSION_LLVM_EXTRA_SYSTEM_LIBS "${FUSION_ZSTD_LIB}")
else()
  message(WARNING "libzstd not found; LLVM Support may fail to link (apt: libzstd-dev)")
endif()
