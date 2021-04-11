set(REGEX_LIBRARIES "" CACHE INTERNAL "")

if(WIN32 OR CYGWIN) # windows

  set(REGEX_LIBRARIES regex CACHE INTERNAL "regex")

  list(APPEND regex_SOURCES
    # When building for Linux, we use regular expression
    # functions supplied by the gnu C library.
    # For the native WIN32 version, we need to use our own copy.
    # These were copied from http://gnuwin32.sourceforge.net/packages/regex.htm
    # Consider upgrading from https://www.gnu.org/software/libc/sources.html
    ${CUSTOM_REGEX_DIR}/regex.c
    )

  add_library(regex STATIC
    ${regex_SOURCES}
    )

  set_target_properties(regex
    PROPERTIES COMPILE_FLAGS "-Dbool=int -Dtrue=1 -Dfalse=0 -DREGEX_STATIC"
    )

endif()
