# Cross-compile v2redux for 64-bit Windows from Linux with MinGW-w64.
#   cmake -S . -B build-win \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#     -DV2_BUILD_TESTS=OFF -DV2_RONAN=ON
#   cmake --build build-win --target v2play
# Produces build-win/v2play.exe (statically linked -> no runtime DLLs needed).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_mingw x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${_mingw}-gcc)
set(CMAKE_CXX_COMPILER ${_mingw}-g++)
set(CMAKE_RC_COMPILER  ${_mingw}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${_mingw})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
