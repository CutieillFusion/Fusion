add_executable(test_runner test_runner.c)
target_link_libraries(test_runner PRIVATE runtime)
add_test(NAME test_runner COMMAND test_runner)

add_executable(fusion_tests fusion_tests.cpp)
target_link_libraries(fusion_tests PRIVATE fusion_compiler ${CMAKE_DL_LIBS})
# Force-include static runtime so rt_print_i64 is in the binary; export for dlsym(RTLD_DEFAULT, "rt_print_i64")
target_link_options(fusion_tests PRIVATE
  "LINKER:--whole-archive,$<TARGET_FILE:runtime_static>,--no-whole-archive"
  "LINKER:--export-dynamic"
)
target_include_directories(fusion_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../compiler ${CMAKE_CURRENT_SOURCE_DIR}/../compiler/src)
if(LLVM_FOUND)
  target_include_directories(fusion_tests PRIVATE ${LLVM_INCLUDE_DIRS})
  if(TARGET LLVMSupport)
    include("${LLVM_CMAKE_DIR}/LLVM-Config.cmake")
    include("${CMAKE_SOURCE_DIR}/cmake/FusionLLVMComponents.cmake")
    llvm_map_components_to_libnames(FUSION_LLVM_LIBS ${FUSION_LLVM_COMPONENTS})
    include("${CMAKE_SOURCE_DIR}/cmake/SanitizeLLVMLibs.cmake")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
      target_link_libraries(fusion_tests PRIVATE "$<LINK_GROUP:RESCAN,${FUSION_LLVM_LIBRARY_PATHS}>" ${FUSION_LLVM_LIBS_SANITIZED})
    else()
      target_link_libraries(fusion_tests PRIVATE "LINKER:--start-group" ${FUSION_LLVM_LIBRARY_PATHS} "LINKER:--end-group" ${FUSION_LLVM_LIBS_SANITIZED})
    endif()
  elseif(TARGET LLVM::Support)
    target_link_libraries(fusion_tests PRIVATE LLVM::Support LLVM::Core LLVM::ExecutionEngine LLVM::OrcJIT)
  else()
    target_link_libraries(fusion_tests PRIVATE ${LLVM_LIBRARIES})
  endif()
  target_compile_definitions(fusion_tests PRIVATE FUSION_HAVE_LLVM=1)
  target_link_libraries(fusion_tests PRIVATE ${FUSION_LLVM_EXTRA_SYSTEM_LIBS})
endif()
add_test(NAME fusion_tests COMMAND fusion_tests)

add_test(NAME fusion_help COMMAND $<TARGET_FILE:fusion> --help)
set_tests_properties(fusion_help PROPERTIES
  REQUIRED_FILES "$<TARGET_FILE:fusion>"
)

add_test(NAME fusion_run_print_1_2 COMMAND $<TARGET_FILE:fusion> run ${CMAKE_SOURCE_DIR}/test.fusion)
set_tests_properties(fusion_run_print_1_2 PROPERTIES
  REQUIRED_FILES "$<TARGET_FILE:fusion>"
  ENVIRONMENT "LC_ALL=C"
)
