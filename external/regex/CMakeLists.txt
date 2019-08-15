set(REGEX_LIBRARIES "" CACHE INTERNAL "")

if(WIN32) # windows

  set(REGEX_LIBRARIES regex CACHE INTERNAL "regex")

  set(regex_SOURCES
    ${regex_SOURCES}
    # When building for Linux, we use regular expression
    # functions supplied by the gnu C library.
    # For the native WIN32 version, we need to use our own copy.
    # These were copied from http://gnuwin32.sourceforge.net/packages/regex.htm
    # Consider upgrading from https://www.gnu.org/software/libc/sources.html
    #
    # check if needed: -Dbool=int -Dtrue=1 -Dfalse=0
    ${CUSTOM_REGEX_DIR}/regex.c
    )
  ADD_LIBRARY(regex STATIC
    ${regex_SOURCES}
    )
endif()
