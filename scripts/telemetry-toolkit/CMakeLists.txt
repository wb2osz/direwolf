add_custom_target(install-conf-telemetry
  COMMAND ${CMAKE_COMMAND} -E copy "${CUSTOM_TELEMETRY_DIR}/telem-m0xer-3.txt" ~
  COMMAND ${CMAKE_COMMAND} -E copy "${CUSTOM_TELEMETRY_DIR}/telem-balloon.conf" ~
  COMMAND ${CMAKE_COMMAND} -E copy "${CUSTOM_TELEMETRY_DIR}/telem-volts.conf" ~
  )

install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-balloon.pl" DESTINATION bin)
install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-bits.pl" DESTINATION bin)
install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-data.pl" DESTINATION bin)
install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-data91.pl" DESTINATION bin)
install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-eqns.pl" DESTINATION bin)
install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-parm.pl" DESTINATION bin)
install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-seq.sh" DESTINATION bin)
install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-unit.pl" DESTINATION bin)
install(PROGRAMS "${CUSTOM_TELEMETRY_DIR}/telem-volts.py" DESTINATION bin)

install(FILES "${CUSTOM_TELEMETRY_DIR}/telem-m0xer-3.txt" DESTINATION share/doc/${CMAKE_PROJECT_NAME}/examples)
install(FILES "${CUSTOM_TELEMETRY_DIR}/telem-balloon.conf" DESTINATION share/doc/${CMAKE_PROJECT_NAME}/examples)
install(FILES "${CUSTOM_TELEMETRY_DIR}/telem-volts.conf" DESTINATION share/doc/${CMAKE_PROJECT_NAME}/examples)
