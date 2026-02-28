# GoogleTest: FetchContent (pinned), only when building tests
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include("${CMAKE_SOURCE_DIR}/cmake/LinkFusionLLVM.cmake")

# --- Phase 6 test .so (struct, out-param, pointer-to-pointer) ---
add_library(fusion_phase6_so SHARED data/libfusion_phase6.c)
set_target_properties(fusion_phase6_so PROPERTIES OUTPUT_NAME fusion_phase6 PREFIX "")
set_target_properties(fusion_phase6_so PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(fusion_phase6_so PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- fusion_test_compiler_* (GoogleTest, one executable per suite) ---
set(FUSION_COMPILER_TEST_SOURCES
  compiler/test_lexer.cpp
  compiler/test_parser.cpp
  compiler/test_layout.cpp
  compiler/test_sema.cpp
  compiler/test_multifile.cpp
  compiler/test_codegen.cpp
  compiler/test_jit.cpp
)

include(GoogleTest)
foreach(src IN LISTS FUSION_COMPILER_TEST_SOURCES)
  get_filename_component(name_we ${src} NAME_WE)
  string(REPLACE "test_" "" suite ${name_we})
  add_executable(fusion_test_compiler_${suite} ${src})
  add_dependencies(fusion_test_compiler_${suite} fusion_phase6_so)
  target_link_libraries(fusion_test_compiler_${suite} PRIVATE GTest::gtest_main fusion_compiler ${CMAKE_DL_LIBS})
  target_link_options(fusion_test_compiler_${suite} PRIVATE
    "LINKER:--whole-archive,$<TARGET_FILE:runtime_static>,--no-whole-archive"
    "LINKER:--export-dynamic"
  )
  if(FUSION_LIBFFI_FOUND)
    target_link_libraries(fusion_test_compiler_${suite} PRIVATE LibFFI::LibFFI)
  endif()
  target_include_directories(fusion_test_compiler_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../compiler ${CMAKE_CURRENT_SOURCE_DIR}/../compiler/src)
  fusion_target_link_llvm(fusion_test_compiler_${suite})
  gtest_discover_tests(fusion_test_compiler_${suite}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    PROPERTIES LABELS "compiler;${suite}" ENVIRONMENT "GTEST_COLOR=yes;FUSION_PHASE6_SO=$<TARGET_FILE:fusion_phase6_so>"
  )
endforeach()

# --- fusion_test_runtime (GoogleTest, C++ calling C runtime) ---
set(FUSION_RUNTIME_GTEST_SOURCES
  runtime/test_runtime_basic.cpp
  runtime/test_runtime_dl.cpp
  runtime/test_runtime_ffi.cpp
)
add_executable(fusion_test_runtime ${FUSION_RUNTIME_GTEST_SOURCES})
target_link_libraries(fusion_test_runtime PRIVATE GTest::gtest_main runtime m ${CMAKE_DL_LIBS})
target_include_directories(fusion_test_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../runtime_c/include)
add_dependencies(fusion_test_runtime fusion_phase6_so)

gtest_discover_tests(fusion_test_runtime
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  PROPERTIES LABELS "runtime" ENVIRONMENT "GTEST_COLOR=yes;FUSION_PHASE6_SO=$<TARGET_FILE:fusion_phase6_so>"
)
