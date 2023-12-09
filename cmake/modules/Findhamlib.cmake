# - Try to find Hamlib
#
# HAMLIB_FOUND - system has Hamlib
# HAMLIB_LIBRARIES - location of the library for hamlib
# HAMLIB_INCLUDE_DIRS - location of the include files for hamlib
#
# Requires these CMake modules:
#  FindPackageHandleStandardArgs (known included with CMake >=2.6.2)
#
# Original Author:
# 2019 Davide Gerhard <rainbow@irh.it>

set(HAMLIB_ROOT_DIR
  "${HAMLIB_ROOT_DIR}"
  CACHE
  PATH
  "Directory to search for hamlib")

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_HAMLIB hamlib)
endif()

find_path(HAMLIB_INCLUDE_DIR
  NAMES hamlib/rig.h
  PATHS
  /usr/include
  /usr/local/include
  /opt/local/include
  HINTS
  ${PC_HAMLIB_INCLUDEDIR}
  ${HAMLIB_ROOT_DIR}
  )

find_library(HAMLIB_LIBRARY
  NAMES hamlib
  PATHS
  /usr/lib64/hamlib
  /usr/lib/hamlib
  /usr/lib64
  /usr/lib
  /usr/local/lib64/hamlib
  /usr/local/lib/hamlib
  /usr/local/lib64
  /usr/local/lib
  /opt/local/lib
  /opt/local/lib/hamlib
  HINTS
  ${PC_HAMLIB_LIBDIR}
  ${HAMLIB_ROOT_DIR}

  )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hamlib
  DEFAULT_MSG
  HAMLIB_LIBRARY
  HAMLIB_INCLUDE_DIR
  )

if(HAMLIB_FOUND)
  list(APPEND HAMLIB_LIBRARIES ${HAMLIB_LIBRARY})
  list(APPEND HAMLIB_INCLUDE_DIRS ${HAMLIB_INCLUDE_DIR})
  mark_as_advanced(HAMLIB_ROOT_DIR)
endif()

mark_as_advanced(HAMLIB_INCLUDE_DIR HAMLIB_LIBRARY)
