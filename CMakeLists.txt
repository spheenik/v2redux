# V2M portable player — honest-to-the-source, deterministic V2 synth renderer.
#
# Builds everywhere (Linux/macOS/Windows; GCC/Clang/MSVC). The whole point of
# this project is BIT-IDENTICAL output on every host, so the float policy below
# is not optional decoration — it is the contract:
#   * no FMA contraction  (an FMA skips a rounding step -> different bits)
#   * never fast-math      (FTZ/DAZ would break v0 subnormal-envelope semantics)
#   * SSE2, never x87 80-bit (excess precision diverges from the SSE/NEON path)
# Verified: x86_64 and aarch64 produce the same sha256 for the whole corpus.
cmake_minimum_required(VERSION 3.16)
project(v2redux VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- feature configuration (era range + optional Ronan speech synth) -------
option(V2_RONAN "Enable the Ronan speech-synth channel" ON)
set(V2_VER_MIN "" CACHE STRING "Lower bound of the compiled era range (blank = full)")
set(V2_VER_MAX "" CACHE STRING "Upper bound of the compiled era range (blank = full)")

# ---- the determinism / warning policy, per compiler ------------------------
# Applied to every target so test binaries match the library bit-for-bit.
add_library(v2_flags INTERFACE)
if(MSVC)
  # /fp:precise (default) does NOT contract; make it explicit and forbid fast.
  target_compile_options(v2_flags INTERFACE /fp:precise /W3 /EHsc)
  # 32-bit MSVC can emit x87; force SSE2 so it matches x64 / the SSE path.
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_options(v2_flags INTERFACE /arch:SSE2)
  endif()
  target_compile_definitions(v2_flags INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(v2_flags INTERFACE
    -ffp-contract=off -Wall -Wextra)
  # 32-bit x86 (GCC/Clang) defaults to x87; pin SSE2 like the 64-bit build.
  if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND
     CMAKE_SYSTEM_PROCESSOR MATCHES "i[0-9]86|x86")
    target_compile_options(v2_flags INTERFACE -msse2 -mfpmath=sse)
  endif()
endif()

add_library(v2_defs INTERFACE)
if(NOT V2_RONAN)
  target_compile_definitions(v2_defs INTERFACE V2_RONAN=0)
endif()
if(NOT V2_VER_MIN STREQUAL "")
  target_compile_definitions(v2_defs INTERFACE V2_VER_MIN=${V2_VER_MIN})
endif()
if(NOT V2_VER_MAX STREQUAL "")
  target_compile_definitions(v2_defs INTERFACE V2_VER_MAX=${V2_VER_MAX})
endif()

# ---- the library -----------------------------------------------------------
add_library(v2redux STATIC
  src/v2player.cpp src/v2load.cpp src/v2core.cpp src/v2seq.cpp src/ronan.cpp)
target_link_libraries(v2redux PUBLIC v2_defs PRIVATE v2_flags)
target_include_directories(v2redux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---- the offline render CLI ------------------------------------------------
add_executable(v2dump src/v2dump.cpp)
target_link_libraries(v2dump PRIVATE v2redux v2_flags)

# ---- tests (ctest) ---------------------------------------------------------
# Off by default so a consumer embedding the lib pays nothing for them.
option(V2_BUILD_TESTS "Build the test suite" ON)
if(V2_BUILD_TESTS)
  enable_testing()

  # standalone (no lib) math/table self-checks
  foreach(t mathcheck tablecheck)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/${t}.cpp)
      add_executable(${t} test/${t}.cpp)
      target_link_libraries(${t} PRIVATE v2_flags)
      target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
      add_test(NAME ${t} COMMAND ${t})
    endif()
  endforeach()

  # lib-linked checks. These take corpus paths as args; wire the genuine v6
  # songs. loadcheck is build-only: it needs a *converted* file, and the
  # converted corpus was removed by design (the player renders originals).
  set(V2M_DIR ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
  foreach(t twoinstance loadcheck forcecheck)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/${t}.cpp)
      add_executable(${t} test/${t}.cpp)
      target_link_libraries(${t} PRIVATE v2redux v2_flags)
    endif()
  endforeach()
  if(TARGET twoinstance)
    add_test(NAME twoinstance COMMAND twoinstance
      ${V2M_DIR}/pzero_new.v2m ${V2M_DIR}/v2_zeitmaschine_new.v2m)
  endif()
  if(TARGET forcecheck)
    add_test(NAME forcecheck COMMAND forcecheck ${V2M_DIR}/pzero_new.v2m)
  endif()

  # the corpus determinism gate: hashes of v2dump renders vs baselines.sha256.
  # Drives THIS build's v2dump (not a stale one in the source tree).
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME corpus_hashes
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/test/check.py
              --dump $<TARGET_FILE:v2dump>)
  endif()
endif()
