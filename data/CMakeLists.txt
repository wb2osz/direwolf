#
# Update: 1 May 2023  (still 1.7 dev version)
#
# The original intention was to allow an easy way to download the most
# recent versions of some files.
#
# "update-data" would only work once.
#
# These locations are no longer being maintained:
#	http://www.aprs.org/aprs11/tocalls.txt		-- 14 Dec 2021
#	http://www.aprs.org/symbols/symbols-new.txt	-- 17 Mar 2021
#	http://www.aprs.org/symbols/symbolsX.txt	-- 25 Nov 2015
# so there is no reason to provide a capability grab the latest version.
#
# Rather than fixing an obsolete capability, it will just be removed.
#
# The destination field is often used to identify the manufacturer/model.
# These are not hardcoded into Dire Wolf.  Instead they are read from
# a file called tocalls.txt at application start up time.
#
# The original permanent symbols are built in but the "new" symbols,
# using overlays, are often updated.  These are also read from files.
#


include(ExternalProject)

set(TOCALLS_TXT "tocalls.txt")
set(SYMBOLS-NEW_TXT "symbols-new.txt")
set(SYMBOLSX_TXT "symbolsX.txt")
set(CUSTOM_BINARY_DATA_DIR "${CMAKE_BINARY_DIR}/data")

# we can also move to a separate cmake file and use file(download)
# see conf/install_conf.cmake as example
file(COPY "${CUSTOM_DATA_DIR}/${TOCALLS_TXT}" DESTINATION "${CUSTOM_BINARY_DATA_DIR}")
file(COPY "${CUSTOM_DATA_DIR}/${SYMBOLS-NEW_TXT}" DESTINATION "${CUSTOM_BINARY_DATA_DIR}")
file(COPY "${CUSTOM_DATA_DIR}/${SYMBOLSX_TXT}" DESTINATION "${CUSTOM_BINARY_DATA_DIR}")

install(FILES "${CUSTOM_BINARY_DATA_DIR}/${TOCALLS_TXT}" DESTINATION ${INSTALL_DATA_DIR})
install(FILES "${CUSTOM_BINARY_DATA_DIR}/${SYMBOLS-NEW_TXT}" DESTINATION ${INSTALL_DATA_DIR})
install(FILES "${CUSTOM_BINARY_DATA_DIR}/${SYMBOLSX_TXT}" DESTINATION ${INSTALL_DATA_DIR})
