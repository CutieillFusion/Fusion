find_package(Threads REQUIRED)

# Core library: server, analysis, index, features. No main, no transport (so tests can link without I/O).
add_library(fusion_lsp_core STATIC
  src/server.cpp
  src/analysis.cpp
  src/index.cpp
  src/features.cpp
)

target_include_directories(fusion_lsp_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/compiler/src
)

target_link_libraries(fusion_lsp_core PUBLIC
  fusion_frontend
  nlohmann_json::nlohmann_json
  Threads::Threads
)

# Executable: main + transport + core. Production uses real transport.
add_executable(fusion_lsp
  src/main.cpp
  src/transport.cpp
)

target_link_libraries(fusion_lsp PRIVATE
  fusion_lsp_core
  ${CMAKE_DL_LIBS}
)

# Put the binary next to the main fusion compiler in the build tree.
set_target_properties(fusion_lsp PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# LSP integration tests (link core + GTest only)
add_subdirectory(tests)
