set(HIDAPI_LIBRARIES "" CACHE INTERNAL "")

if(WIN32 OR CYGWIN) # windows

  set(HIDAPI_LIBRARIES hidapi CACHE INTERNAL "hidapi")

  list(APPEND hidapi_SOURCES
    # Functions for accessing HID devices on Windows.
    # These were copied from https://github.com/libusb/hidapi
    ${CUSTOM_HIDAPI_DIR}/hid.c
    )

  add_library(hidapi STATIC
    ${hidapi_SOURCES}
    )

  set_target_properties(hidapi
    PROPERTIES COMPILE_FLAGS "-Dbool=int -Dtrue=1 -Dfalse=0 -DUSE_HIDAPI_STATIC"
    )

endif()
