add_library(runtime SHARED src/stub.c src/dl.c)
target_include_directories(runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(runtime PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(runtime PROPERTIES OUTPUT_NAME runtime PREFIX "lib")

add_library(runtime_static STATIC src/stub.c src/dl.c)
target_include_directories(runtime_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(runtime_static PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(runtime_static PROPERTIES OUTPUT_NAME runtime PREFIX "lib")
