cmake_minimum_required(VERSION 3.10.0)

project(direwolf)

# configure version
set(direwolf_VERSION_MAJOR "1")
set(direwolf_VERSION_MINOR "9")
set(direwolf_VERSION_PATCH "0")
set(direwolf_VERSION_SUFFIX "Development")

# options
# See Issue 297.
option(FORCE_SSE "Compile with SSE instruction only" ON)
option(FORCE_SSSE3 "Compile with SSSE3 instruction only" OFF)
option(FORCE_SSE41 "Compile with SSE4.1 instruction only" OFF)
option(OPTIONAL_TEST "Compile optional test (might be broken)" OFF)
# UNITTEST option must be after CMAKE_BUILT_TYPE
option(OPTIONAL_DNSSD "Include support for DNS-SD" ON)

# where cmake find custom modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

# fix c standard used on the project
# No, specifying c99 prevented cpp 15 (c23) from pointing out an error.
# removed for release 1.8 # set(CMAKE_C_STANDARD 99)

# Set additional project information
set(COMPANY "wb2osz")
add_definitions("-DCOMPANY=\"${COMPANY}\"")
set(APPLICATION_NAME "Dire Wolf")
add_definitions("-DAPPLICATION_NAME=\"${APPLICATION_NAME}\"")
set(APPLICATION_MAINTAINER="John Langner, WB2OSZ")
set(COPYRIGHT "Copyright (c) 2011-2025 John Langner, WB2OSZ. All rights reserved.")
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
set(CUSTOM_HIDAPI_DIR "${CUSTOM_EXTERNAL_DIR}/hidapi")
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
# This has architecture of the build machine, not the target platform.
# e.g. Comes out as x86_64 when building for i686 target platform.
#set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${direwolf_VERSION}_${CMAKE_SYSTEM_PROCESSOR}")
# We don't know the target yet so this is set after FindCPUflags.
set(CPACK_PACKAGE_CONTACT "https://github.com/wb2osz/direwolf")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dire Wolf is an AX.25 soundcard TNC, digipeater, APRS IGate, GPS tracker, and APRStt gateway")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_SOURCE_IGNORE_FILES "${PROJECT_BINARY_DIR};/.git/;.gitignore;menu.yml;.travis.yml;.appveyor.yml;default.nix;.envrc;TODOs.org;/.scripts/")
SET(CPACK_PACKAGE_VERSION "${direwolf_VERSION}")
SET(CPACK_PACKAGE_VERSION_MAJOR "${direwolf_VERSION_MAJOR}")
SET(CPACK_PACKAGE_VERSION_MINOR "${direwolf_VERSION_MINOR}")
SET(CPACK_PACKAGE_VERSION_PATCH "${direwolf_VERSION_PATCH}")
SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libasound2,libgps23")

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

# temp for testing
#message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
#message(STATUS "Pointer size: ${CMAKE_SIZEOF_VOID_P}")
#if(CMAKE_SIZEOF_VOID_P EQUAL 8)
#    message(STATUS "Target is 64-bit")
#elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
#    message(STATUS "Target is 32-bit")
#else()
#    message(WARNING "Unknown pointer size: ${CMAKE_SIZEOF_VOID_P}")
#endif()


#message(STATUS "ARCHITECTURE: ${ARCHITECTURE}")
# For Intel/AMD target, we should see x86 or x86_64.
# i686 package name for 32 bit is to imply minimum Pentium III with SSE instructions.

if(${ARCHITECTURE} STREQUAL "x86")
  set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${direwolf_VERSION}_i686")
else()
  set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${direwolf_VERSION}_${ARCHITECTURE}")
endif()

# auto include current directory
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# set OS dependent variables
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  set(LINUX TRUE)

  configure_file("${CMAKE_SOURCE_DIR}/cmake/cpack/${CMAKE_PROJECT_NAME}.desktop.in"
    "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop" @ONLY)

elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
  set(FREEBSD TRUE)
  configure_file("${CMAKE_SOURCE_DIR}/cmake/cpack/${CMAKE_PROJECT_NAME}.desktop.in"
    "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop" @ONLY)

