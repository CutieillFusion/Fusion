# FusionRuntime.cmake
# Call fusion_runtime_libffi(TARGET) to apply LibFFI includes, libraries, and dependencies to a runtime target.
# Requires FUSION_LIBFFI_FOUND and related LibFFI variables (from FindFusionLibFFI.cmake when used from runtime_c).

function(fusion_runtime_libffi TARGET)
  if(NOT FUSION_LIBFFI_FOUND)
    return()
  endif()
  if(FUSION_LIBFFI_FETCHED)
    target_include_directories(${TARGET} PRIVATE ${LibFFI_INCLUDE_DIR})
    target_link_libraries(${TARGET} PRIVATE LibFFI::LibFFI)
    add_dependencies(${TARGET} libffi)
  elseif(LibFFI_FOUND)
    target_include_directories(${TARGET} PRIVATE ${LibFFI_INCLUDE_DIRS})
    target_link_libraries(${TARGET} PRIVATE ${LibFFI_LIBRARIES})
  else()
    target_include_directories(${TARGET} PRIVATE ${LibFFI_INCLUDE_DIR})
    target_link_libraries(${TARGET} PRIVATE LibFFI::LibFFI)
  endif()
endfunction()
