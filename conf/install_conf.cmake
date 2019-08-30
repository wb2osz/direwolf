if(NOT EXISTS $ENV{HOME}/direwolf.conf)
  configure_file("${CUSTOM_BINARY_DIR}/direwolf.conf" $ENV{HOME})
endif()

if(NOT EXISTS $ENV{HOME}/sdr.conf)
  configure_file("${CUSTOM_CONF_DIR}/sdr.conf" $ENV{HOME})
endif()

if(NOT EXISTS $ENV{HOME}/dw-start.sh)
  configure_file("${CUSTOM_SCRIPTS_DIR}/dw-start.sh" $ENV{HOME})
endif()

if(NOT EXISTS $ENV{HOME}/telem-m0xer-3.txt)
  configure_file("${CUSTOM_TELEMETRY_DIR}/telem-m0xer-3.txt" $ENV{HOME})
endif()

if(NOT EXISTS $ENV{HOME}/telem-balloon.conf)
  configure_file("${CUSTOM_TELEMETRY_DIR}/telem-balloon.conf" $ENV{HOME})
endif()

if(NOT EXISTS $ENV{HOME}/telem-volts.conf)
  configure_file("${CUSTOM_TELEMETRY_DIR}/telem-volts.conf" $ENV{HOME})
endif()
