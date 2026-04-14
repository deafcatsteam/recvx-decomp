cmake_minimum_required(VERSION 3.20)
project(recvx_pc C CXX)

# ----------------------------------------------------------------------------
# Toolchain sanity
# ----------------------------------------------------------------------------
# We target MSVC x86 (32-bit) so that the EE's 32-bit pointer assumptions
# baked into the decomp source remain valid without a global audit.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(WARNING
        "RECVX PC port expects a 32-bit build (sizeof(void*)==4). "
        "Configure with: cmake -A Win32 (Visual Studio) "
        "or use a 32-bit toolchain. Current sizeof(void*)=${CMAKE_SIZEOF_VOID_P}.")
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

if(NOT SDL2_FOUND)
    message(STATUS "SDL2 not found — building stub-only target. Install via vcpkg: vcpkg install sdl2:x86-windows")
endif()
if(NOT OpenGL_FOUND)
    message(STATUS "OpenGL not found — renderer backend will be a no-op until installed.")
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
)

add_library(recvx_port_stubs STATIC ${PORT_STUB_SOURCES})
target_include_directories(recvx_port_stubs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${RECVX_GAME_INC}
        ${RECVX_KATANA_INC}
        ${RECVX_PS2SDK_INC}
        ${RECVX_PS2COMMON}
        ${RECVX_CRI_INC}
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

add_library(recvx_fmv STATIC
    src/fmv/fmv_null.c
)
target_include_directories(recvx_fmv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

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
    recvx_port_stubs
)
target_include_directories(recvx_pc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(recvx_pc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

message(STATUS "RECVX PC port skeleton configured (phase 0).")
