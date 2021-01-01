# - Try to find libgpiod
# Once done this will define
#  GPIOD_FOUND - System has libgpiod
#  GPIOD_INCLUDE_DIRS - The libgpiod include directories
#  GPIOD_LIBRARIES - The libraries needed to use libgpiod
#  GPIOD_DEFINITIONS - Compiler switches required for using libgpiod

find_package(PkgConfig)
pkg_check_modules(PC_GPIOD QUIET gpiod)

find_path(GPIOD_INCLUDE_DIR gpiod.h)
find_library(GPIOD_LIBRARY NAMES gpiod)

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set GPIOD_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(gpiod  DEFAULT_MSG
                                  GPIOD_LIBRARY GPIOD_INCLUDE_DIR)

mark_as_advanced(GPIOD_INCLUDE_DIR GPIOD_LIBRARY)

set(GPIOD_LIBRARIES ${GPIOD_LIBRARY})
set(GPIOD_INCLUDE_DIRS ${GPIOD_INCLUDE_DIR})
