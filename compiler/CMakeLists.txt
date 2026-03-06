find_package(Threads REQUIRED)

# Frontend library: lex/parse/sema/layout/multifile — no LLVM dependency.
# Used by fusion_lsp (LSP server) and fusion_compiler.
add_library(fusion_frontend STATIC
  src/ast.cpp
  src/lexer.cpp
  src/parser.cpp
  src/layout.cpp
  src/sema.cpp
  src/multifile.cpp
)
target_include_directories(fusion_frontend PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler library: codegen on top of frontend (requires LLVM headers at build time).
add_library(fusion_compiler STATIC src/codegen.cpp)
target_include_directories(fusion_compiler PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${LLVM_INCLUDE_DIRS}
)
target_compile_definitions(fusion_compiler PUBLIC FUSION_HAVE_LLVM=1)
target_link_libraries(fusion_compiler PUBLIC fusion_frontend)

add_executable(fusion src/main.cpp)
target_link_libraries(fusion PRIVATE fusion_compiler)
target_include_directories(fusion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/runtime_c/include)
include("${CMAKE_SOURCE_DIR}/cmake/LinkFusionLLVM.cmake")
fusion_target_link_llvm(fusion)
# Force-include static runtime so rt_print_i64 is in the binary (nothing references it at link time).
# Export global symbols so dlsym(RTLD_DEFAULT, "rt_print_i64") finds it at JIT run time.
target_link_libraries(fusion PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
if(FUSION_LIBFFI_FOUND)
  target_link_libraries(fusion PRIVATE LibFFI::LibFFI)
endif()
if(FUSION_CURL_FOUND)
  target_link_libraries(fusion PRIVATE ${FUSION_CURL_TARGET})
  if(FUSION_CURL_INCLUDE_DIR)
    target_include_directories(fusion PRIVATE ${FUSION_CURL_INCLUDE_DIR})
  endif()
endif()
target_link_options(fusion PRIVATE
  "LINKER:--whole-archive,$<TARGET_FILE:runtime_static>,--no-whole-archive"
  "LINKER:--export-dynamic"
)
