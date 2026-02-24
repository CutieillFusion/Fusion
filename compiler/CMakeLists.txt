add_executable(fusion src/main.cpp)
target_include_directories(fusion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(LLVM_FOUND)
  target_include_directories(fusion PRIVATE ${LLVM_INCLUDE_DIRS})
  # Pre-built LLVM may not define LLVM::Support; use LLVM_LIBRARIES when present.
  if(TARGET LLVM::Support)
    target_link_libraries(fusion PRIVATE LLVM::Support)
  else()
    target_link_libraries(fusion PRIVATE ${LLVM_LIBRARIES})
  endif()
  target_compile_definitions(fusion PRIVATE FUSION_HAVE_LLVM=1)
endif()
