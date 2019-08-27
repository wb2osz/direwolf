if(NOT EXISTS ~/direwolf.conf)
  install(FILES "${CMAKE_BINARY_DIR}/direwolf.conf" DESTINATION ~)
endif()

if(NOT EXISTS ~/sdr.conf)
  install(FILES "${CUSTOM_CONF_DIR}/sdr.conf" DESTINATION ~)
endif()

if(NOT EXISTS ~/dw-start.sh)
  install(FILES "${CUSTOM_SCRIPTS_DIR}/dw-start.sh" DESTINATION ~)
endif()

if(NOT EXISTS ~/telem-m0xer-3.txt)
  install(FILES "${CUSTOM_TELEMETRY_DIR}/telem-m0xer-3.txt" DESTINATION ~)
endif()

if(NOT EXISTS ~/telem-balloon.conf)
  install(FILES "${CUSTOM_TELEMETRY_DIR}/telem-balloon.conf" DESTINATION ~)
endif()

if(NOT EXISTS ~/telem-volts.conf)
  install(FILES "${CUSTOM_TELEMETRY_DIR}/telem-volts.conf" DESTINATION ~)
endif()
