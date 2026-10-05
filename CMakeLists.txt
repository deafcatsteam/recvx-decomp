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
project(cvx_pc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Forcing a 32-bit build (-m32)")
        add_compile_options(-m32)
        add_link_options(-m32)
    elseif(MSVC)
        message(FATAL_ERROR "Configure a 32-bit build: cmake -A Win32 ...")
    endif()
endif()

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

# Game files whose PS2 version relies on inline assembly or hardware and that
# the port replaces with its own implementation (see port/README.md).
set(GAME_REPLACED
    expand.c
    face_bh.c
    njplus.c
    objitm.c
    ps2_MovieFunc.c
    ps2_NaDraw2D.c
    ps2_NaMath.c
    ps2_NaMatrix.c
    ps2_NaView.c
    ps2_NinjaCnk.c
    ps2_Vu1Scissor2.c
    ps2_Vu1Strip.c
    ps2_dummy.c
    ps2_loadtim2.c
)

file(GLOB GAME_SOURCES ${GAME_DIR}/*.c)
foreach(f ${GAME_REPLACED})
    list(REMOVE_ITEM GAME_SOURCES ${GAME_DIR}/${f})
endforeach()

# PC implementations of the replaced files (only those written so far).
set(PORT_SOURCES
    port/src/ninja/pc_NaMath.c
    port/src/ninja/pc_NaMatrix.c
)

add_library(cvx_game STATIC ${GAME_SOURCES} ${PORT_SOURCES})
target_include_directories(cvx_game PUBLIC
    ${CMAKE_SOURCE_DIR}/port/include/sdk
    ${CMAKE_SOURCE_DIR}/src/cri/mwlib/ee/lib/libadxe
    ${GAME_DIR}
    ${CMAKE_SOURCE_DIR}/include/ps2/veronica/prog
    ${CRI_INC}
    ${CMAKE_SOURCE_DIR}/include/recvx-decomp-cri/cri/mwlib/ee/include
)
# The Katana SDK ships its own libc headers for the Dreamcast compiler; search
# it after the system so the host's <math.h>, <string.h>, ... win.
target_compile_options(cvx_game PUBLIC
    "SHELL:-idirafter ${CASE_DIR}" "SHELL:-idirafter ${KATANA_INC}")
target_compile_options(cvx_game PUBLIC
    "SHELL:-include ${CMAKE_SOURCE_DIR}/port/include/port_prefix.h"
    # Decompiled code is full of K&R-style declarations and implicit casts.
    -w
    -fno-strict-aliasing
    -fwrapv
)

enable_testing()

add_executable(test_math port/tests/test_math.c)
target_link_libraries(test_math PRIVATE cvx_game m)
add_test(NAME math COMMAND test_math)
