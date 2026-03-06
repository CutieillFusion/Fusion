# PatchLLVMExportsMissingFiles.cmake
# Downgrade LLVMExports.cmake missing-file check from FATAL_ERROR to STATUS so prebuilt
# LLVM tarballs that omit MLIR/other tools (e.g. mlir-linalg-ods-yaml-gen) still work.
# Usage: cmake -D PATCH_FILE=/path/to/LLVMExports.cmake -P PatchLLVMExportsMissingFiles.cmake
if(NOT PATCH_FILE OR NOT EXISTS "${PATCH_FILE}")
  message(FATAL_ERROR "PATCH_FILE must point to existing LLVMExports.cmake")
endif()
file(READ "${PATCH_FILE}" content)
set(content_orig "${content}")
# Match exact content: LLVM writes \" for quotes inside the message string.
set(old "    if(NOT EXISTS \"\${_cmake_file}\")
      message(FATAL_ERROR \"The imported target \\\"\${_cmake_target}\\\" references the file
   \\\"\${_cmake_file}\\\"
but this file does not exist.  Possible reasons include:
* The file was deleted, renamed, or moved to another location.
* An install or uninstall procedure did not complete successfully.
* The installation package was faulty and contained
   \\\"\${CMAKE_CURRENT_LIST_FILE}\\\"
but not all the files it references.
\")
    endif()")
set(new "    if(NOT EXISTS \"\${_cmake_file}\")
      message(STATUS \"LLVM: skipping missing optional file for \${_cmake_target}\")
    endif()")
string(REPLACE "${old}" "${new}" content "${content_orig}")
if(content STREQUAL "${content_orig}")
  message(STATUS "LLVMExports.cmake: no patch needed or pattern not found")
  return()
endif()
file(WRITE "${PATCH_FILE}" "${content}")
message(STATUS "Patched ${PATCH_FILE} to allow missing optional LLVM/MLIR tool files")
