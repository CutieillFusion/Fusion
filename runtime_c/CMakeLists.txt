include("${CMAKE_SOURCE_DIR}/cmake/FindFusionLibFFI.cmake")

set(RUNTIME_FFI_SOURCES src/ffi_stub.c)
if(FUSION_LIBFFI_FOUND)
  set(RUNTIME_FFI_SOURCES src/ffi.c)
endif()

include("${CMAKE_SOURCE_DIR}/cmake/FusionRuntime.cmake")

add_library(runtime SHARED src/stub.c src/dl.c ${RUNTIME_FFI_SOURCES})
target_include_directories(runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
fusion_runtime_libffi(runtime)
target_link_libraries(runtime PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(runtime PROPERTIES OUTPUT_NAME runtime PREFIX "lib")

add_library(runtime_static STATIC src/stub.c src/dl.c ${RUNTIME_FFI_SOURCES})
target_include_directories(runtime_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
fusion_runtime_libffi(runtime_static)
target_link_libraries(runtime_static PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(runtime_static PROPERTIES OUTPUT_NAME runtime PREFIX "lib")