elseif(${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
  set(OPENBSD TRUE)
  set(HAVE_SNDIO TRUE)

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

  # optionally enable dns-sd
  if(OPTIONAL_DNSSD)
    set(USE_MACOS_DNSSD ON)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_MACOS_DNSSD")
  endif()

elseif (WIN32)

  if(C_MSVC)
    if (NOT VS2015 AND NOT VS2017 AND NOT VS2019)
      message(FATAL_ERROR "You must use Microsoft Visual Studio 2015, 2017 or 2019 as compiler")
    else()
      # compile with full multicore
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
      set(CUSTOM_SHELL_BIN "")
    endif()
  endif()
endif()

if (C_CLANG OR C_GCC)
  # _BSD_SOURCE is deprecated we need to use _DEFAULT_SOURCE.
  #
  # That works find for more modern compilers but we have issues with:
  #	Centos 7, gcc 4.8.5, glibc 2.17
  #	Centos 6, gcc 4.4.7, glibc 2.12
  #
  # CentOS 6 & 7:  Without -D_BSD_SOURCE, we get Warning: Implicit declaration of
  # functions alloca, cfmakeraw, scandir, setlinebuf, strcasecmp, strncasecmp, and strsep.
  # When a function (like strsep) returns a pointer, the compiler instead assumes a 32 bit
  # int and sign extends it out to be a 64 bit pointer.  Use the pointer and Kaboom!
  #
  # CentOS 6: We have additional problem.  Without -D_POSIX_C_SOURCE=199309L, we get
  # implicit declaration of function clock_gettime and the linker can't find it.
  #
  # It turns out that -D_GNU_SOURCE can be used instead of both of those.  For more information, 
  # see https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
  #
  # Why was this not an issue before?  If gcc is used without the -std=c99 option, 
  # it is perfectly happy with clock_gettime, strsep, etc. but with the c99 option, it no longer
  # recognizes a bunch of commonly used functions.  Using _GNU_SOURCE, rather than _DEFAULT_SOURCE
  # solves the problem for CentOS 6 & 7.  This also makes -D_XOPEN_SOURCE= unnecessary.
  # I hope it doesn't break with newer versions of glibc.
  #
  # I also took out -Wextra because it spews out so much noise a serious problem was not noticed.
  # It might go back in someday when I have more patience to clean up all the warnings.
  #


  # Address Sanitizer.  See https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
  # gcc links with libasan automatically when compiled with -fsanitize=address

  # set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g -Og -fno-optimize-sibling-calls -fno-omit-frame-pointer")


  if(FREEBSD)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wvla -ffast-math -ftree-vectorize -D_DEFAULT_SOURCE ${EXTRA_FLAGS}")
  else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wvla -ffast-math -ftree-vectorize -D_GNU_SOURCE ${EXTRA_FLAGS}")
  endif()
  #
  #
  # -lm is needed for functions in math.h
  if (LINUX)
    # We have another problem with CentOS 6.  clock_gettime() is in librt so we need -lrt.
    # The clock_* functions were moved into gnu libc for version 2.17.
    #   https://sourceware.org/ml/libc-announce/2012/msg00001.html
    # If using gnu libc 2.17, or later, the -lrt is no longer needed but doesn't hurt.
    # I'm making this conditional on LINUX because it is not needed for BSD and MacOSX.
    link_libraries("-lrt -lm")
  else()
    link_libraries("-lm")
  endif()
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
  if(FREEBSD)
    set(INSTALL_MAN_DIR "man/man1")
  else()
    set(INSTALL_MAN_DIR "share/man/man1")
  endif()
  set(INSTALL_DATA_DIR "share/${PROJECT_NAME}")
endif(WIN32 OR CYGWIN)

# requirements

include(CheckSymbolExists)

# Some platforms provide their own strlcpy & strlcat. (BSD, MacOSX)
# Others don't so we provide our own. (Windows, most, but not all Linux)
# Here we detect whether these are provided by the OS and set a symbol
# so that:
#  (1) libgps does not supply its own version.
#  (2) we know whether we need to supply our own copy.
#
# This was all working fine until these were added to the gnu c library 2.38.
# References:
#  - https://www.gnu.org/software/libc/sources.html
#  - https://sourceware.org/git/?p=glibc.git;a=blob_plain;f=NEWS;hb=HEAD
#
# This test is not detecting them for glibc 2.38 resulting in a conflict.
# Why?  Are they declared in a different file or in some strange way?
#
# This is how they are declared in include/string.h:
#
#	extern __typeof (strlcpy) __strlcpy;
#	libc_hidden_proto (__strlcpy)
#	extern __typeof (strlcat) __strlcat;
#	libc_hidden_proto (__strlcat)
#
# Apparently cmake does not recognize this style.
# Keep this here for BSD type systems where it behaves as expected.
# We will need to add a hack in direwolf.h to define these if glibc version >= 2.38.

check_symbol_exists(strlcpy string.h HAVE_STRLCPY)
if(HAVE_STRLCPY)
    add_compile_options(-DHAVE_STRLCPY)
endif()
check_symbol_exists(strlcat string.h HAVE_STRLCAT)
if(HAVE_STRLCAT)
    add_compile_options(-DHAVE_STRLCAT)
endif()

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
  if(WIN32 OR CYGWIN)
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-hamlib")
  endif()
else()
  set(HAMLIB_INCLUDE_DIRS "")
  set(HAMLIB_LIBRARIES "")
endif()

find_package(gpiod)
if(GPIOD_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_GPIOD")

  # Version 2 uses a completely different API.
  # Try to determine version and put it on compile command line.

  # Require pkg-config support
  find_package(PkgConfig REQUIRED)
  # Probe for libgpiod
  pkg_check_modules(LIBGPIOD REQUIRED libgpiod)
  # Abort if not found
  if(NOT LIBGPIOD_FOUND)
    message(FATAL_ERROR "libgpiod not found via pkg-config")
  endif()

  # LIBGPIOD_VERSION holds the full x.y.z string

  # Now split out major and minor versions into separate integers
  string(REPLACE "." ";" VLIST ${LIBGPIOD_VERSION})
  list(GET VLIST 0 LIBGPIOD_VERSION_MAJOR)
  list(GET VLIST 1 LIBGPIOD_VERSION_MINOR)
  message(STATUS "libgpiod version ${LIBGPIOD_VERSION} major=${LIBGPIOD_VERSION_MAJOR} minor=${LIBGPIOD_VERSION_MINOR}")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLIBGPIOD_VERSION=${LIBGPIOD_VERSION}")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLIBGPIOD_VERSION_MAJOR=${LIBGPIOD_VERSION_MAJOR}")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLIBGPIOD_VERSION_MINOR=${LIBGPIOD_VERSION_MINOR}")

else()
  set(GPIOD_INCLUDE_DIRS "")
  set(GPIOD_LIBRARIES "")
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

  if(OPTIONAL_DNSSD)
    find_package(Avahi)
    if(AVAHI_CLIENT_FOUND)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_AVAHI_CLIENT")
    endif()
  endif()

elseif (HAVE_SNDIO)
  find_package(sndio REQUIRED)
  if(SNDIO_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_SNDIO")
  endif()

elseif (APPLE)
  find_package(Portaudio REQUIRED)
  if(PORTAUDIO_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_PORTAUDIO")
  endif()

  find_package(hidapi REQUIRED)
  if(HIDAPI_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_CM108")
  endif()

elseif (NOT WIN32 AND NOT CYGWIN)
  find_package(Portaudio REQUIRED)
  if(PORTAUDIO_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_PORTAUDIO")
  endif()

else()
  set(ALSA_INCLUDE_DIRS "")
  set(ALSA_LIBRARIES "")
  set(UDEV_INCLUDE_DIRS "")
  set(UDEV_LIBRARIES "")
  # Version 1.7 supports CM108/CM119 GPIO PTT for Windows.
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_CM108")
  set(PORTAUDIO_INCLUDE_DIRS "")
  set(PORTAUDIO_LIBRARIES "")
  set(SNDIO_INCLUDE_DIRS "")
  set(SNDIO_LIBRARIES "")
endif()

# manage and fetch new data
add_subdirectory(data)

# external libraries
add_subdirectory(${CUSTOM_GEOTRANZ_DIR})
add_subdirectory(${CUSTOM_REGEX_DIR})
if(NOT APPLE)
  # Mac builds use the hidapi library, not custom local files
  add_subdirectory(${CUSTOM_HIDAPI_DIR})
endif()
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
if (LINUX OR FREEBSD)
  install(FILES ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop DESTINATION share/applications)
  install(FILES ${CMAKE_SOURCE_DIR}/cmake/cpack/${CMAKE_PROJECT_NAME}_icon.png DESTINATION share/pixmaps)
endif()

# install hamlib DLLs and license files on Windows
if ((WIN32 OR CYGWIN) AND HAMLIB_FOUND)
  install(FILES ${HAMLIB_DLLS} DESTINATION ${INSTALL_BIN_DIR})
  install(FILES ${HAMLIB_ROOT_DIR}/COPYING.LIB.txt DESTINATION ${INSTALL_DOC_DIR} RENAME hamlib-COPYING.LIB.txt)
  install(FILES ${HAMLIB_ROOT_DIR}/LICENSE.txt DESTINATION ${INSTALL_DOC_DIR} RENAME hamlib-LICENSE.txt)
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
