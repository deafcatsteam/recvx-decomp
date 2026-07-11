# RE:CVX decomp + PC port — initial setup design

Date: 2026-07-11

## Goal

Get both `recvx-decomp` (PS2 decompilation) and `recvx-decomp-port` (PC port)
forked, cloned, synced, and building locally in Docker, using the ISO already
present at `/home/skitzo/Documents/Resident Evil - Code - Veronica X (USA).iso`.
Deeper contribution work (advancing decompilation %, finishing the PC port)
comes after this setup, as separate follow-up work.

## Context

- `recvx-decomp` (upstream `AshfordFamily`, branch `master`): active PS2
  decompilation project. 46.1% code matched / 66.8% functions matched as of
  2026-07-10. Builds via devcontainer (MIPS binutils + wibo) or manual
  Python+MIPS-binutils+wibo setup.
- `recvx-decomp-port` (upstream `AshfordFamily`, branch `pc-port`): PC port
  built on top of the decomp. Last updated 2026-05-26, ~100 commits behind
  `recvx-decomp` master (39.4% vs 46.1% code matched) — diverged before a lot
  of recent decompilation progress.
- User account `deafcatsteam` has read-only access to both upstream repos
  (no push). Forking is required to push/PR.
- User has Windows available but chose Docker/devcontainer as the working
  environment for this setup.
- The PC port's `CMakePresets.json` currently only defines an MSVC x64 +
  vcpkg preset — no Linux preset exists yet.
- Inspected `port/CMakeLists.txt` and `port/src/main_pc.c`: the build is
  already partly toolchain-agnostic (`if(MSVC)/else()` branches, `-include`
  fallback for the prelude header, vcpkg CONFIG-mode `find_package` calls
  that also resolve on Linux). Windows-only code is isolated but not zero:
  `main_pc.c`'s crash handler is guarded behind `#ifdef _WIN32` already, but
  `tex_pool_alloc.c` implements a low-4GiB allocator using a Windows-only API
  with no Linux equivalent yet, and `game_texture_stubs.c` also references
  `windows.h`. Both are only pulled in when `RECVX_BUILD_GAME=ON` (the
  CMakeLists default).

## Scope decision

"Finish" the projects is out of scope for now — user explicitly wants a
working setup first (build succeeds, ISO usable), with deeper contribution
(decompiling more functions, porting more game code) as later, separate work.

## Plan

### 1. Fork + clone
- Fork both repos from `AshfordFamily` to `deafcatsteam`.
- Clone both forks `--recursive` into `~/projects/recvx-decomp` and
  `~/projects/recvx-decomp-port`.
- Add an `upstream` git remote pointing at the original `AshfordFamily` repos
  in both clones, for future syncing.

### 2. Sync `recvx-decomp-port`'s `pc-port` branch
- Merge `upstream/master` (from `recvx-decomp`, ~100 commits of decompilation
  progress) into `pc-port` before doing any new port work, to avoid the
  branches diverging further and to build against the latest decompiled
  source.
- Resolve any merge conflicts that come up (expected mainly in
  `report.json`, `compile_config.json`, and possibly shared `src/`/`include/`
  files touched by both branches).

### 3. Build `recvx-decomp` in Docker
- Use the existing `.devcontainer` (MIPS binutils + wibo + Python) — no
  changes needed, it already supports Linux.
- Extract `SLUS_201.84` from the user's ISO into `config/`.
- Run `python compile.py --setup` then `python compile.py` inside the
  container to confirm the build succeeds.
- Optionally repackage the ISO with `mkiso.py` to confirm the resulting ELF
  boots under PCSX2, as an end-to-end sanity check.

### 4. Build `recvx-decomp-port` in Docker — "Phase 0" approach
- Add a Linux configure preset to `port/CMakePresets.json` (Ninja generator,
  vcpkg `x64-linux` triplet), reusing the existing devcontainer (already has
  MIPS binutils + wibo; will additionally need Ninja + vcpkg + X11/OpenGL
  dev packages installed).
- Build with `-DRECVX_BUILD_GAME=OFF` first. This excludes the
  Windows-only-dependent game target (`tex_pool_alloc.c`,
  `game_texture_stubs.c`) and builds only the Phase 0 skeleton (stubs,
  backend, ISO/AFS/FMV/audio libs, `recvx_pc` executable playing back FMV).
  This is the fastest path to a working Linux build in the chosen Docker
  environment.
- Run the resulting `recvx_pc` against the user's ISO to confirm the FMV
  playback demo works.
- Explicitly deferred (follow-up work, not part of this setup): porting
  `tex_pool_alloc.c`'s low-4GiB allocator to Linux (e.g. `mmap` with
  `MAP_32BIT`) and auditing `game_texture_stubs.c`'s `windows.h` usage, to
  enable `RECVX_BUILD_GAME=ON` on Linux.

## Success criteria

- Both repos forked under `deafcatsteam` and cloned to `~/projects/`.
- `pc-port` branch merged with latest `recvx-decomp` master, no unresolved
  conflicts.
- `recvx-decomp` builds successfully in its devcontainer against the user's
  ISO.
- `recvx-decomp-port` builds successfully in Docker with a new Linux preset,
  `RECVX_BUILD_GAME=OFF`, and runs the FMV demo against the user's ISO.

## Out of scope (follow-up work, not this setup)

- Advancing `recvx-decomp`'s decompilation percentage.
- Porting the low-4GiB allocator and `game_texture_stubs.c` to Linux so
  `RECVX_BUILD_GAME=ON` works outside MSVC.
- Any new gameplay/rendering feature work in the PC port.

## Execution outcome (2026-07-11)

