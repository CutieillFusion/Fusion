# LinkFusionLLVM.cmake
# Call fusion_target_link_llvm(TARGET) to link TARGET against LLVM and FUSION_LLVM_EXTRA_SYSTEM_LIBS.
# Requires LLVM_FOUND and FUSION_LLVM_EXTRA_SYSTEM_LIBS (from root after FusionLLVMExtraLibs.cmake).
# Use from compiler/ and tests/ for fusion and fusion_test_compiler.

function(fusion_target_link_llvm TARGET)
  target_include_directories(${TARGET} PRIVATE ${LLVM_INCLUDE_DIRS})
  if(TARGET LLVMSupport)
    include("${LLVM_CMAKE_DIR}/LLVM-Config.cmake")
    include("${CMAKE_SOURCE_DIR}/cmake/FusionLLVMComponents.cmake")
    llvm_map_components_to_libnames(FUSION_LLVM_LIBS ${FUSION_LLVM_COMPONENTS})
    include("${CMAKE_SOURCE_DIR}/cmake/SanitizeLLVMLibs.cmake")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
      target_link_libraries(${TARGET} PRIVATE "$<LINK_GROUP:RESCAN,${FUSION_LLVM_LIBRARY_PATHS}>" ${FUSION_LLVM_LIBS_SANITIZED})
    else()
      target_link_libraries(${TARGET} PRIVATE "LINKER:--start-group" ${FUSION_LLVM_LIBRARY_PATHS} "LINKER:--end-group" ${FUSION_LLVM_LIBS_SANITIZED})
    endif()
  elseif(TARGET LLVM::Support)
    target_link_libraries(${TARGET} PRIVATE LLVM::Support LLVM::Core LLVM::ExecutionEngine LLVM::OrcJIT)
  else()
    target_link_libraries(${TARGET} PRIVATE ${LLVM_LIBRARIES})
  endif()
  target_compile_definitions(${TARGET} PRIVATE FUSION_HAVE_LLVM=1)
  target_link_libraries(${TARGET} PRIVATE ${FUSION_LLVM_EXTRA_SYSTEM_LIBS})
endfunction()
