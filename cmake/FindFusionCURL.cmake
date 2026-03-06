# FindFusionCURL.cmake
# Find CURL (required for HTTP builtins).
# Sets FUSION_CURL_FOUND and FUSION_CURL_TARGET.
# Install with: sudo apt install libcurl4-openssl-dev

set(FUSION_CURL_FOUND FALSE)
set(FUSION_CURL_TARGET "")

# 1) Try system find_package first
find_package(CURL QUIET)
if(CURL_FOUND)
  set(FUSION_CURL_FOUND TRUE)
  set(FUSION_CURL_TARGET "CURL::libcurl")
  set(FUSION_CURL_FOUND TRUE PARENT_SCOPE)
  set(FUSION_CURL_TARGET "CURL::libcurl" PARENT_SCOPE)
  return()
endif()

# 2) Try pkg-config (e.g. user install in ~/.local with PKG_CONFIG_PATH set)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_CURL QUIET libcurl)
  if(PC_CURL_FOUND)
    find_library(CURL_LIB NAMES curl libcurl PATHS ${PC_CURL_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_path(CURL_INC NAMES curl/curl.h PATHS ${PC_CURL_INCLUDE_DIRS} NO_DEFAULT_PATH)
    if(CURL_LIB AND CURL_INC)
      add_library(CURL::libcurl UNKNOWN IMPORTED)
      set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LOCATION "${CURL_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CURL_INC}"
        INTERFACE_LINK_LIBRARIES "${PC_CURL_LINK_LIBRARIES}")
      set(FUSION_CURL_FOUND TRUE)
      set(FUSION_CURL_TARGET "CURL::libcurl")
      set(FUSION_CURL_FOUND TRUE PARENT_SCOPE)
      set(FUSION_CURL_TARGET "CURL::libcurl" PARENT_SCOPE)
      return()
    endif()
  endif()
endif()

message(FATAL_ERROR "CURL not found. Install it with: sudo apt install libcurl4-openssl-dev")
