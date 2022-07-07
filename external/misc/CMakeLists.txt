
set(MISC_LIBRARIES misc CACHE INTERNAL "misc")

include_directories(
  ${CMAKE_SOURCE_DIR}/src
  )

if(LINUX)
# Previously -
#  list(APPEND misc_SOURCES
#    # Provide our own copy of strlcpy and strlcat
#    # because they are not included with Linux.
#    ${CUSTOM_MISC_DIR}/strlcpy.c
#    ${CUSTOM_MISC_DIR}/strlcat.c
#    )
# It seems that Alpine Linux and Void Linux have strlcpy and
# strlcat so we need to handle the situation more delicately.
# When doing it this way, there is probably no reason to
# distinguish between Linux and BSD-like systems here.
# If we kept going, the same thing could be done for each
# of the functions and no OS check would be needed.

  if (NOT HAVE_STRLCPY)
    list(APPEND misc_SOURCES
      ${CUSTOM_MISC_DIR}/strlcpy.c
      )
  endif()

  if (NOT HAVE_STRLCAT)
    list(APPEND misc_SOURCES
      ${CUSTOM_MISC_DIR}/strlcat.c
      )
  endif()

  add_library(misc STATIC
    ${misc_SOURCES}
    )

elseif(WIN32 OR CYGWIN) # windows

  list(APPEND misc_SOURCES
    # There are several string functions found in Linux
    # but not on Windows.  Need to provide our own copy.
    ${CUSTOM_MISC_DIR}/strsep.c
    ${CUSTOM_MISC_DIR}/strtok_r.c
    ${CUSTOM_MISC_DIR}/strcasestr.c
    ${CUSTOM_MISC_DIR}/strlcpy.c
    ${CUSTOM_MISC_DIR}/strlcat.c
    )

  add_library(misc STATIC
    ${misc_SOURCES}
    )

else()

  # on macOS, OpenBSD and FreeBSD not misc is necessary
  set(MISC_LIBRARIES "" CACHE INTERNAL "")

endif()
