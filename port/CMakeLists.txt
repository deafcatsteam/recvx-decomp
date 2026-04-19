cmake_minimum_required(VERSION 3.20)
project(recvx_pc C CXX)

# ----------------------------------------------------------------------------
# Toolchain
# ----------------------------------------------------------------------------
# We target MSVC x64. PS2 EE uses 32-bit pointers so file-backed structs
# with pointer fields need reformatters post-load, and (int)/(u32) casts
# on pointers must be audited. These are handled case-by-case rather than
# globally by pointer width.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING
        "RECVX PC port expects a 64-bit build. "
        "Configure with: cmake -A x64  (or use an x64 toolchain).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(MSVC)
    add_compile_options(/W3 /wd4244 /wd4267 /wd4305 /wd4018 /wd4101 /wd4133)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# Port owns this preprocessor define so decomp source can branch on it later
add_compile_definitions(RECVX_PC_PORT=1)

# ----------------------------------------------------------------------------
# Paths
# ----------------------------------------------------------------------------
set(RECVX_ROOT       ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RECVX_SRC        ${RECVX_ROOT}/src)
set(RECVX_INCLUDE    ${RECVX_ROOT}/include)
set(RECVX_KATANA_INC ${RECVX_INCLUDE}/recvx-decomp-katana/KATANA/Include)
set(RECVX_PS2SDK_INC ${RECVX_INCLUDE}/recvx-decomp-ps2_sdk/usr/local/sce/ee/include)
set(RECVX_PS2COMMON  ${RECVX_INCLUDE}/recvx-decomp-ps2_sdk/usr/local/sce/common/include)
set(RECVX_CRI_INC    ${RECVX_INCLUDE}/recvx-decomp-cri/cri/mwlib/include)
set(RECVX_GAME_INC   ${RECVX_INCLUDE}/ps2/veronica/prog)

# ----------------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------------
# SDL2 + FFmpeg are expected from vcpkg or system. Phase 0 makes them OPTIONAL
# so the skeleton still configures on a bare machine.
find_package(SDL2 CONFIG QUIET)
find_package(OpenGL QUIET)

# vcpkg FFMPEG port exposes these as components of a single CONFIG package.
find_package(FFMPEG COMPONENTS avformat avcodec avutil swscale swresample)

if(NOT SDL2_FOUND)
    message(STATUS "SDL2 not found — building stub-only target. Install via vcpkg: vcpkg install sdl2:x86-windows")
endif()
if(NOT OpenGL_FOUND)
    message(STATUS "OpenGL not found — renderer backend will be a no-op until installed.")
endif()
if(NOT FFMPEG_FOUND)
    message(STATUS "FFmpeg not found — FMV backend will be null.")
else()
    message(STATUS "FFmpeg found: ${FFMPEG_VERSION} (include=${FFMPEG_INCLUDE_DIRS})")
endif()

# ----------------------------------------------------------------------------
# Port stub library — implementations of every symbol the decomp links against
# but which lives in the PS2 SDK / Ninja / CRI on console.
# ----------------------------------------------------------------------------
set(PORT_STUB_SOURCES
    src/stubs/stub_log.c
    src/stubs/stub_sce_cd.c
    src/stubs/stub_sce_gs.c
    src/stubs/stub_sce_pad.c
    src/stubs/stub_sce_misc.c
    src/stubs/stub_ninja.c
    src/stubs/stub_sg.c
    src/stubs/stub_cri.c
    src/stubs/stub_sb.c
    src/stubs/stub_game.c
    src/input/input.c
)

