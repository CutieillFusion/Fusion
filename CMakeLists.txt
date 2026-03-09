cmake_minimum_required(VERSION 3.16)

# ---------- optional accelerators (before project()) ----------
option(FUSION_USE_CCACHE "Use ccache if available" ON)
if(FUSION_USE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache found – using ${CCACHE_PROGRAM}")
  endif()
endif()

project(Fusion LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Hint for apt-installed LLVM 18 (cmake won't find it without this)
set(LLVM_DIR "/usr/lib/llvm-18/lib/cmake/llvm" CACHE PATH "Path to LLVMConfig.cmake (apt: llvm-18-dev installs here)")
find_package(LLVM 18.1 REQUIRED CONFIG)
message(STATUS "LLVM ${LLVM_VERSION} found - fusion will support --version")
include(cmake/FusionLLVMExtraLibs.cmake)

include(CTest)
enable_testing()

# nlohmann/json — installed via apt (nlohmann-json3-dev)
find_package(nlohmann_json 3 REQUIRED)

# CURL (for runtime HTTP) and LibFFI so FUSION_* and LibFFI::LibFFI exist for compiler/tests
include("${CMAKE_SOURCE_DIR}/cmake/FindFusionCURL.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/FindFusionLibFFI.cmake")
add_subdirectory(runtime_c)
add_subdirectory(compiler)
add_subdirectory(tests)
add_subdirectory(lsp)
# Ensure runtime_static is built before targets that link it via --whole-archive path.
add_dependencies(fusion runtime_static)
add_dependencies(fusion_test_compiler_lexer fusion_test_compiler_parser fusion_test_compiler_layout fusion_test_compiler_sema fusion_test_compiler_multifile fusion_test_compiler_codegen fusion_test_compiler_jit runtime_static)
# Build all test executables and run CTest (so "make check" works without a prior "make")
add_custom_target(check
  COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
  DEPENDS fusion_test_compiler_lexer fusion_test_compiler_parser fusion_test_compiler_layout fusion_test_compiler_sema fusion_test_compiler_multifile fusion_test_compiler_codegen fusion_test_compiler_jit fusion_test_runtime fusion_test_lsp_server
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
  DEPENDS fusion_test_compiler_lexer fusion_test_compiler_parser fusion_test_compiler_layout fusion_test_compiler_sema fusion_test_compiler_multifile fusion_test_compiler_codegen fusion_test_compiler_jit
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Running compiler tests..."
)
add_custom_target(check-cli
  COMMAND "${CMAKE_CTEST_COMMAND}" -L cli --output-on-failure
  DEPENDS fusion
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Running CLI smoke tests..."
)
add_custom_target(check-lsp
  COMMAND "${CMAKE_CTEST_COMMAND}" -L lsp --output-on-failure
  DEPENDS fusion_test_lsp_server
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Running LSP tests..."
)
if(FUSION_LIBFFI_FOUND)
  target_link_libraries(fusion PRIVATE LibFFI::LibFFI)
endif()

# ---------- optional faster linker ----------
option(FUSION_USE_FAST_LINKER "Use mold or lld if available" OFF)
if(FUSION_USE_FAST_LINKER)
  find_program(MOLD_LINKER mold)
  if(MOLD_LINKER)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
    message(STATUS "Using mold linker")
  else()
    find_program(LLD_LINKER ld.lld)
    if(LLD_LINKER)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
      message(STATUS "Using lld linker")
    else()
      message(STATUS "Neither mold nor lld found – using default linker")
    endif()
  endif()
endif()
