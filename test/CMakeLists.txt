# This is a trick to avoid more complication
# because configure_file() is done at configuration time

set(CUSTOM_TEST_BINARY_DIR "${CMAKE_BINARY_DIR}/test")
set(GEN_PACKETS_BIN "${CMAKE_BINARY_DIR}/src/gen_packets${CMAKE_EXECUTABLE_SUFFIX}")
set(ATEST_BIN "${CMAKE_BINARY_DIR}/src/atest${CMAKE_EXECUTABLE_SUFFIX}")
set(FXSEND_BIN "${CMAKE_BINARY_DIR}/test/fxsend${CMAKE_EXECUTABLE_SUFFIX}")
set(FXREC_BIN "${CMAKE_BINARY_DIR}/test/fxrec${CMAKE_EXECUTABLE_SUFFIX}")
set(IL2P_TEST_BIN "${CMAKE_BINARY_DIR}/test/il2p_test${CMAKE_EXECUTABLE_SUFFIX}")

if(WIN32)
  set(CUSTOM_SCRIPT_SUFFIX ".bat")
else()
  set(CUSTOM_SCRIPT_SUFFIX "")
endif()

set(TEST_CHECK-FX25_FILE "check-fx25")
set(TEST_CHECK-IL2P_FILE "check-il2p")
set(TEST_CHECK-MODEM1200_FILE "check-modem1200")
set(TEST_CHECK-MODEM1200_IL2P_FILE "check-modem1200-i")
set(TEST_CHECK-MODEM300_FILE "check-modem300")
set(TEST_CHECK-MODEM9600_FILE "check-modem9600")
set(TEST_CHECK-MODEM9600_IL2P_FILE "check-modem9600-i")
set(TEST_CHECK-MODEM19200_FILE "check-modem19200")
set(TEST_CHECK-MODEM2400-a_FILE "check-modem2400-a")
set(TEST_CHECK-MODEM2400-b_FILE "check-modem2400-b")
set(TEST_CHECK-MODEM2400-g_FILE "check-modem2400-g")
set(TEST_CHECK-MODEM4800_FILE "check-modem4800")
set(TEST_CHECK-MODEMEAS_FILE "check-modemeas")

# generate the scripts that run the tests

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-FX25_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-FX25_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-IL2P_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-IL2P_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM1200_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM1200_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM1200_IL2P_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM1200_IL2P_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM300_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM300_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM9600_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM9600_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM9600_IL2P_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM9600_IL2P_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM19200_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM19200_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM2400-a_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM2400-a_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM2400-b_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM2400-b_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM2400-g_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM2400-g_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM4800_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM4800_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEMEAS_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEMEAS_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )


