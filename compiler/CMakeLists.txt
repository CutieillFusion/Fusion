set(FUSION_COMPILER_SOURCES
  src/ast.cpp
  src/lexer.cpp
  src/parser.cpp
  src/layout.cpp
  src/sema.cpp
  src/codegen.cpp
  src/multifile.cpp
)
add_library(fusion_compiler STATIC ${FUSION_COMPILER_SOURCES})
target_include_directories(fusion_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})
target_compile_definitions(fusion_compiler PUBLIC FUSION_HAVE_LLVM=1)

add_executable(fusion src/main.cpp)
target_link_libraries(fusion PRIVATE fusion_compiler)
target_include_directories(fusion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
include("${CMAKE_SOURCE_DIR}/cmake/LinkFusionLLVM.cmake")
fusion_target_link_llvm(fusion)
# Force-include static runtime so rt_print_i64 is in the binary (nothing references it at link time).
# Export global symbols so dlsym(RTLD_DEFAULT, "rt_print_i64") finds it at JIT run time.
target_link_libraries(fusion PRIVATE ${CMAKE_DL_LIBS})
if(FUSION_LIBFFI_FOUND)
  target_link_libraries(fusion PRIVATE LibFFI::LibFFI)
endif()
target_link_options(fusion PRIVATE
  "LINKER:--whole-archive,$<TARGET_FILE:runtime_static>,--no-whole-archive"
  "LINKER:--export-dynamic"
)
