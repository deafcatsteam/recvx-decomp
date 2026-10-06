# PC port of Resident Evil Code: Veronica X.
#
# This builds the decompiled game code with a regular host compiler. The
# original PS2 matching build is still driven by compile.py and is unaffected.
#
#   cmake -S . -B build-pc
#   cmake --build build-pc
#
# The port is built as a 32-bit program: the game stores 32-bit pointers in
# its structures and data files, so keeping pointers 4 bytes wide lets the
# decompiled code run unchanged.
cmake_minimum_required(VERSION 3.20)

# Ask for 32-bit code before the compiler is inspected, so that CMake also
# looks for 32-bit libraries. GCC and Clang on Linux and MinGW-w64 accept it.
set(CMAKE_C_FLAGS_INIT "-m32")

project(cvx_pc C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The port must be built as a 32-bit program. On Linux, install "
                        "gcc-multilib; on Windows, use the MinGW-w64 i686 (32-bit) toolchain.")
endif()

# The game stores pointers in DMA tags with only 28 bits of address, as PS2
# memory is 32 MB: keep the program at a fixed address in the low 256 MB.
if(WIN32)
    add_link_options(-Wl,--disable-dynamicbase)
else()
    add_compile_options(-fno-pie)
    add_link_options(-no-pie)
endif()

# Floating point in SSE registers: single precision like the PS2, and much
# faster float/int conversions than the x87 default of 32-bit x86.
add_compile_options(-msse2 -mfpmath=sse)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GAME_DIR ${CMAKE_SOURCE_DIR}/src/ps2/veronica/prog)
set(KATANA_INC ${CMAKE_SOURCE_DIR}/include/recvx-decomp-katana/KATANA/Include)
set(CRI_INC ${CMAKE_SOURCE_DIR}/include/recvx-decomp-cri/cri/mwlib/include)
set(CASE_DIR ${CMAKE_BINARY_DIR}/case)

if(NOT EXISTS ${KATANA_INC}/ninja.h)
    message(FATAL_ERROR "Submodules are missing: git submodule update --init")
endif()

# The Katana and CRI headers come from Windows SDKs and are included with
# inconsistent case; generate links with the spelling the code asks for.
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/port/tools/gen_case_links.py
            ${CASE_DIR} ${KATANA_INC} ${CRI_INC}
    COMMAND_ERROR_IS_FATAL ANY)

# Game files that the port replaces as a whole: the Ninja math (VU0 assembly,
# rewritten in C), the IPU movie player and the EE-gcc runtime helpers. Other
# files only have their assembly disabled with #ifndef PLATFORM_PC; the PC
# versions of those live in port/src (see port/README.md).
set(GAME_REPLACED
    gcc_wrapper.c
    ps2_MovieFunc.c
    ps2_NaMath.c
    ps2_NaMatrix.c
)