Tasks 1-3 completed as planned (fork/clone, `pc-port` synced with
`recvx-decomp` master, `recvx-decomp` builds and produces `elf/main.elf`
against the user's ISO).

Task 4 (PC port Phase 0 Linux build) got the toolchain and dependencies
fully working (Ninja + vcpkg `x64-linux` preset configures cleanly; SDL2,
SDL2_image, SDL2_ttf, and FFmpeg all build from source in the devcontainer)
and fixed two real, previously-undiscovered Linux-portability bugs in the
port's always-built stub code (not gated by `RECVX_BUILD_GAME`):

- `port/src/tex_dump.c` included `<direct.h>` unconditionally for `_mkdir`
  (MSVC-only header) — now guarded behind `#ifdef _WIN32` with a POSIX
  `mkdir`-based fallback.
- `port/src/stubs/stub_ninja.c` redeclared `recvx_gfx_draw_polygon` with a
  locally-defined, field-compatible but differently-named vertex struct
  (`_recvx_gfx_vtx_local` vs. the header's `recvx_gfx_vtx`), which GCC
  rejects as a conflicting declaration (MSVC apparently tolerated it) — now
  uses the header's `recvx_gfx_vtx` type directly, removing the redundant
  duplicate declaration.

However, the build still fails at the **link** step even with
`RECVX_BUILD_GAME=OFF`: `port/src/main_pc.c`'s `--game` code path
(`run_game_loop`, calling `njUserInit`/`njUserMain`/`njUserExit`,
`InitAdvSystem`, `AdvWork`) and `port/src/afs/afs_mount.c`'s RDX lookup
(referencing `rdx_files` / `rdx_image_data_max`, defined in
`ps2_dvd_image.c`) are **not** compile-time gated on `RECVX_BUILD_GAME` —
they're only skipped at runtime via the `--game` CLI flag, so the symbols
are unconditionally referenced at link time regardless of the CMake option.
In other words, `RECVX_BUILD_GAME=OFF` is not currently a working build
configuration on any platform (it was presumably only ever exercised with
`RECVX_BUILD_GAME=ON` on Windows/MSVC) despite the option's own description
("Compile decomp game source (phase 2+)") implying an OFF state should work
standalone.

**Follow-up task — completed 2026-07-11 (later session):** added the
`RECVX_BUILD_GAME` compile-time guard so Phase 0 (`RECVX_BUILD_GAME=OFF`)
now links and runs on Linux:

- `port/CMakeLists.txt`: `target_compile_definitions(recvx_pc PRIVATE
  RECVX_BUILD_GAME=1)` and same for `recvx_afs`, inside the existing
  `if(RECVX_BUILD_GAME)` block.
- `port/src/main_pc.c`: wrapped the `njUserInit`/`njUserMain`/`njUserExit`/
  `InitAdvSystem` externs, the entire `run_game_loop` function (and its
  `sys`/`AdvWork` helpers), and the `run_game_loop(backend)` call site in
  `#ifdef RECVX_BUILD_GAME`. Without the macro, requesting game mode
  (default, or `--game`) now logs a clear error and exits 1 instead of
  failing to link. Also found and guarded a second, independent problem:
  `run_fmv_demo` (the FMV-only path) unconditionally called
  `recvx_pump_pad()`, which only exists in `game_texture_stubs.c`
  (`RECVX_BUILD_GAME`-only) — this call mirrors input into the decomp's
  `Pad[]`/`sys` structs and is unused by the FMV demo (which reads its own
  `recvx_input_buttons()`), so it was dead weight for Phase 0 and safe to
  guard out entirely.
- `port/src/afs/afs_mount.c`: `MountSoundAfs` (writes decomp `sys` struct
  fields) and the RDX-lookup helper (`rdx_files`/`rdx_image_data_max`,
  used by `port_try_rdx_lookup` and inline in `GetIsoFileSize`) are only
  reachable from the now-guarded game path, but as a static library
  `recvx_afs` is still pulled fully into the link (via always-used
  `recvx_set_gamedata_dir`), so ordinary linkers (no
  `-ffunction-sections`/`--gc-sections`) still demand every extern in the
  object file resolve regardless of reachability. Gave both a
  `#ifndef RECVX_BUILD_GAME` stub body (`return -1`) instead.

Verified: configured + built cleanly with `RECVX_BUILD_GAME=OFF`
(`cmake --preset x64-linux-vcpkg -DRECVX_BUILD_GAME=OFF`,
`cmake --build --preset x64-linux-debug`) — `recvx_pc` now links.
Ran `./build/recvx_pc --iso ".../Resident Evil - Code - Veronica X
(USA).iso" --play-movie 0` under `xvfb-run` with `SDL_AUDIODRIVER=dummy`
(container has no display or audio device): ISO opened, GL backend
initialized, `MOVIE/MV_000.PSS` opened from the ISO and decoded via
ffmpeg (mpeg2video 320x352), audio queued to SDL — ran cleanly until the
15s test timeout, no crash. (Note: `g_run_game` defaults to `true` in
`main_pc.c`, so an explicit `--play-movie N` — or `--gallery`/`--list-afs`/
`--dump-afs` — is required to reach the FMV path in a Phase 0 build;
running with no flags now hits the clean "built without RECVX_BUILD_GAME"
error instead of failing to link, which is the intended behavior.)

Committed to this branch (previous session): the Linux devcontainer
tooling (Ninja, vcpkg, autotools, nasm, X11/OpenGL/audio dev headers), the
`x64-linux-vcpkg` / `x64-linux-debug` CMake presets, and the two earlier
source portability fixes (`tex_dump.c`, `stub_ninja.c`). Committed in this
session: the `RECVX_BUILD_GAME` guards above. `recvx_pc` now builds, links,
and runs the FMV demo on Linux.
