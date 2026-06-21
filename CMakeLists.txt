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
# OFF by default so the determinism baselines (Ronan-silent) hold; turn ON with
# -DV2_RONAN=ON for songs that use the speech channel (candytron, josie, ...).
option(V2_RONAN "Enable the Ronan speech-synth channel" OFF)
set(V2_VER_MIN "" CACHE STRING "Lower bound of the compiled era range (blank = full)")
set(V2_VER_MAX "" CACHE STRING "Upper bound of the compiled era range (blank = full)")
set(V2_EMBED_SONG "" CACHE FILEPATH
  "Path to a .v2m to bake into v2play (blank = none; v2play then needs a file arg)")

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
target_compile_definitions(v2_defs INTERFACE V2_RONAN=$<BOOL:${V2_RONAN}>)
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

# ---- the native real-time player + visualizer (sokol) ----------------------
# Audio via sokol_audio (ALSA/CoreAudio/WASAPI), graphics via sokol_gfx+gl:
# oscilloscope, log-frequency spectrum analyzer, and L/R VU bars. Links the
# already-deterministic v2redux lib; this target itself is display-only and
# deliberately does NOT inherit v2_flags (no -Wall over the vendored sokol
# headers, and its float math never touches the render path).
option(V2_BUILD_PLAYER "Build the native sokol player/visualizer (v2play)" ON)
if(V2_BUILD_PLAYER)
  set(SOKOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol)
  set(V2PLAY_SRC src/v2play.cpp ${SOKOL_DIR}/sokol_impl.cc)
  set(V2PLAY_OK ON)
  set(V2PLAY_LIBS "")
  set(V2PLAY_DEFS "")

  if(APPLE)
    # sokol_app/audio need Cocoa; compile the impl as Objective-C++.
    set_source_files_properties(${SOKOL_DIR}/sokol_impl.cc PROPERTIES
      COMPILE_OPTIONS "-x;objective-c++")
    list(APPEND V2PLAY_LIBS "-framework Cocoa" "-framework QuartzCore"
      "-framework Metal" "-framework MetalKit" "-framework AudioToolbox")
  elseif(WIN32)
    # Windows backend: D3D11 by default; V2_WIN_GL switches to OpenGL (WGL),
    # which is what runs under Wine (D3D11 there needs a Vulkan/DXVK stack).
    option(V2_WIN_GL "Use the OpenGL backend on Windows instead of D3D11" OFF)
    if(NOT MSVC)
      # MinGW ignores sokol's #pragma comment(lib,...) -> link deps explicitly.
      if(V2_WIN_GL)
        list(APPEND V2PLAY_LIBS opengl32 ole32 gdi32 user32 shell32 kernel32)
      else()
        list(APPEND V2PLAY_LIBS d3d11 dxgi dxguid ole32 gdi32 user32 shell32 kernel32)
      endif()
    endif()
    if(V2_WIN_GL)
      list(APPEND V2PLAY_DEFS SOKOL_GLCORE)
    endif()
  else()
    # Linux / *nix: GLX + X11 + ALSA
    find_package(OpenGL)
    find_package(X11)
    find_package(ALSA)
    find_package(Threads)
    if(OPENGL_FOUND AND X11_FOUND AND ALSA_FOUND)
      list(APPEND V2PLAY_LIBS OpenGL::GL ${X11_LIBRARIES} ${X11_Xi_LIB}
        ${X11_Xcursor_LIB} ALSA::ALSA Threads::Threads ${CMAKE_DL_LIBS})
    else()
      set(V2PLAY_OK OFF)
      message(WARNING "v2play disabled: needs OpenGL + X11(+Xi,Xcursor) + ALSA "
        "(e.g. libgl-dev libx11-dev libxi-dev libxcursor-dev libasound2-dev)")
    endif()
  endif()

  # optional: bake a .v2m into the exe so it plays with no file argument.
  # Pure-CMake hex generation -> a byte-array TU; no external tools.
  if(V2PLAY_OK AND NOT V2_EMBED_SONG STREQUAL "")
    get_filename_component(_song_abs "${V2_EMBED_SONG}" ABSOLUTE
      BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    if(NOT EXISTS "${_song_abs}")
      message(FATAL_ERROR "V2_EMBED_SONG not found: ${_song_abs}")
    endif()
    get_filename_component(_song_name "${_song_abs}" NAME)
    # .cpp (not .c): the project enables only CXX, so a .c source would be
    # silently dropped. extern "C" gives the const arrays external linkage.
    set(_gen ${CMAKE_CURRENT_BINARY_DIR}/embedded_song.cpp)
    file(READ "${_song_abs}" _hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," _bytes "${_hex}")
    file(WRITE ${_gen}
      "// generated from ${_song_name} -- do not edit\n"
      "extern \"C\" const unsigned char v2play_song[] = {${_bytes}};\n"
      "extern \"C\" const unsigned long v2play_song_len = sizeof(v2play_song);\n")
    list(APPEND V2PLAY_SRC ${_gen})
    message(STATUS "v2play: embedding song '${_song_name}'")
  endif()

  if(V2PLAY_OK)
    add_executable(v2play ${V2PLAY_SRC})
    target_link_libraries(v2play PRIVATE v2redux ${V2PLAY_LIBS})
    target_include_directories(v2play PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(V2PLAY_DEFS)
      target_compile_definitions(v2play PRIVATE ${V2PLAY_DEFS})
    endif()
    if(WIN32 AND NOT MSVC)
      # self-contained .exe: no libgcc/libstdc++/winpthread DLLs at runtime
      target_link_options(v2play PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    if(NOT V2_EMBED_SONG STREQUAL "")
      target_compile_definitions(v2play PRIVATE
        V2PLAY_EMBEDDED V2PLAY_EMBED_NAME="${_song_name}")
    endif()
  endif()
endif()

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
