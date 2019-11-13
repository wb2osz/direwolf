
if(NOT (WIN32 OR CYGWIN))
  install(PROGRAMS "${CUSTOM_SCRIPTS_DIR}/dwespeak.sh" DESTINATION ${INSTALL_BIN_DIR})
  install(PROGRAMS "${CUSTOM_SCRIPTS_DIR}/dw-start.sh" DESTINATION ${INSTALL_SCRIPTS_DIR})
  add_subdirectory(telemetry-toolkit)
endif()
