cmake_minimum_required(VERSION 3.1.0)

project(direwolf)

# configure version
set(direwolf_VERSION_MAJOR "1")
set(direwolf_VERSION_MINOR "6")
set(direwolf_VERSION_PATCH "0")
set(direwolf_VERSION_SUFFIX "")

# options
option(FORCE_SSE "Compile with SSE instruction only" OFF)
option(FORCE_SSSE3 "Compile with SSSE3 instruction only" OFF)
option(FORCE_SSE41 "Compile with SSE4.1 instruction only" OFF)
option(OPTIONAL_TEST "Compile optional test (might be broken)" OFF)
# UNITTEST option must be after CMAKE_BUILT_TYPE

# where cmake find custom modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

# fix c standard used on the project
set(CMAKE_C_STANDARD 99)

# Set additional project information
set(COMPANY "wb2osz")
add_definitions("-DCOMPANY=\"${COMPANY}\"")
set(APPLICATION_NAME "Dire Wolf")
add_definitions("-DAPPLICATION_NAME=\"${APPLICATION_NAME}\"")
set(APPLICATION_MAINTAINER="John Langner, WB2OSZ")
set(COPYRIGHT "Copyright (c) 2019 John Langner, WB2OSZ. All rights reserved.")
add_definitions("-DCOPYRIGHT=\"${COPYRIGHT}\"")
set(IDENTIFIER "com.${COMPANY}.${APPLICATION_NAME}")
add_definitions("-DIDENTIFIER=\"${IDENTIFIER}\"")
# raspberry as only lxterminal not xterm
if(NOT (WIN32 OR CYGWIN))
  find_program(BINARY_TERMINAL_BIN lxterminal)
  if(BINARY_TERMINAL_BIN)
    set(APPLICATION_DESKTOP_EXEC "${BINARY_TERMINAL_BIN} -e ${CMAKE_PROJECT_NAME}")
  else()
    set(APPLICATION_DESKTOP_EXEC "xterm -e ${CMAKE_PROJECT_NAME}")
  endif()
endif()

find_package(Git)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git/")
  # we can also use `git describe --tags`
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT res)
    string(REGEX REPLACE "^v([0-9]+)\.([0-9]+)\.([0-9]+)-" "" git_commit ${out})
    set(direwolf_VERSION_SUFFIX "-${git_commit}")
    set(direwolf_VERSION_COMMIT "${git_commit}")
  endif()
endif()

# set variables
set(direwolf_VERSION "${direwolf_VERSION_MAJOR}.${direwolf_VERSION_MINOR}.${direwolf_VERSION_PATCH}${direwolf_VERSION_SUFFIX}")
message(STATUS "${APPLICATION_NAME} Version: ${direwolf_VERSION}")
add_definitions("-DIREWOLF_VERSION=\"${direwolf_VERSION}\"")
add_definitions("-DMAJOR_VERSION=${direwolf_VERSION_MAJOR}")
add_definitions("-DMINOR_VERSION=${direwolf_VERSION_MINOR}")
if(direwolf_VERSION_COMMIT)
  add_definitions("-DEXTRA_VERSION=${direwolf_VERSION_COMMIT}")
endif()