file(GLOB GAME_SOURCES ${GAME_DIR}/*.c)
foreach(f ${GAME_REPLACED})
    list(REMOVE_ITEM GAME_SOURCES ${GAME_DIR}/${f})
endforeach()

# PC implementations of the replaced files (only those written so far).
set(PORT_SOURCES
    port/src/ninja/pc_NaMath.c
    port/src/ninja/pc_NaMatrix.c
    port/src/platform/pc_disc.c
    port/src/platform/pc_kernel.c
    port/src/platform/pc_iop.c
    port/src/platform/pc_sdk.c
    port/src/platform/pc_vu0.c
    port/src/audio/pc_adx.c
    port/src/game/pc_game_asm.c
    port/src/game/pc_movie.c
    port/src/game/pc_render3d.c
)

# Settings for code that sees the game's headers: the game itself, the port's
# replacements and the tests. The host-only entry point (pc_main.c) does not
# get them.
add_library(cvx_game_env INTERFACE)

# Host services used by the platform layer, built without the game settings.
add_library(cvx_host STATIC
    port/src/host/pc_host.c
    port/src/host/pc_diag.c
    port/src/host/pc_log.c
    port/src/host/pc_audio.c
    port/src/host/pc_sound.c
    port/src/audio/pc_hsyn.c
    port/src/audio/pc_hseq.c
    port/src/audio/pc_snddrv.c
    port/src/host/pc_video.c
    port/src/platform/pc_hwregs.c
    port/src/gs/gs.c
    port/src/gs/gs_mem.c
    port/src/gs/gs_threads.c
)
target_link_libraries(cvx_host PUBLIC m)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(cvx_host PUBLIC Threads::Threads)
endif()

# Movies: MPEG-2 video decoded by FFmpeg (LGPL), downloaded and built with
# only the MPEG video decoders. It is used by the host side only
# (port/src/host/pc_video.c), whose headers are not affected by the game's
# `long` remapping.
option(CVX_WITH_FFMPEG "Play the game's movies (MPEG-2 decoded by FFmpeg)" ON)
if(CVX_WITH_FFMPEG)
    include(ExternalProject)
    find_program(MAKE_PROGRAM NAMES gmake make mingw32-make REQUIRED)
    cmake_host_system_information(RESULT CVX_NCPU QUERY NUMBER_OF_LOGICAL_CORES)
    set(FFMPEG_PREFIX ${CMAKE_BINARY_DIR}/ffmpeg)
    set(FFMPEG_ARGS
        --prefix=<INSTALL_DIR> --arch=x86
        --disable-everything --disable-autodetect --disable-programs --disable-doc
        --disable-network --disable-avdevice --disable-avformat --disable-swscale
        --disable-swresample --disable-avfilter --disable-asm --disable-debug
        --enable-static --disable-shared
        --enable-decoder=mpeg2video,mpeg1video --enable-parser=mpegvideo)
    if(WIN32)
        list(APPEND FFMPEG_ARGS --target-os=mingw32)
        if(CMAKE_CROSSCOMPILING)
            string(REGEX REPLACE "gcc(\\.exe)?$" "" FFMPEG_CROSS ${CMAKE_C_COMPILER})
            list(APPEND FFMPEG_ARGS --enable-cross-compile --cross-prefix=${FFMPEG_CROSS})
        endif()
    else()
        list(APPEND FFMPEG_ARGS "--cc=${CMAKE_C_COMPILER} -m32" --extra-ldflags=-m32)
    endif()
    ExternalProject_Add(ffmpeg
        GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
        GIT_TAG n7.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS FALSE
        UPDATE_COMMAND ""
        PREFIX ${FFMPEG_PREFIX}
        INSTALL_DIR ${FFMPEG_PREFIX}
        CONFIGURE_COMMAND <SOURCE_DIR>/configure ${FFMPEG_ARGS}
        BUILD_COMMAND ${MAKE_PROGRAM} -j${CVX_NCPU}
        INSTALL_COMMAND ${MAKE_PROGRAM} install
        BUILD_BYPRODUCTS ${FFMPEG_PREFIX}/lib/libavcodec.a ${FFMPEG_PREFIX}/lib/libavutil.a)
    file(MAKE_DIRECTORY ${FFMPEG_PREFIX}/include)
    add_library(cvx_avutil STATIC IMPORTED)
    set_target_properties(cvx_avutil PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_PREFIX}/lib/libavutil.a
        INTERFACE_INCLUDE_DIRECTORIES ${FFMPEG_PREFIX}/include)
    add_library(cvx_avcodec STATIC IMPORTED)
    set_target_properties(cvx_avcodec PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_PREFIX}/lib/libavcodec.a
        INTERFACE_INCLUDE_DIRECTORIES ${FFMPEG_PREFIX}/include
        INTERFACE_LINK_LIBRARIES cvx_avutil)
    add_dependencies(cvx_avutil ffmpeg)
    add_dependencies(cvx_avcodec ffmpeg)
    target_link_libraries(cvx_host PRIVATE cvx_avcodec)
    target_link_libraries(cvx_host PUBLIC atomic)
    if(WIN32)
        target_link_libraries(cvx_host PUBLIC bcrypt ole32 user32)
    endif()
    target_compile_definitions(cvx_host PRIVATE CVX_HAVE_FFMPEG)
endif()

add_library(cvx_game STATIC ${GAME_SOURCES} ${PORT_SOURCES})
target_link_libraries(cvx_game PRIVATE cvx_game_env PUBLIC cvx_host)
target_include_directories(cvx_game_env INTERFACE
    ${CMAKE_SOURCE_DIR}/port/include/sdk
    ${CMAKE_SOURCE_DIR}/src/cri/mwlib/ee/lib/libadxe
    ${GAME_DIR}
    ${CMAKE_SOURCE_DIR}/include/ps2/veronica/prog
    ${CRI_INC}
    ${CMAKE_SOURCE_DIR}/include/recvx-decomp-cri/cri/mwlib/ee/include
)
# The Katana SDK ships its own libc headers for the Dreamcast compiler; search
# it after the system so the host's <math.h>, <string.h>, ... win.
target_compile_options(cvx_game_env INTERFACE
    "SHELL:-idirafter ${CASE_DIR}" "SHELL:-idirafter ${KATANA_INC}")
target_compile_options(cvx_game_env INTERFACE
    "SHELL:-include ${CMAKE_SOURCE_DIR}/port/include/port_prefix.h"
    # Decompiled code is full of K&R-style declarations and implicit casts.
    -w
    -fno-strict-aliasing
    -fwrapv
    # Lets the diagnostics show where the game is (port/src/host/pc_diag.c).
    -fno-omit-frame-pointer
)

enable_testing()

add_executable(test_math port/tests/test_math.c)
target_link_libraries(test_math PRIVATE cvx_game cvx_game_env m)
add_test(NAME math COMMAND test_math)

add_executable(test_ot port/tests/test_ot.c)
target_link_libraries(test_ot PRIVATE cvx_game cvx_game_env m)
add_test(NAME ordering_table COMMAND test_ot)

# The game's main() becomes cvx_main(), called by the port's entry point.
set_source_files_properties(${GAME_DIR}/njloop.c PROPERTIES COMPILE_DEFINITIONS main=cvx_main)

add_executable(cvx_pc port/src/main/pc_main.c port/src/main/pc_window.c)
target_link_libraries(cvx_pc PRIVATE cvx_game m)
if(NOT WIN32)
    set_target_properties(cvx_pc PROPERTIES ENABLE_EXPORTS ON) # names in stack traces
endif()

# Window and input through SDL2: the system's 32-bit SDL2 when there is one,
# otherwise SDL2 is downloaded and built along with the game.
option(CVX_WITH_SDL "Open a window and read input with SDL2" ON)
if(CVX_WITH_SDL)
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        message(STATUS "No 32-bit SDL2 found: building it from source")
        include(FetchContent)
        FetchContent_Declare(SDL2
            URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
            URL_HASH SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        if(UNIX AND NOT APPLE)
            # The 32-bit X11/Wayland libraries are rarely installed; without
            # them this SDL2 has no real video driver. Install your
            # distribution's 32-bit SDL2 (libsdl2-dev:i386) for a window.
            message(WARNING "Building SDL2 without X11/Wayland: install a 32-bit SDL2 "
                            "package (e.g. libsdl2-dev:i386) to get a window on Linux.")
            set(SDL_X11 OFF CACHE BOOL "" FORCE)
            set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
        endif()
        FetchContent_MakeAvailable(SDL2)
    endif()
    if(TARGET SDL2::SDL2-static)
        target_link_libraries(cvx_pc PRIVATE SDL2::SDL2-static)
    else()
        target_link_libraries(cvx_pc PRIVATE SDL2::SDL2)
    endif()
    target_compile_definitions(cvx_pc PRIVATE CVX_HAVE_SDL)
endif()

add_executable(test_gs port/tests/test_gs.c)
target_link_libraries(test_gs PRIVATE cvx_host)
add_test(NAME gs COMMAND test_gs)

# Draws a GS frame dump from the game again (see port/tools/gs_replay.c).
add_executable(gs_replay port/tools/gs_replay.c)
target_link_libraries(gs_replay PRIVATE cvx_host)

add_executable(test_disc port/tests/test_disc.c)
target_link_libraries(test_disc PRIVATE cvx_game cvx_game_env m)
add_test(NAME disc_make_image
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/port/tests/make_test_iso.py
                 ${CMAKE_BINARY_DIR}/test_disc.iso)
add_test(NAME disc COMMAND test_disc)
set_tests_properties(disc PROPERTIES
    DEPENDS disc_make_image
    ENVIRONMENT CVX_ISO=${CMAKE_BINARY_DIR}/test_disc.iso)

add_executable(test_3d port/tests/test_3d.c)
target_link_libraries(test_3d PRIVATE cvx_game cvx_game_env m)
add_test(NAME render_3d COMMAND test_3d)

add_executable(test_movie port/tests/test_movie.c)
target_link_libraries(test_movie PRIVATE cvx_game cvx_game_env m)
add_test(NAME movie_make_image
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/port/tests/make_test_iso.py
                 ${CMAKE_BINARY_DIR}/test_movie.iso ${CMAKE_SOURCE_DIR}/port/tests/data/test.pss)
add_test(NAME movie COMMAND test_movie)
set_tests_properties(movie PROPERTIES
    DEPENDS movie_make_image
    ENVIRONMENT CVX_ISO=${CMAKE_BINARY_DIR}/test_movie.iso)

add_executable(test_snddrv port/tests/test_snddrv.c)
target_link_libraries(test_snddrv PRIVATE cvx_host m)
add_test(NAME sound_driver COMMAND test_snddrv)

add_executable(test_sound port/tests/test_sound.c)
target_link_libraries(test_sound PRIVATE cvx_game cvx_game_env m)
add_test(NAME sound COMMAND test_sound)
set_tests_properties(sound PROPERTIES
    DEPENDS disc_make_image
    ENVIRONMENT CVX_ISO=${CMAKE_BINARY_DIR}/test_disc.iso)