add_library(recvx_port_stubs STATIC ${PORT_STUB_SOURCES})
# Port stubs intentionally do NOT pull in KATANA / PS2 SDK / CRI headers.
# Those headers ship their own stddef.h / size_t typedefs that clash with
# MSVC's CRT, so we keep them off the stub include path. Game source
# (added in phase 1) will get them via the game-source target instead,
# isolated behind a wrapper header that masks the offending typedefs.
target_include_directories(recvx_port_stubs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ----------------------------------------------------------------------------
# Backend library — renderer / audio / input behind an abstract interface
# ----------------------------------------------------------------------------
set(BACKEND_SOURCES
    src/backend/backend.c
    src/backend/backend_gl.c
    src/backend/backend_null.c
)
add_library(recvx_backend STATIC ${BACKEND_SOURCES})
target_include_directories(recvx_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(SDL2_FOUND)
    target_link_libraries(recvx_backend PUBLIC SDL2::SDL2 SDL2::SDL2main)
    target_compile_definitions(recvx_backend PUBLIC RECVX_HAVE_SDL2=1)
endif()
if(OpenGL_FOUND)
    target_link_libraries(recvx_backend PUBLIC OpenGL::GL)
    target_compile_definitions(recvx_backend PUBLIC RECVX_HAVE_GL=1)
endif()

# ----------------------------------------------------------------------------
# ISO + FMV libraries
# ----------------------------------------------------------------------------
add_library(recvx_iso STATIC
    src/iso/iso9660.c
)
target_include_directories(recvx_iso PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# AFS reader + boot-time glue that replaces sdfunc.c's file-request surface.
# afs_mount.c defines PatId / MountSoundAfs / RequestReadInsideFile /
# GetInsideFileSize / GetReadFileStatus — same signatures as sdfunc.c but
# backed by loose AFS files on disk.
add_library(recvx_afs STATIC
    src/afs/afs.c
    src/afs/afs_mount.c
)
target_include_directories(recvx_afs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(recvx_afs PUBLIC recvx_port_stubs)

if(FFMPEG_FOUND)
    set(FMV_SOURCES src/fmv/fmv_ffmpeg.c)
else()
    set(FMV_SOURCES src/fmv/fmv_null.c)
endif()
add_library(recvx_fmv STATIC ${FMV_SOURCES})
target_include_directories(recvx_fmv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(FFMPEG_FOUND)
    target_include_directories(recvx_fmv PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(recvx_fmv PUBLIC ${FFMPEG_LIBRARIES})
    target_compile_definitions(recvx_fmv PUBLIC RECVX_HAVE_FFMPEG=1)
endif()
target_link_libraries(recvx_fmv PUBLIC recvx_iso)

# ----------------------------------------------------------------------------
# Phase 0 executable: spins up the backend, walks a fake frame loop.
# Once Phase 1 lands we will swap in src/ps2/veronica/prog/*.c as a game lib
# and replace the loop with njUserInit/njUserMain/njUserExit.
# ----------------------------------------------------------------------------
add_executable(recvx_pc src/main_pc.c)
target_link_libraries(recvx_pc PRIVATE
    recvx_backend
    recvx_iso
    recvx_fmv
    recvx_afs
    recvx_port_stubs
)
target_include_directories(recvx_pc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(recvx_pc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ----------------------------------------------------------------------------
# Game source library (phase 2 — currently opt-in via -DRECVX_BUILD_GAME=ON)
#
# Wiring strategy once enabled:
#   1. Force-include recvx_game_prelude.h on every game .c (MSVC /FI,
#      GCC/Clang -include) so KATANA's stdlib headers are masked.
#   2. Add KATANA/PS2-SDK/CRI header dirs to THIS target only, keeping
#      them off port stubs (which already compiled clean without them).
#   3. Start with src/ps2/veronica/prog/main.c only, expand file-by-file
#      as each missing symbol is shimmed or brought in.
# ----------------------------------------------------------------------------
option(RECVX_BUILD_GAME "Compile decomp game source (phase 2+)" ON)

if(RECVX_BUILD_GAME)
    set(RECVX_GAME_SOURCES
        ${RECVX_SRC}/ps2/veronica/prog/main.c
        ${RECVX_SRC}/ps2/veronica/prog/system.c
        ${RECVX_SRC}/ps2/veronica/prog/adv.c
        ${RECVX_SRC}/ps2/veronica/prog/padman.c
        ${RECVX_SRC}/ps2/veronica/prog/vibman.c
        # Port-side nj* texture stubs that need the real ninjastr.h layout.
        # Lives in the game target (not recvx_port_stubs) so it gets prelude
        # force-include + KATANA include path, matching decomp struct offsets.
        ${CMAKE_CURRENT_SOURCE_DIR}/src/game_texture_stubs.c
        # Expand this list incrementally as each file compiles.
    )

    add_library(recvx_game STATIC ${RECVX_GAME_SOURCES})
    # compat/ MUST come before the real PS2 SDK / MWCC include dirs so our
    # MSVC-safe shadows for PREFIX_PS2*.h / eetypes.h / libcdvd.h / etc.
    # win the include-path race. KATANA ninja headers stay real.
    # PRIVATE: these header dirs (KATANA especially) must NOT leak to
    # consumers like recvx_pc — KATANA's stddef.h clashes with MSVC CRT.
    target_include_directories(recvx_game
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/include/compat
            ${RECVX_GAME_INC}
            ${RECVX_KATANA_INC}
            ${RECVX_CRI_INC}
    )
    target_compile_definitions(recvx_game PRIVATE
        # Ninja headers key off these; set them so we don't cascade into
        # MWCC-specific paths inside ninja.h.
        _NJ_PS2_ _SCE_EE_
        # Pre-set KATANA's CRT shadow-header guards so any <stddef.h> /
        # <stdio.h> / etc. that resolves to KATANA/Include/ becomes a no-op
        # and we fall through to the real MSVC CRT types.
        _STDDEF_SHC _STDIO_SHC _STDLIB_SHC _STRING_SHC
        _MATH_SHC _FLOAT_SHC _LIMITS_SHC
    )
    # Put the MSVC CRT include dir AHEAD of our own so <stddef.h> / <stdio.h>
    # resolve to MSVC's real headers before the KATANA dir gets a chance to
    # shadow them. CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES is auto-populated
    # from the compiler at configure time.
    if(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES)
        target_include_directories(recvx_game BEFORE PRIVATE
            ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES})
    endif()
    if(MSVC)
        # MSVC doesn't understand GCC's `__attribute__((aligned(N)))` syntax
        # on globals, and the decomp has a few. Give it a benign alias.
        target_compile_options(recvx_game PRIVATE /wd4005 /wd4244 /wd4267
            /wd4305 /wd4133 /wd4101 /wd4018)
    endif()
    # Force-include the prelude that masks KATANA's CRT shadow headers.
    if(MSVC)
        target_compile_options(recvx_game PRIVATE
            /FI${CMAKE_CURRENT_SOURCE_DIR}/include/recvx_game_prelude.h)
    else()
        target_compile_options(recvx_game PRIVATE
            -include ${CMAKE_CURRENT_SOURCE_DIR}/include/recvx_game_prelude.h)
    endif()
    target_link_libraries(recvx_game PUBLIC recvx_port_stubs recvx_afs)
    # MSVC 2015+ inlined the printf/sprintf/scanf family into stdio.h.
    # Code compiled without the inline header visible (system.c triggers
    # a C4013 "sprintf assumed extern int") emits direct calls to the
    # old out-of-line names. legacy_stdio_definitions.lib ships those
    # symbols as forwards into the inline impls, satisfying the linker.
    if(MSVC)
        target_link_libraries(recvx_game PUBLIC legacy_stdio_definitions)
    endif()

    target_link_libraries(recvx_pc PRIVATE recvx_game)
    message(STATUS "RECVX PC port: game source ENABLED (phase 2+)")
else()
    message(STATUS "RECVX PC port: game source DISABLED (pass -DRECVX_BUILD_GAME=ON to enable)")
endif()

message(STATUS "RECVX PC port configured.")
