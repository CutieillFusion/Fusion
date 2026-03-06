# FindFusionCURL.cmake
# Find CURL (required for HTTP builtins). If not found and FUSION_FETCH_CURL is ON,
# download and build libcurl in the build tree (no sudo required).
# Sets FUSION_CURL_FOUND and FUSION_CURL_TARGET (CURL::libcurl or libcurl when fetched).

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

# 3) Fetch and build curl in build tree (no sudo)
if(FUSION_FETCH_CURL)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build curl static" FORCE)
  set(BUILD_CURL_EXE OFF CACHE BOOL "Skip curl binary" FORCE)
  set(HTTP_ONLY OFF CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "Required for linking static curl into shared libs" FORCE)

  include(FetchContent)
  FetchContent_Declare(curl
    URL https://github.com/curl/curl/releases/download/curl-8_10_0/curl-8.10.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(curl)

  if(TARGET libcurl_static)
    set(FUSION_CURL_FOUND TRUE)
    set(FUSION_CURL_TARGET "libcurl_static")
    set(FUSION_CURL_INCLUDE_DIR "${curl_SOURCE_DIR}/include" PARENT_SCOPE)
    set(FUSION_CURL_FOUND TRUE PARENT_SCOPE)
    set(FUSION_CURL_TARGET "libcurl_static" PARENT_SCOPE)
    message(STATUS "CURL fetched and built in build tree (no system install needed)")
    return()
  endif()
endif()

set(FUSION_CURL_FOUND FALSE PARENT_SCOPE)
set(FUSION_CURL_TARGET "" PARENT_SCOPE)
