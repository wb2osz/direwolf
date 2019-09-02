
if(NOT (WIN32 OR CYGWIN))
  install(PROGRAMS "${CUSTOM_SCRIPTS_DIR}/dwespeak.sh" DESTINATION bin)
  install(PROGRAMS "${CUSTOM_SCRIPTS_DIR}/dw-start.sh" DESTINATION share/doc/${CMAKE_PROJECT_NAME}/examples)
  add_subdirectory(telemetry-toolkit)
endif()
