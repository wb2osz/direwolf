# - Try to find hidapi
#
#  HIDAPI_FOUND - system has hidapi
#  HIDAPI_LIBRARIES - location of the library for hidapi
#  HIDAPI_INCLUDE_DIRS - location of the include files for hidapi

set(HIDAPI_ROOT_DIR
  "${HIDAPI_ROOT_DIR}"
  CACHE
  PATH
  "Directory to search for hidapi")

# no need to check pkg-config

find_path(HIDAPI_INCLUDE_DIRS
  NAMES
  hidapi.h
  PATHS
  /usr/local/include
  /usr/include
  /opt/local/include
  HINTS
  ${HIDAPI_ROOT_DIR}
  PATH_SUFFIXES
  hidapi
  )

find_library(HIDAPI_LIBRARIES
  NAMES
  hidapi
  PATHS
  /usr/local/lib
  /usr/lib
  /usr/lib64
  /opt/local/lib
  HINTS
  ${HIDAPI_ROOT_DIR}
  )


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HIDAPI DEFAULT_MSG HIDAPI_INCLUDE_DIRS HIDAPI_LIBRARIES)

mark_as_advanced(HIDAPI_INCLUDE_DIRS HIDAPI_LIBRARIES)
