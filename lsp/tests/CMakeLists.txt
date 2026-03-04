# LSP integration tests: link fusion_lsp_core + GTest only (no transport).
# GoogleTest is fetched by the top-level tests/ so it is available here.
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(fusion_test_lsp_server test_server.cpp)

target_link_libraries(fusion_test_lsp_server PRIVATE
  fusion_lsp_core
  GTest::gtest_main
)

target_include_directories(fusion_test_lsp_server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_SOURCE_DIR}/lsp/src
  ${CMAKE_SOURCE_DIR}/compiler/src
)

gtest_discover_tests(fusion_test_lsp_server
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  PROPERTIES LABELS "lsp;integration"
)