# global includes
# not ideal but not so slow
# otherwise use target_include_directories
include_directories(
  ${CUSTOM_SRC_DIR}
  ${GPSD_INCLUDE_DIRS}
  ${HAMLIB_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${UDEV_INCLUDE_DIRS}
  ${PORTAUDIO_INCLUDE_DIRS}
  ${CUSTOM_GEOTRANZ_DIR}
  ${CMAKE_BINARY_DIR}/src
  )

if(WIN32 OR CYGWIN)
  include_directories(
    ${CUSTOM_REGEX_DIR}
  )
endif()

if(WIN32 OR CYGWIN)
  list(REMOVE_ITEM atest9_SOURCES
    ${CUSTOM_SRC_DIR}/dwgpsd.c
    )
endif()


# Unit test for inner digipeater algorithm
list(APPEND dtest_SOURCES
  ${CUSTOM_SRC_DIR}/digipeater.c
  ${CUSTOM_SRC_DIR}/ais.c
  ${CUSTOM_SRC_DIR}/dedupe.c
  ${CUSTOM_SRC_DIR}/pfilter.c
  ${CUSTOM_SRC_DIR}/ax25_pad.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/tq.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  ${CUSTOM_SRC_DIR}/decode_aprs.c
  ${CUSTOM_SRC_DIR}/deviceid.c
  ${CUSTOM_SRC_DIR}/dwgpsnmea.c
  ${CUSTOM_SRC_DIR}/dwgps.c
  ${CUSTOM_SRC_DIR}/dwgpsd.c
  ${CUSTOM_SRC_DIR}/serial_port.c
  ${CUSTOM_SRC_DIR}/latlong.c
  ${CUSTOM_SRC_DIR}/telemetry.c
  ${CUSTOM_SRC_DIR}/symbols.c
  ${CUSTOM_SRC_DIR}/tt_text.c
  )

if(WIN32 OR CYGWIN)
  list(REMOVE_ITEM dtest_SOURCES
    ${CUSTOM_SRC_DIR}/dwgpsd.c
    )
endif()

add_executable(dtest
  ${dtest_SOURCES}
  )

set_target_properties(dtest
  PROPERTIES COMPILE_FLAGS "-DDIGITEST -DUSE_REGEX_STATIC"
  )

target_link_libraries(dtest
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  ${GPSD_LIBRARIES}
  Threads::Threads
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(dtest ws2_32)
endif()


# Unit test for APRStt tone sequence parsing.
list(APPEND ttest_SOURCES
  ${CUSTOM_SRC_DIR}/aprs_tt.c
  ${CUSTOM_SRC_DIR}/tt_text.c
  ${CUSTOM_SRC_DIR}/latlong.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(ttest
  ${ttest_SOURCES}
  )

set_target_properties(ttest
  PROPERTIES COMPILE_FLAGS "-DTT_MAIN"
  )

target_link_libraries(ttest
  ${MISC_LIBRARIES}
  ${GEOTRANZ_LIBRARIES}
  )


# Unit test for APRStt tone sequence / text conversions.
list(APPEND tttexttest_SOURCES
  ${CUSTOM_SRC_DIR}/tt_text.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(tttexttest
  ${tttexttest_SOURCES}
  )

set_target_properties(tttexttest
  PROPERTIES COMPILE_FLAGS "-DTTT_TEST"
  )

target_link_libraries(tttexttest
  ${MISC_LIBRARIES}
  )


# Unit test for Packet Filtering.
list(APPEND pftest_SOURCES
  ${CUSTOM_SRC_DIR}/pfilter.c
  ${CUSTOM_SRC_DIR}/ais.c
  ${CUSTOM_SRC_DIR}/ax25_pad.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/decode_aprs.c
  ${CUSTOM_SRC_DIR}/deviceid.c
  ${CUSTOM_SRC_DIR}/dwgpsnmea.c
  ${CUSTOM_SRC_DIR}/dwgps.c
  ${CUSTOM_SRC_DIR}/dwgpsd.c
  ${CUSTOM_SRC_DIR}/serial_port.c
  ${CUSTOM_SRC_DIR}/latlong.c
  ${CUSTOM_SRC_DIR}/symbols.c
  ${CUSTOM_SRC_DIR}/telemetry.c
  ${CUSTOM_SRC_DIR}/tt_text.c
  )

if(WIN32 OR CYGWIN)
  list(REMOVE_ITEM pftest_SOURCES
    ${CUSTOM_SRC_DIR}/dwgpsd.c
    )
endif()

add_executable(pftest
  ${pftest_SOURCES}
  )

set_target_properties(pftest
  PROPERTIES COMPILE_FLAGS "-DPFTEST -DUSE_REGEX_STATIC"
  )

target_link_libraries(pftest
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  ${GPSD_LIBRARIES}
  Threads::Threads
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(pftest ws2_32)
endif()

# Unit test for telemetry decoding.
list(APPEND tlmtest_SOURCES
  ${CUSTOM_SRC_DIR}/telemetry.c
  ${CUSTOM_SRC_DIR}/ax25_pad.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

if(WIN32 OR CYGWIN)
  list(REMOVE_ITEM tlmtest_SOURCES
    ${CUSTOM_SRC_DIR}/dwgpsd.c
    )
endif()

add_executable(tlmtest
  ${tlmtest_SOURCES}
  )

set_target_properties(tlmtest
  PROPERTIES COMPILE_FLAGS "-DTEST -DUSE_REGEX_STATIC"
  )

target_link_libraries(tlmtest
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(tlmtest ws2_32)
endif()


# Unit test for location coordinate conversion.
list(APPEND lltest_SOURCES
  ${CUSTOM_SRC_DIR}/latlong.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(lltest
  ${lltest_SOURCES}
  )

set_target_properties(lltest
  PROPERTIES COMPILE_FLAGS "-DLLTEST"
  )

target_link_libraries(lltest
  ${MISC_LIBRARIES}
  )


# Unit test for encoding position & object report.
list(APPEND enctest_SOURCES
  ${CUSTOM_SRC_DIR}/encode_aprs.c
  ${CUSTOM_SRC_DIR}/latlong.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(enctest
  ${enctest_SOURCES}
  )

set_target_properties(enctest
  PROPERTIES COMPILE_FLAGS "-DEN_MAIN"
  )

target_link_libraries(enctest
  ${MISC_LIBRARIES}
  )


# Unit test for KISS encapsulation.
list(APPEND kisstest_SOURCES
  ${CUSTOM_SRC_DIR}/kiss_frame.c
  )

add_executable(kisstest
  ${kisstest_SOURCES}
  )

set_target_properties(kisstest
  PROPERTIES COMPILE_FLAGS "-DKISSTEST"
  )


# Unit test for constructing frames besides UI.
list(APPEND pad2test_SOURCES
  ${CUSTOM_SRC_DIR}/ax25_pad2.c
  ${CUSTOM_SRC_DIR}/ax25_pad.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(pad2test
  ${pad2test_SOURCES}
  )

set_target_properties(pad2test
  PROPERTIES COMPILE_FLAGS "-DPAD2TEST -DUSE_REGEX_STATIC"
  )

target_link_libraries(pad2test
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(pad2test ws2_32)
endif()


# Unit Test for XID frame encode/decode.
list(APPEND xidtest_SOURCES
  ${CUSTOM_SRC_DIR}/xid.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(xidtest
  ${xidtest_SOURCES}
  )

set_target_properties(xidtest
  PROPERTIES COMPILE_FLAGS "-DXIDTEST"
  )

target_link_libraries(xidtest
  ${MISC_LIBRARIES}
  )


# Unit Test for DTMF encode/decode.
list(APPEND dtmftest_SOURCES
  ${CUSTOM_SRC_DIR}/dtmf.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(dtmftest
  ${dtmftest_SOURCES}
  )

set_target_properties(dtmftest
  PROPERTIES COMPILE_FLAGS "-DDTMF_TEST"
  )

# Unit Test FX.25 algorithm.

list(APPEND fxsend_SOURCES
  ${CUSTOM_SRC_DIR}/fx25_send.c
  ${CUSTOM_SRC_DIR}/fx25_encode.c
  ${CUSTOM_SRC_DIR}/fx25_init.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(fxsend
  ${fxsend_SOURCES}
  )

set_target_properties(fxsend
  PROPERTIES COMPILE_FLAGS "-DFXTEST"
  )

list(APPEND fxrec_SOURCES
  ${CUSTOM_SRC_DIR}/fx25_rec.c
  ${CUSTOM_SRC_DIR}/fx25_extract.c
  ${CUSTOM_SRC_DIR}/fx25_init.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(fxrec
  ${fxrec_SOURCES}
  )

set_target_properties(fxrec
  PROPERTIES COMPILE_FLAGS "-DFXTEST"
  )


# Unit Test IL2P with out modems.

list(APPEND il2p_test_SOURCES
  ${CUSTOM_SRC_DIR}/il2p_test.c
  ${CUSTOM_SRC_DIR}/il2p_init.c
  ${CUSTOM_SRC_DIR}/il2p_rec.c
  ${CUSTOM_SRC_DIR}/il2p_send.c
  ${CUSTOM_SRC_DIR}/il2p_codec.c
  ${CUSTOM_SRC_DIR}/il2p_payload.c
  ${CUSTOM_SRC_DIR}/il2p_header.c
  ${CUSTOM_SRC_DIR}/il2p_scramble.c
  ${CUSTOM_SRC_DIR}/ax25_pad.c
  ${CUSTOM_SRC_DIR}/ax25_pad2.c
  ${CUSTOM_SRC_DIR}/fx25_encode.c
  ${CUSTOM_SRC_DIR}/fx25_extract.c
  ${CUSTOM_SRC_DIR}/fx25_init.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(il2p_test
  ${il2p_test_SOURCES}
  )

#FIXME - remove if not needed.
#set_target_properties(il2p_test
#  PROPERTIES COMPILE_FLAGS "-DXXXXX"
#  )

target_link_libraries(il2p_test
  ${MISC_LIBRARIES}
  )

# doing ctest on previous programs

add_test(dtest dtest)
add_test(ttest ttest)
add_test(tttexttest tttexttest)
add_test(pftest pftest)
add_test(tlmtest tlmtest)
add_test(lltest lltest)
add_test(enctest enctest)
add_test(kisstest kisstest)
add_test(pad2test pad2test)
add_test(xidtest xidtest)
add_test(dtmftest dtmftest)

add_test(check-fx25 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-FX25_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-il2p "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-IL2P_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem1200 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM1200_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem1200-i "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM1200_IL2P_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem300 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM300_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem9600 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM9600_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem9600-i "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM9600_IL2P_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem19200 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM19200_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem2400-a "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM2400-a_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem2400-b "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM2400-b_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem2400-g "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM2400-g_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem4800 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM4800_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modemeas "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEMEAS_FILE}${CUSTOM_SCRIPT_SUFFIX}")



#  -----------------------------  Manual tests and experiments  ---------------------------
if(OPTIONAL_TEST)

  # Unit test for IGate
  list(APPEND itest_SOURCES
    ${CUSTOM_SRC_DIR}/igate.c
    ${CUSTOM_SRC_DIR}/ais.c
    ${CUSTOM_SRC_DIR}/ax25_pad.c
    ${CUSTOM_SRC_DIR}/fcs_calc.c
    ${CUSTOM_SRC_DIR}/mheard.c
    ${CUSTOM_SRC_DIR}/pfilter.c
    ${CUSTOM_SRC_DIR}/telemetry.c
    ${CUSTOM_SRC_DIR}/decode_aprs.c
    ${CUSTOM_SRC_DIR}/deviceid.c
    ${CUSTOM_SRC_DIR}/dwgpsnmea.c
    ${CUSTOM_SRC_DIR}/dwgps.c
    ${CUSTOM_SRC_DIR}/dwgpsd.c
    ${CUSTOM_SRC_DIR}/serial_port.c
    ${CUSTOM_SRC_DIR}/textcolor.c
    ${CUSTOM_SRC_DIR}/dtime_now.c
    ${CUSTOM_SRC_DIR}/latlong.c
    ${CUSTOM_SRC_DIR}/tt_text.c
    ${CUSTOM_SRC_DIR}/symbols.c
    )

  if(WIN32 OR CYGWIN)
    list(REMOVE_ITEM itest_SOURCES
      ${CUSTOM_SRC_DIR}/dwgpsd.c
      )
  endif()

  add_executable(itest
    ${itest_SOURCES}
    )

  set_target_properties(itest
    PROPERTIES COMPILE_FLAGS "-DITEST"
    )

  target_link_libraries(itest
    ${MISC_LIBRARIES}
    ${GPSD_LIBRARIES}
    Threads::Threads
    )

  if(WIN32 OR CYGWIN)
    target_link_libraries(itest ws2_32)
  endif()


  # For demodulator tweaking experiments.
  list(APPEND testagc_SOURCES
    ${CUSTOM_SRC_DIR}/atest.c
    ${CUSTOM_SRC_DIR}/ais.c
    ${CUSTOM_SRC_DIR}/demod.c
    ${CUSTOM_SRC_DIR}/dsp.c
    ${CUSTOM_SRC_DIR}/demod_afsk.c
    ${CUSTOM_SRC_DIR}/demod_psk.c
    ${CUSTOM_SRC_DIR}/demod_9600.c
    ${CUSTOM_SRC_DIR}/hdlc_rec.c
    ${CUSTOM_SRC_DIR}/hdlc_rec2.c
    ${CUSTOM_SRC_DIR}/multi_modem.c
    ${CUSTOM_SRC_DIR}/rrbb.c
    ${CUSTOM_SRC_DIR}/fcs_calc.c
    ${CUSTOM_SRC_DIR}/ax25_pad.c
    ${CUSTOM_SRC_DIR}/decode_aprs.c
    ${CUSTOM_SRC_DIR}/deviceid.c
    ${CUSTOM_SRC_DIR}/dwgpsnmea.c
    ${CUSTOM_SRC_DIR}/dwgps.c
    ${CUSTOM_SRC_DIR}/dwgpsd.c
    ${CUSTOM_SRC_DIR}/serial_port.c
    ${CUSTOM_SRC_DIR}/telemetry.c
    ${CUSTOM_SRC_DIR}/dtime_now.c
    ${CUSTOM_SRC_DIR}/latlong.c
    ${CUSTOM_SRC_DIR}/tt_text.c
    ${CUSTOM_SRC_DIR}/symbols.c
    ${CUSTOM_SRC_DIR}/textcolor.c
    )

  if(WIN32 OR CYGWIN)
    list(REMOVE_ITEM testagc_SOURCES
      ${CUSTOM_SRC_DIR}/dwgpsd.c
      )
  endif()

  add_executable(testagc
    ${testagc_SOURCES}
    )

  target_link_libraries(testagc
    ${MISC_LIBRARIES}
    ${GPSD_LIBRARIES}
    Threads::Threads
    )

  if(WIN32 OR CYGWIN)
    target_link_libraries(testagc ws2_32)
  endif()


  # Send GPS location to KISS TNC each second.
  list(APPEND walk96_SOURCES
    ${CUSTOM_SRC_DIR}/walk96.c
    ${CUSTOM_SRC_DIR}/ais.c
    ${CUSTOM_SRC_DIR}/dwgps.c
    ${CUSTOM_SRC_DIR}/dwgpsnmea.c
    ${CUSTOM_SRC_DIR}/dwgpsd.c
    ${CUSTOM_SRC_DIR}/kiss_frame.c
    ${CUSTOM_SRC_DIR}/latlong.c
    ${CUSTOM_SRC_DIR}/encode_aprs.c
    ${CUSTOM_SRC_DIR}/serial_port.c
    ${CUSTOM_SRC_DIR}/textcolor.c
    ${CUSTOM_SRC_DIR}/ax25_pad.c
    ${CUSTOM_SRC_DIR}/fcs_calc.c
    ${CUSTOM_SRC_DIR}/xmit.c
    ${CUSTOM_SRC_DIR}/xid.c
    ${CUSTOM_SRC_DIR}/hdlc_send.c
    ${CUSTOM_SRC_DIR}/gen_tone.c
    ${CUSTOM_SRC_DIR}/ptt.c
    ${CUSTOM_SRC_DIR}/tq.c
    ${CUSTOM_SRC_DIR}/hdlc_rec.c
    ${CUSTOM_SRC_DIR}/hdlc_rec2.c
    ${CUSTOM_SRC_DIR}/rrbb.c
    ${CUSTOM_SRC_DIR}/dsp.c
    ${CUSTOM_SRC_DIR}/multi_modem.c
    ${CUSTOM_SRC_DIR}/demod.c
    ${CUSTOM_SRC_DIR}/demod_afsk.c
    ${CUSTOM_SRC_DIR}/demod_psk.c
    ${CUSTOM_SRC_DIR}/demod_9600.c
    ${CUSTOM_SRC_DIR}/server.c
    ${CUSTOM_SRC_DIR}/morse.c
    ${CUSTOM_SRC_DIR}/dtmf.c
    ${CUSTOM_SRC_DIR}/audio_stats.c
    ${CUSTOM_SRC_DIR}/dtime_now.c
    ${CUSTOM_SRC_DIR}/dlq.c
    )

  if(LINUX)
    list(APPEND walk96_SOURCES
      ${CUSTOM_SRC_DIR}/audio.c
      )
    if(UDEV_FOUND)
      list(APPEND walk96_SOURCES
        ${CUSTOM_SRC_DIR}/cm108.c
        )
    endif()
  elseif(WIN32 OR CYGWIN) # windows
    list(APPEND walk96_SOURCES
      ${CUSTOM_SRC_DIR}/audio_win.c
      )
    list(REMOVE_ITEM walk96_SOURCES
      ${CUSTOM_SRC_DIR}/dwgpsd.c
      )
  else() # macOS freebsd openbsd
    list(APPEND walk96_SOURCES
      ${CUSTOM_SRC_DIR}/audio_portaudio.c
      )
  endif()

  add_executable(walk96
    ${walk96_SOURCES}
    )

  set_target_properties(walk96
    PROPERTIES COMPILE_FLAGS "-DWALK96 -DUSE_REGEX_STATIC"
    )

  target_link_libraries(walk96
    ${MISC_LIBRARIES}
    ${REGEX_LIBRARIES}
    ${GPSD_LIBRARIES}
    ${HAMLIB_LIBRARIES}
    ${ALSA_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    ${UDEV_LIBRARIES}
    Threads::Threads
    )

  if(WIN32 OR CYGWIN)
    target_link_libraries(walk96 ws2_32)
  endif()


  # TODO  miss the audio file

  # testagc
  # ./atest -P H+ -F 0 ../01_Track_1.wav ../02_Track_2.wav | grep "packets decoded in" >atest.out

  # testagc3
  # ./gen_packets -B 300 -n 100 -o noisy3.wav
  # ./atest3 -B 300 -P D -D 3 noisy3.wav | grep "packets decoded in" >atest.out

  # testagc96
  # ./gen_packets -B 9600 -n 100 -o noisy96.wav
  # ./atest96 -B 9600 ../walkabout9600c.wav noisy96.wav zzz16.wav zzz16.wav zzz16.wav zzz8.wav zzz8.wav zzz8.wav | grep "packets decoded in" >atest.out

  # testagc24
  # ./atest24 -B 2400 test2400.wav | grep "packets decoded in" >atest.out

  # testagc24mfj
  # ./atest24mfj -F 1 -B 2400 ../ref-doc/MFJ-2400-PSK/2k4_short.wav

  # testagc48
  # ./atest48 -B 4800 test4800.wav | grep "packets decoded in" >atest.out
endif()  # OPTIONAL_TEST
