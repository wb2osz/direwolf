# Clang or AppleClang (see CMP0025)
if(NOT DEFINED C_CLANG AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(C_CLANG 1)
elseif(NOT DEFINED C_GCC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(C_GCC 1)
elseif(NOT DEFINED C_MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(C_MSVC 1)
endif()

# Detect current compilation architecture and create standard definitions
include(CheckSymbolExists)
function(detect_architecture symbol arch)
    if (NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)

        # The output variable needs to be unique across invocations otherwise
        # CMake's crazy scope rules will keep it defined
        if (ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
            add_definitions(-DARCHITECTURE_${arch}=1)
        endif()
    endif()
endfunction()

# direwolf versions thru 1.5 were available pre-built for 32 bit Windows targets.
# Research and experimentation revealed that the SSE instructions made a big
# difference in runtime speed but SSE2 and later were not significantly better
# for this application.  I decided to build with only the SSE instructions making
# the Pentium 3 the minimum requirement. SSE2 would require at least a Pentium 4
# and offered no significant performance advantage.
# These are ancient history - from the previous Century - but old computers, generally
# considered useless for anything else, often end up in the ham shack.
#
# When cmake was first used for direwolf, the default target became 64 bit and the
# SSE2, SSE3, SSE4.1, and SSE4.2 instructions were automatically enabled based on the
# build machine capabilities.  This was fine until I tried running the application
# on a computer much older than where it was built.  It did not have the SSE4 instructions
# and the application died without a clue for the reason.
# Just how much benefit do these new instructions provide for this application?
#
# These were all run on the same computer, but compiled in different ways.
# Times to run atest with Track 1 of the TNC test CD:
#
#   direwolf 1.5 - 32 bit target - gcc 6.3.0
#
#	60.4 sec. Pentium 3 with SSE
#
#   direwolf 1.6 - 32 bit target - gcc 7.4.0
#
#	81.0 sec. with no SIMD instructions enabled.
#	54.4 sec. with SSE
#	52.0 sec. with SSE2
#	52.4 sec. with SSE2, SSE3
#	52.3 sec. with SSE2, SSE3, SSE4.1, SSE4.2
#	49.9 sec. Fedora standard: -m32 -march=i686 -mtune=generic -msse2 -mfpmath=sse
#	50.4 sec. sse not sse2:    -m32 -march=i686 -mtune=generic -msse -mfpmath=sse
#
#	That's what I found several years ago with a much older compiler.
#	The original SSE helped a lot but SSE2 and later made little difference.
#
#   direwolf 1.6 - 64 bit target - gcc 7.4.0
#
#	34.8 sec. with no SIMD instructions enabled.
#	34.8 sec. with SSE
#	34.8 sec. with SSE2
#	34.2 sec. with SSE2, SSE3
#	33.5 sec. with SSE2, SSE3, SSE4.1, SSE4.2
#	33.4 Fedora standard: -mtune=generic
#
# 	Why do we see such little variation?  64-bit target implies
#	SSE, SSE2, SSE3 instructions are available.
#
# Building for a 64 bit target makes it run about 1.5x faster on the same hardware.
#
# The default will be set for maximum portability so packagers won't need to
# to anything special.
#
#
# While ENABLE_GENERIC also had the desired result (for x86_64), I don't think
# it is the right approach.  It prevents the detection of the architecture,
# i.e. x86, x86_64, ARM, ARM64.  That's why it did not go looking for the various
# SSE instructions.   For x86, we would miss out on using SSE.

if (NOT ENABLE_GENERIC)
    if (C_MSVC)
        detect_architecture("_M_AMD64" x86_64)
        detect_architecture("_M_IX86" x86)
        detect_architecture("_M_ARM" ARM)
        detect_architecture("_M_ARM64" ARM64)
    else()
        detect_architecture("__x86_64__" x86_64)
        detect_architecture("__i386__" x86)
        detect_architecture("__arm__" ARM)
        detect_architecture("__aarch64__" ARM64)
    endif()
endif()
if (NOT DEFINED ARCHITECTURE)
    set(ARCHITECTURE "GENERIC")
    set(ARCHITECTURE_GENERIC 1)
    add_definitions(-DARCHITECTURE_GENERIC=1)
endif()
message(STATUS "Target architecture: ${ARCHITECTURE}")

set(TEST_DIR ${PROJECT_SOURCE_DIR}/cmake/cpu_tests)

# flag that set the minimum cpu flag requirements
# used to create re-distribuitable binary

if (${ARCHITECTURE} MATCHES "x86_64|x86" AND (FORCE_SSE OR FORCE_SSSE3 OR FORCE_SSE41))
  if (FORCE_SSE)
    set(HAS_SSE ON CACHE BOOL "SSE SIMD enabled")
    if(C_GCC OR C_CLANG)
      if (${ARCHITECTURE} MATCHES "x86_64")
          # All 64-bit capable chips support MMX, SSE, SSE2, and SSE3
          # so they are all enabled automatically.  We don't want to use
          # SSE4, based on build machine capabilites, because the application
          # would not run properly on an older CPU.
          set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mtune=generic" )
          set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mtune=generic" )
      else()
          # Fedora standard uses -msse2 here.
          # I dropped it down to -msse for greater compatibility and little penalty.
          set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -march=i686 -mtune=generic -msse -mfpmath=sse" )
          set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -march=i686 -mtune=generic -msse -mfpmath=sse" )
      endif()
      message(STATUS "Use SSE SIMD instructions")
      add_definitions(-DUSE_SSE)
    elseif(C_MSVC)
      set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /arch:SSE" )
      set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:SSE" )
      set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
      set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
      set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
      message(STATUS "Use MSVC SSSE3 SIMD instructions")
      add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
      add_definitions(-DUSE_SSSE3)
    endif()
  elseif (FORCE_SSSE3)
    set(HAS_SSSE3 ON CACHE BOOL "SSSE3 SIMD enabled")
    if(C_GCC OR C_CLANG)
      set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mssse3" )
      set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mssse3" )
      message(STATUS "Use SSSE3 SIMD instructions")
      add_definitions(-DUSE_SSSE3)
    elseif(C_MSVC)
      set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /arch:SSSE3" )
      set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:SSSE3" )
      set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
      set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
      set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
      message(STATUS "Use MSVC SSSE3 SIMD instructions")
      add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
      add_definitions(-DUSE_SSSE3)
    endif()
  elseif (FORCE_SSE41)
    set(HAS_SSSE3 ON CACHE BOOL "SSSE3 SIMD enabled")
    set(HAS_SSE4_1 ON CACHE BOOL "Architecture has SSE 4.1 SIMD enabled")
    if(C_GCC OR C_CLANG)
      set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.1" )
      set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.1" )
      message(STATUS "Use SSE 4.1 SIMD instructions")
      add_definitions(-DUSE_SSSE3)
      add_definitions(-DUSE_SSE4_1)
    elseif(C_MSVC)
      # seems that from MSVC 2015 comiler doesn't support those flags
      set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /arch:SSE4_1" )
      set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:SSE4_1" )
      set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
      set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
      set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
      message(STATUS "Use SSE 4.1 SIMD instructions")
      add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
      add_definitions(-DUSE_SSSE3)
      add_definitions(-DUSE_SSE4_1)
    endif()
  endif()
else ()
  if (${ARCHITECTURE} MATCHES "x86_64|x86")
    if(C_MSVC)
      try_run(RUN_SSE2 COMPILE_SSE2 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_sse2.cxx" COMPILE_DEFINITIONS /O0)
    else()
      try_run(RUN_SSE2 COMPILE_SSE2 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_sse2.cxx" COMPILE_DEFINITIONS -msse2 -O0)
    endif()
    if(COMPILE_SSE2 AND RUN_SSE2 EQUAL 0)
      set(HAS_SSE2 ON CACHE BOOL "Architecture has SSSE2 SIMD enabled")
      message(STATUS "Use SSE2 SIMD instructions")
      if(C_GCC OR C_CLANG)
        set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2" )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2" )
        add_definitions(-DUSE_SSE2)
      elseif(C_MSVC)
        set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /arch:SSE2" )
        set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:SSE2" )
        set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:SSE2" )
        set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:SSE2" )
        set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
        add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
        add_definitions(-DUSE_SSE2)
      endif()
    else()
      set(HAS_SSE2 OFF CACHE BOOL "Architecture does not have SSSE2 SIMD enabled")
    endif()
    if(C_MSVC)
      try_run(RUN_SSSE3 COMPILE_SSSE3 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_ssse3.cxx" COMPILE_DEFINITIONS /O0)
    else()
      try_run(RUN_SSSE3 COMPILE_SSSE3 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_ssse3.cxx" COMPILE_DEFINITIONS -mssse3 -O0)
    endif()
    if(COMPILE_SSSE3 AND RUN_SSSE3 EQUAL 0)
      set(HAS_SSSE3 ON CACHE BOOL "Architecture has SSSE3 SIMD enabled")
      message(STATUS "Use SSSE3 SIMD instructions")
      if(C_GCC OR C_CLANG)
        set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mssse3" )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mssse3" )
        add_definitions(-DUSE_SSSE3)
       elseif(C_MSVC)
         # seems not present on MSVC 2017
         #set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:SSSE3" )
         set( CMAKE_C_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
         set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
         set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
         add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
         add_definitions(-DUSE_SSSE3)
         endif()
       else()
         set(HAS_SSSE3 OFF CACHE BOOL "Architecture does not have SSSE3 SIMD enabled")
       endif()
       if(C_MSVC)
         try_run(RUN_SSE4_1 COMPILE_SSE4_1 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_sse41.cxx" COMPILE_DEFINITIONS /O0)
       else()
        try_run(RUN_SSE4_1 COMPILE_SSE4_1 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_sse41.cxx" COMPILE_DEFINITIONS -msse4.1 -O0)
    endif()
    if(COMPILE_SSE4_1 AND RUN_SSE4_1 EQUAL 0)
      set(HAS_SSE4_1 ON CACHE BOOL "Architecture has SSE 4.1 SIMD enabled")
      message(STATUS "Use SSE 4.1 SIMD instructions")
      if(C_GCC OR C_CLANG)
        set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.1" )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.1" )
        add_definitions(-DUSE_SSE4_1)
       elseif(C_MSVC)
           # seems not present on MSVC 2017
           #set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:SSE4_1" )
           #set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:SSE4_1" )
           set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
           set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
           set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
           add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
           add_definitions(-DUSE_SSE4_1)
       endif()
    else()
       set(HAS_SSE4_1 OFF CACHE BOOL "Architecture does not have SSE 4.1 SIMD enabled")
    endif()
    if(C_MSVC)
        try_run(RUN_SSE4_2 COMPILE_SSE4_2 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_sse42.cxx" COMPILE_DEFINITIONS /O0)
    else()
        try_run(RUN_SSE4_2 COMPILE_SSE4_2 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_sse42.cxx" COMPILE_DEFINITIONS -msse4.2 -O0)
    endif()
    if(COMPILE_SSE4_2 AND RUN_SSE4_2 EQUAL 0)
       set(HAS_SSE4_2 ON CACHE BOOL "Architecture has SSE 4.2 SIMD enabled")
       message(STATUS "Use SSE 4.2 SIMD instructions")
       if(C_GCC OR C_CLANG)
           set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.2" )
           set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2" )
           add_definitions(-DUSE_SSE4_2)
       elseif(C_MSVC)
           # seems not present on MSVC 2017
           #set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:SSE4_2" )
           #set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:SSE4_2" )
           set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
           set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox" )
           set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
           add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
           add_definitions(-DUSE_SSE4_2)
       endif()
    else()
       set(HAS_SSE4_2 OFF CACHE BOOL "Architecture does not have SSE 4.2 SIMD enabled")
    endif()
    if(C_MSVC)
        try_run(RUN_AVX COMPILE_AVX "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_avx.cxx" COMPILE_DEFINITIONS /O0)
    else()
        try_run(RUN_AVX COMPILE_AVX "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_avx.cxx" COMPILE_DEFINITIONS -mavx -O0)
    endif()
    if(COMPILE_AVX AND RUN_AVX EQUAL 0)
       set(HAS_AVX ON CACHE BOOL "Architecture has AVX SIMD enabled")
       message(STATUS "Use AVX SIMD instructions")
       if(C_GCC OR C_CLANG)
           set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx" )
           set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx" )
           add_definitions(-DUSE_AVX)
       elseif(C_MSVC)
         set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /arch:AVX" )
         set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:AVX" )
         set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:AVX" )
         set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:AVX" )
         set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
         add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
         add_definitions(-DUSE_AVX)
       endif()
    else()
       set(HAS_AVX OFF CACHE BOOL "Architecture does not have AVX SIMD enabled")
    endif()
    if(C_MSVC)
        try_run(RUN_AVX2 COMPILE_AVX2 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_avx2.cxx" COMPILE_DEFINITIONS /O0)
    else()
        try_run(RUN_AVX2 COMPILE_AVX2 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_avx2.cxx" COMPILE_DEFINITIONS -mavx2 -O0)
    endif()
    if(COMPILE_AVX2 AND RUN_AVX2 EQUAL 0)
       set(HAS_AVX2 ON CACHE BOOL "Architecture has AVX2 SIMD enabled")
       message(STATUS "Use AVX2 SIMD instructions")
       if(C_GCC OR C_CLANG)
           set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2" )
           set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2" )
           add_definitions(-DUSE_AVX2)
         elseif(C_MSVC)
         set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /arch:AVX2" )
         set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:AVX2" )
         set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:AVX2" )
         set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:AVX2" )
         set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
         add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
         add_definitions(-DUSE_AVX2)
       endif()
    else()
       set(HAS_AVX2 OFF CACHE BOOL "Architecture does not have AVX2 SIMD enabled")
    endif()
    if(C_MSVC)
        try_run(RUN_AVX512 COMPILE_AVX512 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_avx512.cxx" COMPILE_DEFINITIONS /O0)
    else()
        try_run(RUN_AVX512 COMPILE_AVX512 "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_x86_avx512.cxx" COMPILE_DEFINITIONS -mavx512f -O0)
    endif()
    if(COMPILE_AVX512 AND RUN_AVX512 EQUAL 0)
       set(HAS_AVX512 ON CACHE BOOL "Architecture has AVX512 SIMD enabled")
       message(STATUS "Use AVX512 SIMD instructions")
       if(C_GCC OR C_CLANG)
           set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx512f" )
           set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx512f" )
           add_definitions(-DUSE_AVX512)
       elseif(C_MSVC)
         set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /arch:AVX512" )
         set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /arch:AVX512" )
         set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:AVX512" )
         set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oi /GL /Ot /Ox /arch:AVX512" )
         set( CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG" )
         add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
         add_definitions(-DUSE_AVX512)
       endif()
    else()
       set(HAS_AVX512 OFF CACHE BOOL "Architecture does not have AVX512 SIMD enabled")
    endif()
elseif(ARCHITECTURE_ARM)
    if(C_MSVC)
        try_run(RUN_NEON COMPILE_NEON "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_arm_neon.cxx" COMPILE_DEFINITIONS /O0)
    else()
        if(${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL ${CMAKE_SYSTEM_PROCESSOR})
          try_run(RUN_NEON COMPILE_NEON "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_arm_neon.cxx" COMPILE_DEFINITIONS -mfpu=neon -O0)
        else()  
          try_compile(COMPILE_NEON "${CMAKE_BINARY_DIR}/tmp" "${TEST_DIR}/test_arm_neon.cxx" COMPILE_DEFINITIONS -mfpu=neon -O0)
          set(RUN_NEON  0)
        endif()
    endif()
    if(COMPILE_NEON AND RUN_NEON EQUAL 0)
       set(HAS_NEON ON CACHE BOOL "Architecture has NEON SIMD enabled")
       message(STATUS "Use NEON SIMD instructions")
       if(C_GCC OR C_CLANG)
           set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=neon" )
           set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon" )
           add_definitions(-DUSE_NEON)
       endif()
    else()
       set(HAS_NEON OFF CACHE BOOL "Architecture does not have NEON SIMD enabled")
    endif()
elseif(ARCHITECTURE_ARM64)
    # Advanced SIMD (aka NEON) is mandatory for AArch64
    set(HAS_NEON ON CACHE BOOL "Architecture has NEON SIMD enabled")
    message(STATUS "Use NEON SIMD instructions")
    add_definitions(-DUSE_NEON)
endif()
endif()

# clear binary test folder
FILE(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/tmp)
