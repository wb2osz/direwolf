
set(MISC_LIBRARIES misc CACHE INTERNAL "misc")

include_directories(
  ${CMAKE_SOURCE_DIR}/src
  )

if(LINUX)
  set(misc_SOURCES
    ${misc_SOURCES}
    # Provide our own copy of strlcpy and strlcat
    # because they are not included with Linux.
    ${CUSTOM_MISC_DIR}/strlcpy.c
    ${CUSTOM_MISC_DIR}/strlcat.c
    )
  ADD_LIBRARY(misc STATIC
    ${misc_SOURCES}
    )
elseif(WIN32) # windows
  set(misc_SOURCES
    ${misc_SOURCES}
    # There are several string functions found in Linux
    # but not on Windows.  Need to provide our own copy.
    ${CUSTOM_MISC_DIR}/strsep.c
    ${CUSTOM_MISC_DIR}/strtok_r.c
    ${CUSTOM_MISC_DIR}/strcasestr.c
    ${CUSTOM_MISC_DIR}/strlcpy.c
    ${CUSTOM_MISC_DIR}/strlcat.c
    )
  ADD_LIBRARY(misc STATIC
    ${misc_SOURCES}
    )
else()
  # on macOS, OpenBSD and FreeBSD not misc is necessary
  set(MISC_LIBRARIES "" CACHE INTERNAL "")
endif()
