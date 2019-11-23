#
# The destination field is often used to identify the manufacturer/model.
# These are not hardcoded into Dire Wolf.  Instead they are read from
# a file called tocalls.txt at application start up time.
#
# The original permanent symbols are built in but the "new" symbols,
# using overlays, are often updated.  These are also read from files.
#
# You can obtain an updated copy by typing "make data-update".
# This is not part of the normal build process.  You have to do this explicitly.
#
# The locations below appear to be the most recent.
# The copy at http://www.aprs.org/tocalls.txt is out of date.
#

include(ExternalProject)

set(TOCALLS_TXT "tocalls.txt")
set(TOCALLS_TXT_BKP "tocalls.txt.old")
set(TOCALLS_URL "http://www.aprs.org/aprs11/tocalls.txt")
set(SYMBOLS-NEW_TXT "symbols-new.txt")
set(SYMBOLS-NEW_TXT_BKP "symbols-new.txt.old")
set(SYMBOLS-NEW_URL "http://www.aprs.org/symbols/symbols-new.txt")
set(SYMBOLSX_TXT "symbolsX.txt")
set(SYMBOLSX_TXT_BKP "symbolsX.txt.old")
set(SYMBOLSX_URL "http://www.aprs.org/symbols/symbolsX.txt")
set(CUSTOM_BINARY_DATA_DIR "${CMAKE_BINARY_DIR}/data")

# we can also move to a separate cmake file and use file(download)
# see conf/install_conf.cmake as example
file(COPY "${CUSTOM_DATA_DIR}/${TOCALLS_TXT}" DESTINATION "${CUSTOM_BINARY_DATA_DIR}")
file(COPY "${CUSTOM_DATA_DIR}/${SYMBOLS-NEW_TXT}" DESTINATION "${CUSTOM_BINARY_DATA_DIR}")
file(COPY "${CUSTOM_DATA_DIR}/${SYMBOLSX_TXT}" DESTINATION "${CUSTOM_BINARY_DATA_DIR}")

add_custom_target(data_rename
  COMMAND ${CMAKE_COMMAND} -E rename "${CUSTOM_BINARY_DATA_DIR}/${TOCALLS_TXT}" "${CUSTOM_BINARY_DATA_DIR}/${TOCALLS_TXT_BKP}"
  COMMAND ${CMAKE_COMMAND} -E rename "${CUSTOM_BINARY_DATA_DIR}/${SYMBOLS-NEW_TXT}" "${CUSTOM_BINARY_DATA_DIR}/${SYMBOLS-NEW_TXT_BKP}"
  COMMAND ${CMAKE_COMMAND} -E rename "${CUSTOM_BINARY_DATA_DIR}/${SYMBOLSX_TXT}" "${CUSTOM_BINARY_DATA_DIR}/${SYMBOLSX_TXT_BKP}"
  )

ExternalProject_Add(download_tocalls
  DEPENDS data_rename
  URL ${TOCALLS_URL}
  PREFIX ""
  DOWNLOAD_DIR "${CUSTOM_BINARY_DATA_DIR}"
  DOWNLOAD_NAME "${TOCALLS_TXT}"
  DOWNLOAD_NO_EXTRACT 0
  EXCLUDE_FROM_ALL 1
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  TEST_COMMAND ""
  )

ExternalProject_Add(download_symbols-new
  DEPENDS data_rename
  URL ${SYMBOLS-NEW_URL}
  PREFIX ""
  DOWNLOAD_DIR "${CUSTOM_BINARY_DATA_DIR}"
  DOWNLOAD_NAME "${SYMBOLS-NEW_TXT}"
  DOWNLOAD_NO_EXTRACT 0
  EXCLUDE_FROM_ALL 1
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  TEST_COMMAND ""
  )

ExternalProject_Add(download_symbolsx
  DEPENDS data_rename
  URL ${SYMBOLSX_URL}
  PREFIX ""
  DOWNLOAD_DIR "${CUSTOM_BINARY_DATA_DIR}"
  DOWNLOAD_NAME "${SYMBOLSX_TXT}"
  DOWNLOAD_NO_EXTRACT 0
  EXCLUDE_FROM_ALL 1
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  TEST_COMMAND ""
  )

add_custom_target(update-data)
add_dependencies(update-data data_rename download_tocalls download_symbols-new download_symbolsx)

install(FILES "${CUSTOM_BINARY_DATA_DIR}/${TOCALLS_TXT}" DESTINATION ${INSTALL_DATA_DIR})
install(FILES "${CUSTOM_BINARY_DATA_DIR}/${SYMBOLS-NEW_TXT}" DESTINATION ${INSTALL_DATA_DIR})
install(FILES "${CUSTOM_BINARY_DATA_DIR}/${SYMBOLSX_TXT}" DESTINATION ${INSTALL_DATA_DIR})