set(CUSTOM_SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(CUSTOM_EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/external")
set(CUSTOM_MISC_DIR "${CUSTOM_EXTERNAL_DIR}/misc")
set(CUSTOM_REGEX_DIR "${CUSTOM_EXTERNAL_DIR}/regex")
set(CUSTOM_GEOTRANZ_DIR "${CUSTOM_EXTERNAL_DIR}/geotranz")
set(CUSTOM_DATA_DIR "${CMAKE_SOURCE_DIR}/data")
set(CUSTOM_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/scripts")
set(CUSTOM_TELEMETRY_DIR "${CUSTOM_SCRIPTS_DIR}/telemetry-toolkit")
set(CUSTOM_CONF_DIR "${CMAKE_SOURCE_DIR}/conf")
set(CUSTOM_DOC_DIR "${CMAKE_SOURCE_DIR}/doc")
set(CUSTOM_MAN_DIR "${CMAKE_SOURCE_DIR}/man")
set(CUSTOM_TEST_DIR "${CMAKE_SOURCE_DIR}/test")
set(CUSTOM_TEST_SCRIPTS_DIR "${CUSTOM_TEST_DIR}/scripts")
set(CUSTOM_SHELL_SHABANG "#!/bin/sh -e")

# cpack variables
set(CPACK_GENERATOR "ZIP")
set(CPACK_STRIP_FILES true)
set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${direwolf_VERSION}_${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_CONTACT "https://github.com/wb2osz/direwolf")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dire Wolf is a software soundcard AX.25 packet modem/TNC and APRS encoder/decoder")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_SOURCE_IGNORE_FILES "${PROJECT_BINARY_DIR};/.git/;.gitignore;menu.yml;.travis.yml;.appveyor.yml;default.nix;.envrc;TODOs.org;/.scripts/")

# if we don't set build_type
if(NOT DEFINED CMAKE_BUILD_TYPE OR "${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type set to: ${CMAKE_BUILD_TYPE}")
message("CMake system: ${CMAKE_SYSTEM_NAME}")

# Unittest should be on for dev builds and off for releases.
if(CMAKE_BUILD_TYPE MATCHES "Release")
  option(UNITTEST "Build unittest binaries." OFF)
else()
  option(UNITTEST "Build unittest binaries." ON)
endif()

# set compiler
include(FindCompiler)

# find cpu flags (and set compiler)
include(FindCPUflags)

# auto include current directory
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# set OS dependant variables
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  set(LINUX TRUE)

  configure_file("${CMAKE_SOURCE_DIR}/cmake/cpack/${CMAKE_PROJECT_NAME}.desktop.in"
    "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop" @ONLY)

elseif(APPLE)
  if("${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
    message(STATUS "Build for macOS target: local version")
  else()
    message(STATUS "Build for macOS target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()

  # prepend path to find_*()
  set(CMAKE_FIND_ROOT_PATH "/opt/local")

  set(CMAKE_MACOSX_RPATH ON)
  message(STATUS "RPATH support: ${CMAKE_MACOSX_RPATH}")

elseif (WIN32)
  if(NOT VS2015 AND NOT VS2017)
    message(FATAL_ERROR "You must use Microsoft Visual Studio 2015 or 2017 as compiler")
  endif()

  # compile with full multicore
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

  set(CUSTOM_SHELL_BIN "")
endif()

if (C_CLANG OR C_GCC)
  # _BSD_SOURCE is deprecated we need to use _DEFAULT_SOURCE
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wvla -ffast-math -ftree-vectorize -D_XOPEN_SOURCE=600 -D_DEFAULT_SOURCE ${EXTRA_FLAGS}")
  # for math.h
  link_libraries("-lm")
elseif (C_MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W3 -MP ${EXTRA_FLAGS}")
endif()

if (C_CLANG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ferror-limit=1")
elseif (C_GCC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmax-errors=1")
endif()

# set installation directories
if (WIN32 OR CYGWIN)
  set(INSTALL_BIN_DIR ".")
  set(INSTALL_DOC_DIR "doc")
  set(INSTALL_CONF_DIR ".")
  set(INSTALL_SCRIPTS_DIR "scripts")
  set(INSTALL_MAN_DIR "man")
  set(INSTALL_DATA_DIR "data")
else()
  set(INSTALL_BIN_DIR "bin")
  set(INSTALL_DOC_DIR "share/doc/${CMAKE_PROJECT_NAME}")
  set(INSTALL_CONF_DIR "${INSTALL_DOC_DIR}/conf")
  set(INSTALL_SCRIPTS_DIR "${INSTALL_DOC_DIR}/scripts")
  set(INSTALL_MAN_DIR "share/man/man1")
  set(INSTALL_DATA_DIR "share/${PROJECT_NAME}")
endif(WIN32 OR CYGWIN)

# requirements
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(GPSD)
if(GPSD_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DENABLE_GPSD")
else()
  set(GPSD_INCLUDE_DIRS "")
  set(GPSD_LIBRARIES "")
endif()

find_package(hamlib)
if(HAMLIB_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_HAMLIB")
else()
  set(HAMLIB_INCLUDE_DIRS "")
  set(HAMLIB_LIBRARIES "")
endif()

if(LINUX)
  find_package(ALSA REQUIRED)
  if(ALSA_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_ALSA")
  endif()

  find_package(udev)
  if(UDEV_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_CM108")
  endif()

elseif (NOT WIN32 AND NOT CYGWIN)
  find_package(portaudio REQUIRED)
  if(PORTAUDIO_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_PORTAUDIO")
  endif()

else()
  set(ALSA_INCLUDE_DIRS "")
  set(ALSA_LIBRARIES "")
  set(UDEV_INCLUDE_DIRS "")
  set(UDEV_LIBRARIES "")
  set(PORTAUDIO_INCLUDE_DIRS "")
  set(PORTAUDIO_LIBRARIES "")
endif()

# manage and fetch new data
add_subdirectory(data)

# external libraries
add_subdirectory(${CUSTOM_GEOTRANZ_DIR})
add_subdirectory(${CUSTOM_REGEX_DIR})
add_subdirectory(${CUSTOM_MISC_DIR})

# direwolf source code and utilities
add_subdirectory(src)

# ctest
if(UNITTEST)
  message(STATUS "Build unit test binaries")
  include(CTest)
  enable_testing()
  add_subdirectory(test)
endif(UNITTEST)

# manage scripts
add_subdirectory(scripts)

# manage config
add_subdirectory(conf)

# install basic docs
install(FILES ${CMAKE_SOURCE_DIR}/CHANGES.md DESTINATION ${INSTALL_DOC_DIR})
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ${INSTALL_DOC_DIR})
install(FILES ${CMAKE_SOURCE_DIR}/external/LICENSE DESTINATION ${INSTALL_DOC_DIR}/external)
add_subdirectory(doc)
add_subdirectory(man)

# install desktop link
if (LINUX)
  install(FILES ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop DESTINATION share/applications)
  install(FILES ${CMAKE_SOURCE_DIR}/cmake/cpack/${CMAKE_PROJECT_NAME}_icon.png DESTINATION share/pixmaps)
endif()

############ uninstall target ################
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/include/uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(uninstall
  COMMAND ${CMAKE_COMMAND} -P
  ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake)

############ packaging ################
add_subdirectory(cmake/cpack)
