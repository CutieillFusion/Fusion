add_executable(test_runner test_runner.c)
target_link_libraries(test_runner PRIVATE runtime)
add_test(NAME test_runner COMMAND test_runner)

add_test(NAME fusion_help COMMAND $<TARGET_FILE:fusion> --help)
set_tests_properties(fusion_help PROPERTIES
  REQUIRED_FILES "$<TARGET_FILE:fusion>"
)
