cmake_minimum_required(VERSION 3.16)
project(Fusion LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(FUSION_REQUIRE_LLVM "Fail configure if LLVM is not available" OFF)

find_package(LLVM QUIET CONFIG)
if(NOT LLVM_FOUND)
  include(cmake/DownloadLLVM.cmake)
  if(LLVM_DIR)
    unset(LLVM_FOUND)
    find_package(LLVM QUIET CONFIG)
  endif()
endif()
if(LLVM_FOUND)
  message(STATUS "LLVM ${LLVM_VERSION} found - fusion will support --version")
else()
  message(STATUS "LLVM not found - fusion will build without --version")
  if(FUSION_REQUIRE_LLVM)
    message(FATAL_ERROR "FUSION_REQUIRE_LLVM=ON but LLVM was not found/downloaded.")
  endif()
endif()

# LLVM static libs need explicit system deps (terminfo, zlib). Centralize for fusion and fusion_tests.
if(LLVM_FOUND)
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
endif()

enable_testing()
add_subdirectory(compiler)
add_subdirectory(runtime_c)
add_subdirectory(tests)
