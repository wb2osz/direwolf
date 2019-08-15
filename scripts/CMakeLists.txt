
if(NOT WIN32)
  install(PROGRAMS "${CUSTOM_SCRIPTS_DIR}/dwespeak.sh" DESTINATION bin)
  install(PROGRAMS "${CUSTOM_SCRIPTS_DIR}/dw-start.sh" DESTINATION share/doc/${CMAKE_PROJECT_NAME}/examples)

  add_custom_target(install-conf-scripts
    COMMAND ${CMAKE_COMMAND} -E copy "${CUSTOM_SCRIPTS_DIR}/dw-start.sh" ~
    )

  add_subdirectory(telemetry-toolkit)
endif()
