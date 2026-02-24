set(FUSION_COMPILER_SOURCES
  src/ast.cpp
  src/lexer.cpp
  src/parser.cpp
  src/sema.cpp
  src/codegen.cpp
)
add_library(fusion_compiler STATIC ${FUSION_COMPILER_SOURCES})
target_include_directories(fusion_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(LLVM_FOUND)
  target_include_directories(fusion_compiler PUBLIC ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(fusion_compiler PUBLIC FUSION_HAVE_LLVM=1)
endif()

add_executable(fusion src/main.cpp)
target_link_libraries(fusion PRIVATE fusion_compiler)
target_include_directories(fusion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(LLVM_FOUND)
  target_include_directories(fusion PRIVATE ${LLVM_INCLUDE_DIRS})
  # Pre-built LLVM exposes LLVMSupport, LLVMCore, etc. (no LLVM:: prefix).
  # Link order: objects / fusion_compiler, then LLVM .a (group), then sanitized system libs (z, tinfo, etc.).
  if(TARGET LLVMSupport)
    include("${LLVM_CMAKE_DIR}/LLVM-Config.cmake")
    include("${CMAKE_SOURCE_DIR}/cmake/FusionLLVMComponents.cmake")
    llvm_map_components_to_libnames(FUSION_LLVM_LIBS ${FUSION_LLVM_COMPONENTS})
    include("${CMAKE_SOURCE_DIR}/cmake/SanitizeLLVMLibs.cmake")
    # Link LLVM libs after object files; use LINK_GROUP RESCAN (--start-group/--end-group)
    # so mutual dependencies between static .a files resolve regardless of order.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
      target_link_libraries(fusion PRIVATE "$<LINK_GROUP:RESCAN,${FUSION_LLVM_LIBRARY_PATHS}>" ${FUSION_LLVM_LIBS_SANITIZED})
    else()
      # Wrap .a libs in --start-group/--end-group so mutual deps resolve (order-independent).
      target_link_libraries(fusion PRIVATE "LINKER:--start-group" ${FUSION_LLVM_LIBRARY_PATHS} "LINKER:--end-group" ${FUSION_LLVM_LIBS_SANITIZED})
    endif()
  elseif(TARGET LLVM::Support)
    target_link_libraries(fusion PRIVATE LLVM::Support LLVM::Core LLVM::ExecutionEngine LLVM::OrcJIT)
  else()
    target_link_libraries(fusion PRIVATE ${LLVM_LIBRARIES})
  endif()
  target_compile_definitions(fusion PRIVATE FUSION_HAVE_LLVM=1)
  target_link_libraries(fusion PRIVATE ${FUSION_LLVM_EXTRA_SYSTEM_LIBS})
endif()
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
