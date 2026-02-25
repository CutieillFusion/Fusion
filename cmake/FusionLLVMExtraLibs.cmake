# FusionLLVMExtraLibs.cmake
# LLVM static libs need explicit system deps (terminfo, zlib). Sets FUSION_LLVM_EXTRA_SYSTEM_LIBS
# for use by LinkFusionLLVM.cmake. Include from root CMakeLists.txt after LLVM is found.

set(FUSION_LLVM_EXTRA_SYSTEM_LIBS "")
# Terminfo/ncurses for LLVM Support (clusters often lack libtinfo.so dev symlink).
set(_fusion_terminfo_candidates
  /usr/lib/x86_64-linux-gnu/libtinfo.so.6.4
  /usr/lib/x86_64-linux-gnu/libtinfo.so.6
  /usr/lib/x86_64-linux-gnu/libncursesw.so.6.4
  /usr/lib/x86_64-linux-gnu/libncurses.so.6.4
  /usr/local/miniforge/miniforge3/lib/libtinfo.so.6.5
  /usr/local/miniforge/miniforge3/lib/libncursesw.so
  /usr/local/miniforge/miniforge3/lib/libncurses.so
)
set(FUSION_TERMINFO_LIB "")
foreach(_cand IN LISTS _fusion_terminfo_candidates)
  if(EXISTS "${_cand}")
    set(FUSION_TERMINFO_LIB "${_cand}")
    break()
  endif()
endforeach()
if(FUSION_TERMINFO_LIB)
  message(STATUS "Using terminfo/ncurses runtime lib: ${FUSION_TERMINFO_LIB}")
  list(APPEND FUSION_LLVM_EXTRA_SYSTEM_LIBS "${FUSION_TERMINFO_LIB}")
else()
  message(WARNING "No usable terminfo/ncurses runtime library found; LLVM Support may fail to link")
endif()
# Zlib for LLVM Support.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  list(APPEND FUSION_LLVM_EXTRA_SYSTEM_LIBS ZLIB::ZLIB)
else()
  find_file(FUSION_ZLIB_RUNTIME
    NAMES libz.so.1 libz.so
    PATHS /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/local/miniforge/miniforge3/lib
    NO_DEFAULT_PATH
  )
  if(FUSION_ZLIB_RUNTIME)
    message(STATUS "Using zlib runtime lib for Fusion: ${FUSION_ZLIB_RUNTIME}")
    list(APPEND FUSION_LLVM_EXTRA_SYSTEM_LIBS "${FUSION_ZLIB_RUNTIME}")
  else()
    message(WARNING "No zlib runtime library found; LLVM Support may fail to link")
  endif()
endif()
