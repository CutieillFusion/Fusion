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

# --- fusion_test_compiler (GoogleTest) ---
add_executable(fusion_test_compiler compiler/test_compiler.cpp)
target_link_libraries(fusion_test_compiler PRIVATE GTest::gtest_main fusion_compiler ${CMAKE_DL_LIBS})
target_link_options(fusion_test_compiler PRIVATE
  "LINKER:--whole-archive,$<TARGET_FILE:runtime_static>,--no-whole-archive"
  "LINKER:--export-dynamic"
)
if(FUSION_LIBFFI_FOUND)
  target_link_libraries(fusion_test_compiler PRIVATE LibFFI::LibFFI)
endif()
target_include_directories(fusion_test_compiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../compiler ${CMAKE_CURRENT_SOURCE_DIR}/../compiler/src)
fusion_target_link_llvm(fusion_test_compiler)

include(GoogleTest)
gtest_discover_tests(fusion_test_compiler
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  PROPERTIES LABELS "compiler" ENVIRONMENT "GTEST_COLOR=yes"
)

# --- fusion_test_runtime (GoogleTest, C++ calling C runtime) ---
set(FUSION_RUNTIME_GTEST_SOURCES
  runtime/test_runtime_basic.cpp
  runtime/test_runtime_dl.cpp
  runtime/test_runtime_ffi.cpp
)
add_executable(fusion_test_runtime ${FUSION_RUNTIME_GTEST_SOURCES})
target_link_libraries(fusion_test_runtime PRIVATE GTest::gtest_main runtime m)
target_include_directories(fusion_test_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../runtime_c/include)

gtest_discover_tests(fusion_test_runtime
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  PROPERTIES LABELS "runtime" ENVIRONMENT "GTEST_COLOR=yes"
)

# --- CTest: CLI smoke tests ---
add_test(NAME cli.help COMMAND $<TARGET_FILE:fusion> --help)
set_tests_properties(cli.help PROPERTIES
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  LABELS "cli;smoke"
)

# CLI smoke: fusion run test.fusion with LC_ALL=C (use repo-root test.fusion for Phase 1)
add_test(NAME cli.run.test_fusion
  COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C $<TARGET_FILE:fusion> run ${CMAKE_SOURCE_DIR}/test.fusion
)
set_tests_properties(cli.run.test_fusion PROPERTIES
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  LABELS "cli;smoke"
)
