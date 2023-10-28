# - Try to find sndio
#
#  SNDIO_FOUND - system has sndio
#  SNDIO_LIBRARIES - location of the library for sndio
#  SNDIO_INCLUDE_DIRS - location of the include files for sndio

set(SNDIO_ROOT_DIR
  "${SNDIO_ROOT_DIR}"
  CACHE
  PATH
  "Directory to search for sndio")

# no need to check pkg-config

find_path(SNDIO_INCLUDE_DIRS
  NAMES
  sndio.h
  PATHS
  /usr/local/include
  /usr/include
  /opt/local/include
  HINTS
  ${SNDIO_ROOT_DIR}
  )

find_library(SNDIO_LIBRARIES
  NAMES
  sndio
  PATHS
  /usr/local/lib
  /usr/lib
  /usr/lib64
  /opt/local/lib
  HINTS
  ${SNDIIO_ROOT_DIR}
  )


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SNDIO DEFAULT_MSG SNDIO_INCLUDE_DIRS SNDIO_LIBRARIES)

mark_as_advanced(SNDIO_INCLUDE_DIRS SNDIO_LIBRARIES)
