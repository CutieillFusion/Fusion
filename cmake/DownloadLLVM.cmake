# DownloadLLVM.cmake - Download pre-built LLVM if not found (Linux, no sudo).
# Sets LLVM_DIR so find_package(LLVM) can succeed after inclusion.

if(LLVM_FOUND)
  return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()

# Option: set to OFF to skip auto-download (e.g. if you provide LLVM yourself).
set(FUSION_DOWNLOAD_LLVM ON CACHE BOOL "Download pre-built LLVM if not found (Linux only)")
if(NOT FUSION_DOWNLOAD_LLVM)
  return()
endif()

# Pre-built LLVM release (x86_64 Linux; Ubuntu 18.04 build usually works on other glibc-based x86_64).
# For 18.1.8 the only x86_64 Linux binary on GitHub is ubuntu-18.04.
set(FUSION_LLVM_VERSION "18.1.8" CACHE STRING "LLVM version to download if not found")
set(FUSION_LLVM_TARBALL_SUFFIX "x86_64-linux-gnu-ubuntu-18.04" CACHE STRING "Pre-built tarball suffix (e.g. x86_64-linux-gnu-ubuntu-18.04)")
set(_fusion_llvm_tarball "clang+llvm-${FUSION_LLVM_VERSION}-${FUSION_LLVM_TARBALL_SUFFIX}.tar.xz")
set(_fusion_llvm_url "https://github.com/llvm/llvm-project/releases/download/llvmorg-${FUSION_LLVM_VERSION}/${_fusion_llvm_tarball}")
set(_fusion_deps_dir "${CMAKE_BINARY_DIR}/deps/llvm")
set(_fusion_tarball_path "${_fusion_deps_dir}/${_fusion_llvm_tarball}")

# Check if we already have a usable LLVM in deps (from a previous run).
file(GLOB _fusion_llvm_extracted "${_fusion_deps_dir}/clang+llvm-*")
if(_fusion_llvm_extracted)
  list(GET _fusion_llvm_extracted 0 _fusion_llvm_root)
  set(_fusion_llvm_cmake "${_fusion_llvm_root}/lib/cmake/llvm")
  if(EXISTS "${_fusion_llvm_cmake}/LLVMConfig.cmake")
    set(LLVM_DIR "${_fusion_llvm_cmake}" CACHE PATH "LLVM config (auto-downloaded)" FORCE)
    message(STATUS "Using previously downloaded LLVM at ${_fusion_llvm_root}")
    return()
  endif()
endif()

# Download tarball if missing.
if(NOT EXISTS "${_fusion_tarball_path}")
  message(STATUS "Downloading pre-built LLVM ${FUSION_LLVM_VERSION} (one-time, no sudo)...")
  file(MAKE_DIRECTORY "${_fusion_deps_dir}")
  file(DOWNLOAD
    "${_fusion_llvm_url}"
    "${_fusion_tarball_path}"
    TLS_VERIFY ON
    SHOW_PROGRESS
    STATUS _fusion_dl_status
    LOG _fusion_dl_log
    INACTIVITY_TIMEOUT 60
  )
  list(GET _fusion_dl_status 0 _fusion_dl_code)
  if(NOT _fusion_dl_code EQUAL 0)
    list(GET _fusion_dl_status 1 _fusion_dl_msg)
    message(WARNING "LLVM download failed: ${_fusion_dl_msg}. Build will continue without LLVM (fusion --version will report 'LLVM not linked').")
    return()
  endif()
  message(STATUS "LLVM tarball downloaded.")
endif()

# Extract (creates one top-level dir like clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-22.04).
message(STATUS "Extracting LLVM...")
execute_process(
  COMMAND ${CMAKE_COMMAND} -E tar xf "${_fusion_tarball_path}"
  WORKING_DIRECTORY "${_fusion_deps_dir}"
  RESULT_VARIABLE _fusion_tar_result
)
if(NOT _fusion_tar_result EQUAL 0)
  message(WARNING "LLVM extract failed. Build will continue without LLVM.")
  return()
endif()

file(GLOB _fusion_llvm_extracted "${_fusion_deps_dir}/clang+llvm-*")
if(NOT _fusion_llvm_extracted)
  message(WARNING "LLVM extract did not produce expected directory. Build will continue without LLVM.")
  return()
endif()
list(GET _fusion_llvm_extracted 0 _fusion_llvm_root)
set(_fusion_llvm_cmake "${_fusion_llvm_root}/lib/cmake/llvm")
if(NOT EXISTS "${_fusion_llvm_cmake}/LLVMConfig.cmake")
  message(WARNING "LLVMConfig.cmake not found under ${_fusion_llvm_root}. Build will continue without LLVM.")
  return()
endif()

set(LLVM_DIR "${_fusion_llvm_cmake}" CACHE PATH "LLVM config (auto-downloaded)" FORCE)
message(STATUS "LLVM ${FUSION_LLVM_VERSION} ready (auto-downloaded to ${_fusion_deps_dir})")
