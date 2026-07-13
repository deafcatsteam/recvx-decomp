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
    # Strip /RTC1 from CMake's default Debug flags. PS2 decomp has if/else
    # chains that leave locals uninit on paths the original game never
    # exercised (e.g. KazuSet's `flg` switch only handles 0/1) — /RTCu
    # (the uninit-read check inside /RTC1) fires a runtime popup on those
    # false positives. /RTCs (stack frame check) goes too; not worth the
    # split since the codebase is small and CRT heap checks still apply.
    foreach(flag_var CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
        string(REGEX REPLACE "/RTC[^ ]*" "" ${flag_var} "${${flag_var}}")
    endforeach()
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
find_package(SDL2_image CONFIG QUIET)
find_package(SDL2_ttf CONFIG QUIET)
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
    # Real C port of the room-load LZSS decompressor (decomp expand.c is
    # pure MIPS asm). Needed by system.c's room state machine (rdtsz).
    src/expand_pc.c
    src/input/input.c
    # Low-4GiB allocator for the NJS_TEXMEMLIST pool. Split out from
    # game_texture_stubs.c because <windows.h>'s POINT struct collides
    # with types.h's POINT when both are included in the same TU.
    src/tex_pool_alloc.c
    # Debug-only TGA dumper (RECVX_DUMP_TEX=1). Lives here, not in the
    # game target, because the recvx_game_prelude.h /FI guards (set to
    # mask KATANA's SH-series stdio) leave FILE undeclared in that TU.
    src/tex_dump.c
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
# ADX audio streaming (CRI ADX → S16 PCM via libavcodec) for PlayAdx paths
# in the decomp. Needs FFmpeg like the FMV target.
# ----------------------------------------------------------------------------
add_library(recvx_audio STATIC
    src/audio/adx_player.c
    src/audio/msa_parser.c
)
target_include_directories(recvx_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(recvx_audio PUBLIC recvx_afs recvx_backend recvx_port_stubs)
if(FFMPEG_FOUND)
    target_include_directories(recvx_audio PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(recvx_audio PUBLIC ${FFMPEG_LIBRARIES})
    target_compile_definitions(recvx_audio PRIVATE RECVX_HAVE_FFMPEG=1)
endif()

# ----------------------------------------------------------------------------
# Phase 0 executable: spins up the backend, walks a fake frame loop.
# Once Phase 1 lands we will swap in src/ps2/veronica/prog/*.c as a game lib
# and replace the loop with njUserInit/njUserMain/njUserExit.
# ----------------------------------------------------------------------------
add_executable(recvx_pc src/main_pc.c src/gallery.c)
target_link_libraries(recvx_pc PRIVATE
    recvx_backend
    recvx_iso
    recvx_fmv
    recvx_audio
    recvx_afs
    recvx_port_stubs
)
target_include_directories(recvx_pc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(SDL2_image_FOUND)
    target_link_libraries(recvx_pc PRIVATE $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>)
    target_compile_definitions(recvx_pc PRIVATE RECVX_HAVE_SDL2_IMAGE=1)
endif()
if(SDL2_ttf_FOUND)
    target_link_libraries(recvx_pc PRIVATE $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>)
    target_compile_definitions(recvx_pc PRIVATE RECVX_HAVE_SDL2_TTF=1)
endif()

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
    target_compile_definitions(recvx_pc PRIVATE RECVX_BUILD_GAME=1)
    target_compile_definitions(recvx_afs PRIVATE RECVX_BUILD_GAME=1)
    target_compile_definitions(recvx_port_stubs PRIVATE RECVX_BUILD_GAME=1)
    set(RECVX_GAME_SOURCES
        ${RECVX_SRC}/ps2/veronica/prog/main.c
        ${RECVX_SRC}/ps2/veronica/prog/system.c
        ${RECVX_SRC}/ps2/veronica/prog/adv.c
        ${RECVX_SRC}/ps2/veronica/prog/padman.c
        ${RECVX_SRC}/ps2/veronica/prog/vibman.c
        # Tiny utilities — bit-flag helpers used by adv.c and game.c. 100%
        # matched, zero deps; replaces the void* stubs in stub_game.c.
        ${RECVX_SRC}/ps2/veronica/prog/flag.c
        # File-view sub-screen of the inventory (FILE tab). 21/23 funcs
        # 100% matched. The 5 sub1.c/message.c symbols it calls
        # (ItemSearch, EraseItem, NameChangeSet, bhDispItemName,
        # bhSetMessage) are stubbed in stub_game.c — fileview compiles
        # in by itself, just doesn't have item state to read against
        # until sub1.c follows.
        ${RECVX_SRC}/ps2/veronica/prog/fileview.c
        # Item-view 3D model preview (zoom-on-pickup). 21/23 matched.
        # Stubs out 26 ninja matrix/draw + model-loading symbols in
        # stub_ninja.c — itemview compiles, but the 3D rendering
        # surface is no-op until njCnk* / matrix stack are implemented.
        ${RECVX_SRC}/ps2/veronica/prog/itemview.c
        # Inventory main loop (StatusMain + StatusInit + cursor / combine
        # / use logic). 31/72 funcs 100%-matched upstream — partial. The
        # stubs added above (Ps2DrawOTag/Sub, Z-buff toggles, SyncPath,
        # njControl3D / Sprite2D / SetConstantAttr / Material / FogColor,
        # njUserClipping, scePrintf) cover all its external calls. A
        # PS2-only WORKBASE register block in Ps2ZbuffOffI is gated
        # behind #ifdef RECVX_PC_PORT.
        ${RECVX_SRC}/ps2/veronica/prog/sub1.c
        # Item data tables (combidata, itemdata, dsptbl, itemflg,
        # itemidtbl, getmestbl, idsettbl, hoseipos, getbulletmax, c_a,
        # mestbl, curedata). 697 lines of pure const arrays — no funcs
        # to match, no external deps, perfectly portable.
        ${RECVX_SRC}/ps2/veronica/prog/item.c
        # PS2 texture-memory loader. 11/12 funcs matched. Replaces our
        # stub bhSetMemPvpTexture with the real impl that walks chunked
        # binary blobs (PLI palette + TIM2 pictures) and feeds each to
        # njSetTextureInfo / njLoadTexture — that's what gets the
        # inventory's actual textures into our pool. The remaining
        # functions (bhCopy*, bhPushAllTexture, etc.) handle texmem
        # eviction/restoration which our GL backend doesn't need.
        ${RECVX_SRC}/ps2/veronica/prog/ps2_texture.c
        # ISO file-routing table: dvd_image_data[] maps each AFS partition
        # to its filename, and rdx_files[205] names every room file inside
        # RDX_LNK.AFS so RequestReadIsoFile("rm_NNNN.rdx") can find the
        # right AFS entry. Pure data, no function bodies.
        ${RECVX_SRC}/ps2/veronica/prog/ps2_dvd_image.c
        # NOT pulling effect.c -- it transitively requires ~250 bhEff*
        # particle handlers, 3D model functions, and ninja math primitives
        # that aren't in our build.
        # Text/font rendering: bhDispItemName, bhDispMessage, bhDispFont,
        # bhDispTime + bhSetMessage. Drives the bottom item-name strip
        # ("Grenade Launcher"), status messages, and the inventory's
        # font glyphs. Reads from sys->mes_ip (sysmes.ald text bundle)
        # and sys->ef_tlist (font texture atlas) -- both need to be
        # loaded for real text to render. Until then most calls
        # NULL-deref-protect.
        ${RECVX_SRC}/ps2/veronica/prog/message.c
        # Port-side nj* texture stubs that need the real ninjastr.h layout.
        # Lives in the game target (not recvx_port_stubs) so it gets prelude
        # force-include + KATANA include path, matching decomp struct offsets.
        ${CMAKE_CURRENT_SOURCE_DIR}/src/game_texture_stubs.c
        # Phase 1 of 3D rendering: port-side Sega Ninja matrix stack +
        # transformation math. The real ps2_NaMatrix.c uses PS2 EE/VU0
        # inline asm we can't compile on MSVC x64, so we reimplement the
        # API in standard C against the NJS_MATRIX = float[16] layout.
        # Replaces the matrix stubs in stub_ninja.c with real impls.
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ninja_3d.c
        # Phase 3 of Path B: chunked-model renderer. Walks NJS_CNK_MODEL
        # vlist/plist chunks and emits triangles via recvx_gfx_draw_tri3d.
        # Replaces the njCnkEasyMultiDrawModel stub in stub_ninja.c so
        # inventory EXAMINE (book / lighter) actually renders the model.
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ninja_cnk.c
        # Real model-binary parser. 100% matched and self-contained;
        # parses MLB blobs into NJS_CNK_OBJECT trees so itemview.c's
        # bhMlbBinRealize call no longer hands wild pointers to
        # downstream draw code.
        ${RECVX_SRC}/ps2/veronica/prog/binfunc.c
        # Controller rumble/vibration actuator simulation. 100% matched,
        # self-contained, no PS2 asm — just scePad* SDK calls (stubbed in
        # stub_sce_pad.c). Replaces stub_game.c's Pad_act/pdVibMx* stubs,
        # which had wrong type/size and signatures vs. what system.c and
        # vibman.c (both already compiled) actually expect.
        ${RECVX_SRC}/ps2/veronica/prog/ps2_sg_pdvib.c
        # Room/RDT loader: bhSetRoom parses the decompressed rm_*.rdx
        # (header + camera cuts + models + motions + textures). The PS2
        # in-place 32-bit pointer relocation is replaced by an explicit
        # x64 conversion under RECVX_PC_PORT (ROM_WORK header fields +
        # a converted CUT_WORK array copy). Replaces the bhSetRDT /
        # bhInitReadRDT / bhFinishRoom / bhPushGameData stubs.
        ${RECVX_SRC}/ps2/veronica/prog/room.c
        # Object/item slot setup + control (bhSetObject/bhSetItem write
        # into sys->obwp/itwp O_WRK slots; room.c calls them per entity).
        ${RECVX_SRC}/ps2/veronica/prog/objitm.c
        # Enemy slot setup (bhSetEnemy returns the BH_PWORK slot room.c's
        # bhSetEneMdl fills in). One x64 pointer-truncation fixed in
        # bhEne_CallocWork (same ALIGN pattern as system.c).
        ${RECVX_SRC}/ps2/veronica/prog/eneset.c
        # Sega Ninja line/plane/sphere collision math. Zero PS2 asm —
        # pure C, needed by hitchk-adjacent code and room placement.
        ${RECVX_SRC}/ps2/veronica/prog/ps2_NaColi.c
        # Room-geometry / floor / entity-vs-entity collision. Zero PS2 asm —
        # confirmed via direct grep (no asm/__asm__/vcallms in the file).
        # Every external symbol it calls is already real and compiled
        # (njSqrt/njUnitVector/njInnerProduct/njScalor/njSinCos in
        # ninja_3d.c; njDistanceP2L/njDistanceL2PL/njCollisionCheckBS in
        # ps2_NaColi.c; bhSetMessage in message.c) except bhCheckFloorNum
        # and bhSetEffect, which fall back to their existing no-op stubs
        # in game_room_stubs.c (pwksub.c/effect.c still uncompiled) —
        # non-blocking placeholders, not crashes. Replaces 16 shadowing
        # no-op stubs previously in game_room_stubs.c (see git history).
        ${RECVX_SRC}/ps2/veronica/prog/hitchk.c
        # Player state machine + init (bhInitPlayer allocates plp->exp0/1/3
        # — bhFinishRoom dereferences exp0 unconditionally). Zero PS2 asm.
        ${RECVX_SRC}/ps2/veronica/prog/player.c
        # Player/weapon model+motion parsing after the SYSTEM.AFS reads
        # (bhReadPlayerData fills plp->mlwP that bhInitRoomChangePlayer
        # dereferences). 7 x64 pointer-truncation aligns fixed.
        ${RECVX_SRC}/ps2/veronica/prog/dread.c
        # Main gameplay sequence + room draw. bhSysCallGame (system.c,
        # already compiled) calls bhMainSequence; bhAllDrawModel draws
        # rom->mdl.objP through njCnkEasyMultiDrawObject (ninja_cnk.c).
        ${RECVX_SRC}/ps2/veronica/prog/game.c
        # View matrix build (bhControlCamera fills cam.mtx from
        # cam.px/ay/... every frame). cmmat/crmat globals stubbed in
        # stub_game.c (real ones live in the uncompiled ps2_dummy.c).
        ${RECVX_SRC}/ps2/veronica/prog/camera.c
        # Digital/analog pad decode (bhSetPad): reads njGetPeripheral(0)
        # (real, SDL-backed — port/src/input/input.c) into sys->pad_on/
        # pad_ax/pad_ay/etc that bhControlPlayer (player.c) reads every
        # frame. pd_port forced to 0 in main_pc.c (see comment there) —
        # bypasses sync.c's async pad-DMA-polling bhCheckPadPort, which
        # we don't compile.
        ${RECVX_SRC}/ps2/veronica/prog/pad.c
        # Per-entity model draw (bhPutModel/bhCalcModel/bhCalcTree) —
        # player/enemy/object draw path, as opposed to bhAllDrawModel's
        # whole-room draw. bhCalcModel populates owP->mtx per node,
        # which bhEne00 (eneset.c) reads right after calling it —
        # without this file compiled, owP was never set and enemy AI
        # dereferenced garbage the moment an enemy activated.
        ${RECVX_SRC}/ps2/veronica/prog/MdlPut.c
        # RE fixed-camera cut system — picks the camera (cam.px/ay/cut)
        # from the converted CUT_WORK array based on player position.
        ${RECVX_SRC}/ps2/veronica/prog/cut.c
        # Per-room directional + point light selection (bhControlLight/
        # bhSetLight/bhSetHalfLight/bhInitLight/bhSetLightTab/
        # bhSetEasyDirLight/bhGetLightVector). Zero PS2 asm — pure C game
        # logic; the actual VU0-bound work is njCnkSetEasyLight* etc,
        # already real in game_room_stubs.c/ninja_cnk.c. Needed
        # njProjectScreen + fNaViwOffsetX/Y added to ninja_3d.c (CPU
        # reimplementation of the point-light visibility cull — see
        # comment there) and njCnkSetSimpleLight/Color/Intensity added
        # to game_room_stubs.c (previously declared but never defined —
        # nothing called them before this file). Replaces 7 shadowing
        # no-op stubs previously in game_room_stubs.c (see git history).
        # Point-light (Multi) rendering itself stays stubbed no-op —
        # out of scope for this pass, see ninja_cnk.c's cnk_shade_vertex.
        ${RECVX_SRC}/ps2/veronica/prog/light.c
        # Weapon system (bhSetWeapon/bhObjWpn/bhActionWeapon/bullet count/
        # gun+knife+bomb+bow hit checks/splash/explosion). Zero PS2 asm —
        # confirmed via grep. Replaces 15 shadowing no-op stubs previously
        # in game_room_stubs.c (see git history).
        ${RECVX_SRC}/ps2/veronica/prog/weapon.c
        # Punch minigame + weapon aim/lock-on core (PlyPchInit/PlyPchMain/
        # bhCPM2_act_*_pch/bhCPM2_SearchPch/bhArmIkMdk). Zero PS2 asm.
        # Compiled alongside weapon.c since playpch.c's CheckGunHit and
        # weapon.c's gun/knife atari checks share the same GA_WORK/WPN_TAB
        # combat plumbing.
        ${RECVX_SRC}/ps2/veronica/prog/playpch.c
        # Capsule/sphere collision math extracted from njplus.c (see file
        # header comment for exactly which lines/why — njplus.c as a whole
        # has 12 VU0 asm blocks elsewhere, these 4 functions aren't among
        # them). Needed by weapon.c's gun/knife/bomb/capsule hit checks.
        ${CMAKE_CURRENT_SOURCE_DIR}/src/njplus_coli.c
        # Player-work helpers: speed integration (bhAddSpeed — was
        # extracted into game_room_stubs.c as a stopgap, now real here),
        # enemy search (bhSearchNearEnemy/2/B, bhSearchNextEnemy), floor/
        # wall queries also referenced by hitchk.c/Task 1
        # (bhCheckFloorNum/bhCheckL2Wall/bhSetFloorNum/bhCheckClipModel —
        # those had fallen back to their hitchk.c-era no-op stubs per that
        # task's CMakeLists comment, now real), water splash, gun fire/
        # magazine/yakkyou setup, and the capsule-vs-wall check weapon.c's
        # hit tests need (bhCheckC2WallN). Zero PS2 asm — confirmed via
        # grep. Replaces 16 shadowing no-op stubs previously in
        # game_room_stubs.c (see git history).
        ${RECVX_SRC}/ps2/veronica/prog/pwksub.c
        # Effect-drawing sub-functions (gun sparks, etc.) that weapon.c's
        # hit-detection calls into (bhSetEffGunSpark). Zero PS2 asm. Head
        # start on the Task 4.2 effects subsystem — the rest of it
        # (effect.c, effsub2.c) lands as its own task.
        ${RECVX_SRC}/ps2/veronica/prog/effsub3.c
        # Motion blend/speed-curve helpers (bhSetMotion/SetMtnFast(Hokan)/
        # SetMtnNormal(Hokan)/SetMtnSlow(Hokan)/AngZyxToYzx/AngYzxToZyx/
        # bhCalcFixOffset/bhFixPosition/bhGetObjMotion). Zero PS2 asm.
        # Replaces 4 shadowing no-op stubs previously in
        # game_room_stubs.c, and the AngZyxToYzx/AngYzxToZyx verbatim
        # copies temporarily added to ninja_3d.c for weapon.c/playpch.c
        # (Task 4.1) — this is their real, complete home.
        ${RECVX_SRC}/ps2/veronica/prog/Motion.c
        # Enemy AI: bhEne28 lives here (shared "sub-placement" logic used
        # by several small enemies) alongside its helpers (bhSub_DirTarget/
        # bhSubpl/em60_init/em_sce/init_subpl/move_subpl/mv00_subpl*).
        # Zero PS2 asm. First Task 4.3 file (Enemy AI roster).
        ${RECVX_SRC}/ps2/veronica/prog/subpl.c
        # Enemy AI: bhEne71 (smallest enemy file at 114 lines — used as
        # the Task 4.3 proof-of-pattern integration). Zero PS2 asm.
        ${RECVX_SRC}/ps2/veronica/prog/en71.c
        # Enemy AI shared helper library: wall/floor/water collision
        # wrappers, blood/fire/particle effect setup, motion-change
        # helpers (bhEne_ChgMtn/bhEne_CheckDirWall*/bhEne_CollisionCheck-
        # Wall*/bhEne_SetBlood*/etc) called by most enemy behavior files,
        # not just en54/en55. Zero PS2 asm.
        ${RECVX_SRC}/ps2/veronica/prog/zonzon.c
        # Enemy AI: bhEne54/bhEne55 (271 lines each, near-identical small
        # enemies). Zero PS2 asm.
        ${RECVX_SRC}/ps2/veronica/prog/en54.c
        ${RECVX_SRC}/ps2/veronica/prog/en55.c
        # Enemy AI shared helper library #2: SE/voice calls, blood/
        # mince/acid effect variants, damage calc, arrow markers
        # (bhEne_CallSE/bhEne_SetBloodEffect*/bhEne_CalcDamage/etc).
        # Zero PS2 asm. Needed njRotateEx/njScaleEx/njTranslateEx added
        # to ninja_3d.c (njRotateEx already pure C in the decomp;
        # njScaleEx/njTranslateEx are VU0 asm but identical math to the
        # already-real njScale/njTranslate, just NJS_VECTOR-arg wrappers).
        ${RECVX_SRC}/ps2/veronica/prog/zonzon1.c
        # Line/segment-vs-geometry collision helpers (distinct from
        # hitchk.c's box/wall checks) — bhCollisionCheckLine*/
        # bhGetHitCollisionNormal/bhInOutCheck. Zero PS2 asm.
        ${RECVX_SRC}/ps2/veronica/prog/hitchkl.c
        # Enemy AI: bhEne20/bhEne10/bhEne27. Zero PS2 asm.
        ${RECVX_SRC}/ps2/veronica/prog/en20.c
        ${RECVX_SRC}/ps2/veronica/prog/en10.c
        ${RECVX_SRC}/ps2/veronica/prog/en27.c
        # Enemy AI: bhEne08. Zero PS2 asm, 323 lines. Its one en03.c
        # dependency (bhEne03_Collision) is UNIMPLEMENTED in the original
        # decomp too (en03.c:5046, scePrintf + no return) — stubbed in
        # game_room_stubs.c instead of pulling in en03.c's other 6671
        # lines for a dead function.
        ${RECVX_SRC}/ps2/veronica/prog/en08.c
        # Enemy AI: bhEne16. Zero PS2 asm, fully self-contained (no
        # external calls besides scePrintf).
        ${RECVX_SRC}/ps2/veronica/prog/en16.c
        # Enemy AI: bhEne24, bhEne25. Zero PS2 asm, both fully
        # self-contained (no external calls besides scePrintf).
        ${RECVX_SRC}/ps2/veronica/prog/en24.c
        ${RECVX_SRC}/ps2/veronica/prog/en25.c
        # Enemy AI: bhEne11, bhEne30. Zero PS2 asm, both fully
        # self-contained (no external calls besides scePrintf).
        # (en01b.c deferred — it's a helper file for bhEne01, whose main
        # file en01.c is 9945 lines, its own future task.)
        ${RECVX_SRC}/ps2/veronica/prog/en11.c
        ${RECVX_SRC}/ps2/veronica/prog/en30.c
        # Enemy AI: bhEne12, bhEne13, bhEne14, bhEne18, bhEne21, bhEne29.
        # All zero PS2 asm, all fully self-contained (no external calls
        # besides scePrintf and their own functions).
        ${RECVX_SRC}/ps2/veronica/prog/en12.c
        ${RECVX_SRC}/ps2/veronica/prog/en13.c
        ${RECVX_SRC}/ps2/veronica/prog/en14.c
        ${RECVX_SRC}/ps2/veronica/prog/en18.c
        ${RECVX_SRC}/ps2/veronica/prog/en21.c
        ${RECVX_SRC}/ps2/veronica/prog/en29.c
        # Enemy AI: bhEne02, bhEne04, bhEne05, bhEne06, bhEne07, bhEne09,
        # bhEne17, bhEne19, bhEne22, bhEne23, bhEne26. All zero PS2 asm,
        # all self-contained except en05.c/en07.c which need
        # bhCalcModel (MdlPut.c)/bhCheckEnemies/bhCheckPlayer (hitchk.c)/
        # bhSetMotion (Motion.c)/bhEne_GetPartsPos/bhEne_SetWeponAtr
        # (zonzon1.c) — all already real. (en15.c deferred — its
        # top-level helpers use unprefixed generic names like Init/
        # Move/Die/Attack/Damage/Chase, higher collision risk than the
        # bhEneNN_-prefixed pattern everything else follows; wants its
        # own careful pass.)
        ${RECVX_SRC}/ps2/veronica/prog/en02.c
        ${RECVX_SRC}/ps2/veronica/prog/en04.c
        ${RECVX_SRC}/ps2/veronica/prog/en05.c
        ${RECVX_SRC}/ps2/veronica/prog/en06.c
        ${RECVX_SRC}/ps2/veronica/prog/en07.c
        ${RECVX_SRC}/ps2/veronica/prog/en09.c
        ${RECVX_SRC}/ps2/veronica/prog/en17.c
        ${RECVX_SRC}/ps2/veronica/prog/en19.c
        ${RECVX_SRC}/ps2/veronica/prog/en22.c
        ${RECVX_SRC}/ps2/veronica/prog/en23.c
        ${RECVX_SRC}/ps2/veronica/prog/en26.c
        # Enemy AI: bhEne15, bhEne53 (both dispatch entries live in this
        # one file). Zero PS2 asm. Uses several unprefixed generic
        # helper names (Init/Move/Die/Attack/Damage/Chase/Stand/etc,
        # all non-static) — checked against the whole prog/ and port/src
        # tree for name collisions before adding: none found.
        ${RECVX_SRC}/ps2/veronica/prog/en15.c
        # Enemy AI: bhEne01. Zero PS2 asm, 9945 lines (biggest enemy
        # file). All external calls already real (bhCheckEnemies/
        # bhCheckFloorEffect/bhCheckPlayer/bhCheckWater/
        # bhClrUseKaidanFlag from hitchk.c; bhEne_CallocWork/
        # bhEne_SetCallFunc/bhSetEnemy from eneset.c; bhSetMotion from
        # Motion.c; njDistanceP2P from ps2_NaColi.c; bhSetShadow is the
        # existing effect.c no-op stub) except npSetAllMatColor, added
        # to njplus_coli.c above. Also needs its two helper files:
        # en01sub.c (3202 lines, zero asm, fully self-contained —
        # defines bhEne01Head/Arm/Leg/Cap/Worm/Bom/Scope/Parent) and
        # en01b.c (1983 lines, zero asm, self-contained — defines the
        # bhEne01_*TypeB dispatch arrays + bhEne01_RotNeck).
        ${RECVX_SRC}/ps2/veronica/prog/en01sub.c
        ${RECVX_SRC}/ps2/veronica/prog/en01b.c
        ${RECVX_SRC}/ps2/veronica/prog/en01.c
        # No-op stubs for the gameplay subsystems room.c/eneset.c/objitm.c
        # reference (enemy AI handlers, collision, lights, effects, player
        # weapon/motion) + real npCopyMemory/npSetMemoryL. Game target so
        # it sees real struct layouts.
        ${CMAKE_CURRENT_SOURCE_DIR}/src/game_room_stubs.c
        # Expand this list incrementally as each file compiles.
    )

    add_library(recvx_game STATIC ${RECVX_GAME_SOURCES})
    # pwksub.c's bhGetFreeMemory is a real PS2 bump allocator over a fixed
    # sys->memp/freemem pool — freemem is never defined anywhere in the
    # port (extern-only), so linking it for real would return NULL (or
    # worse) for every one of its ~18 already-compiled callers (room.c,
    # player.c, system.c, etc.), which currently get real heap memory from
    # game_texture_stubs.c's calloc-backed bhGetFreeMemory — an already
    # load-bearing design choice, not a placeholder. Rename pwksub.c's copy
    # at the preprocessor level so it compiles (matched-source coverage)
    # but never links against/replaces the one everything else calls.
    set_source_files_properties(
        ${RECVX_SRC}/ps2/veronica/prog/pwksub.c
        PROPERTIES COMPILE_DEFINITIONS "bhGetFreeMemory=bhGetFreeMemory_pwksub_unused"
    )
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
    )
    # NOTE: the CRT shadow-header guards (_STDDEF_SHC, _STRING_SHC, etc.)
    # are deliberately NOT pre-defined here. recvx_game_prelude.h (force-
    # included below) already includes the real CRT headers first and only
    # then defines those guards itself. -D flags and -include are applied
    # in command-line order by GCC, so defining the guards here (i.e.
    # before -include on the command line) would mask KATANA's shadow
    # headers before the prelude's own real #include<> runs, leaving
    # size_t/memcpy/etc undeclared. This bit MSVC too, silently: MSVC's
    # own ucrt header search never touches KATANA/Include at all, so the
    # premature defines were merely redundant there, not broken.
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
        # This decomp source was written against MWCC (a lenient C89-era
        # compiler) and previously only built with MSVC in permissive mode
        # — both tolerate implicit function declarations and int/pointer
        # mismatches as warnings, not errors. GCC 14 hardened these three
        # diagnostics to hard errors by default regardless of -std=gnuXX.
        # Downgrade back to non-fatal warnings (not fully silenced) so the
        # build doesn't halt on 1990s-style code we're not here to rewrite;
        # each one is still visible in the build log for later cleanup.
        target_compile_options(recvx_game PRIVATE
            -Wno-error=implicit-function-declaration
            -Wno-error=int-conversion
            -Wno-error=incompatible-pointer-types)
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
