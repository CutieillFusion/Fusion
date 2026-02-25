cmake_minimum_required(VERSION 3.16)
project(Fusion LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(FUSION_FETCH_LIBFFI "If libffi is not found, download and build it into the build tree (enables full FFI)" ON)

find_package(LLVM QUIET CONFIG)
if(NOT LLVM_FOUND)
  include(cmake/DownloadLLVM.cmake)
  if(LLVM_DIR)
    unset(LLVM_FOUND)
    find_package(LLVM QUIET CONFIG)
  endif()
endif()
if(NOT LLVM_FOUND)
  message(FATAL_ERROR "LLVM is required but was not found. Install LLVM or enable download (Linux: FUSION_DOWNLOAD_LLVM is ON by default).")
endif()
message(STATUS "LLVM ${LLVM_VERSION} found - fusion will support --version")
include(cmake/FusionLLVMExtraLibs.cmake)

include(CTest)
enable_testing()
# runtime_c first so FUSION_LIBFFI_FOUND and LibFFI::LibFFI exist for compiler/tests
add_subdirectory(runtime_c)
add_subdirectory(compiler)
add_subdirectory(tests)
# Ensure runtime_static is built before targets that link it via --whole-archive path.
add_dependencies(fusion runtime_static)
add_dependencies(fusion_test_compiler runtime_static)
# Build all test executables and run CTest (so "make check" works without a prior "make")
add_custom_target(check
  COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
  DEPENDS fusion_test_compiler fusion_test_runtime
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Running tests..."
)
add_custom_target(check-runtime
  COMMAND "${CMAKE_CTEST_COMMAND}" -L runtime --output-on-failure
  DEPENDS fusion_test_runtime
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Running runtime tests..."
)
add_custom_target(check-compiler
  COMMAND "${CMAKE_CTEST_COMMAND}" -L compiler --output-on-failure
  DEPENDS fusion_test_compiler
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Running compiler tests..."
)
add_custom_target(check-cli
  COMMAND "${CMAKE_CTEST_COMMAND}" -L cli --output-on-failure
  DEPENDS fusion
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Running CLI smoke tests..."
)
if(FUSION_LIBFFI_FOUND)
  target_link_libraries(fusion PRIVATE LibFFI::LibFFI)
endif()
