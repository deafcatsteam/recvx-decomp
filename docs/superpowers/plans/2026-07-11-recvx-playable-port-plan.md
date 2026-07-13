# RECVX PC Port — Playable Roadmap & Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get `recvx-decomp-port` from "Phase 0 links and plays FMV" to a genuinely
playable Linux (then Windows) build of Code Veronica X, then use that as the base
for post-playable improvements (network, graphics, audio).

**Architecture:** Build on the existing `port/` PC-port layer (SDL2/vcpkg/CMake,
already Linux+Windows-portable stub/backend/AFS/FMV/audio modules). The only
remaining hard blocker to compiling the actual decompiled game source
(`RECVX_BUILD_GAME=ON`) on Linux is one Windows-only allocator function; once that's
ported, the ~16 game-source `.c` files compile under GCC for the first time ever
(previously MSVC-only), which is unknown-risk territory (same class of bug as the
`stub_ninja.c` GCC-vs-MSVC issue fixed earlier this session).

**Tech Stack:** C11/C++17, CMake + Ninja + vcpkg (`x64-linux` / `x64-windows`
triplets), GCC 12 (Linux devcontainer) / MSVC (Windows), SDL2, FFmpeg.

## Global Constraints

- Never force-push; never rewrite `pc-port` branch history — fast-forward only.
- Push target for this repo is the `fork` remote (`deafcatsteam/recvx-decomp:pc-port`), not `origin`.
- User's ISO: `/home/skitzo/Documents/Resident Evil - Code - Veronica X (USA).iso` — read-only, never modify.
- Fidelity rule (inherited from `recvx-decomp` conventions): don't "fix" decomp game logic while porting — only replace platform/hardware-facing code (allocators, `windows.h`-only APIs). Gameplay bugs found along the way get logged, not silently patched, unless they're a straight PS2-vs-PC platform artifact.
- Docker image `recvx-port-dev` (`.devcontainer/Dockerfile`) is the build environment; container `build/` dirs are root-owned — clean with `sudo rm -rf build` **inside** the container, never from the host.

---

## 📊 Progress overview (update this table as work lands)

| Phase | Description | Status | % |
|---|---|---|---|
| **P0** | Fork/clone/sync + Linux devcontainer + Phase 0 (FMV-only) build links & runs | ✅ Done | 100% |
| **P1** | `RECVX_BUILD_GAME=ON` compiles & links on Linux | ✅ Done | 100% |
| **P2** | Game actually boots to title/gameplay on Linux (real input, real room load) | ✅ Done | 100% — both movie-to-room texture-handoff SIGSEGVs fixed, and confirmed real player movement: `bhAddSpeed` (the function every `bhCPM2_act_*` motion handler calls to integrate `plp->px/pz` from speed+heading) was a second shadowed no-op stub, same bug class as `njInitTexture`. Fixed; forced analog input now moves the player continuously frame over frame. Room lighting (`njCnkSetEasyLight*`) and collision (`hitchk.c`) remain stubbed — tracked as P4 scope (full traversal/combat), not P2 |
| **P3** | Windows parity pass (MSVC build of the same `RECVX_BUILD_GAME=ON` config) | 🟠 Paused — 3 real bugs fixed, blocked on KATANA-shadow-header/MSVC include tangle | ~40% |
| **P4** | "Playable" gate: full room traversal, combat, save/load, no `RECVX_BUILD_GAME`-only crashes | 🟡 In progress — collision + lighting done; combat core (`weapon.c`/`playpch.c`/`pwksub.c`) done; effects (`effect.c` + all 8 `effsub*.c`) done (state real, rendering primitives deferred no-op); enemy AI roster **34/34 done**; save/load done (real `sceMc*`/`gdFsOpen` PC shim); crash sweep (Task 4.5) in progress — 9 real bugs found+fixed across two passes via real menu-driven New Game playthrough (first time this port has reached real gameplay through actual menu input rather than gdb shortcuts); a new architectural bug (overlapping x64 pointer writes at legacy 4-byte-spaced offsets in `en01.c`) found and documented as the next blocker; see this doc's "Phase 4 detail" section | ~60% |
| **P5** | Post-playable improvements (network Battle Mode, HD assets, graphics) | ⬜ Not scoped yet | 0% |

This document details **P1** and **P3** task-by-task (concrete, plannable now).
P4-P5 remain a roadmap only — they get their own detailed plan once P3 lands,
since their scope depends on what P1-P3 turn up (matches how
`recvx-decomp-port`'s own `RECVX_BUILD_GAME` option was never actually
exercised standalone before this project).

---

## Phase 1 detail: `RECVX_BUILD_GAME=ON` builds on Linux

### Task 1: Port `recvx_alloc_low4g` to Linux (mmap low-address allocator)

**Why this is the only real blocker:** grepped `game_texture_stubs.c` (the file the
original spec flagged as also needing a "`windows.h` audit") — it does **not**
include `<windows.h>` at all. It only calls `extern void* recvx_alloc_low4g(size_t)`
(`port/src/game_texture_stubs.c:103,108`), implemented Windows-only today in
`port/src/tex_pool_alloc.c:73-77` (`return NULL;` on non-Windows). So Phase 1's
actual scope is smaller than the original spec assumed: one function.

**Files:**
- Modify: `port/src/tex_pool_alloc.c:73-77` (the `#else` branch)

**Interfaces:**
- Produces: `void* recvx_alloc_low4g(size_t bytes)` — must return a pointer whose
  address is `< 0x100000000` (4 GiB), or `NULL` on failure. Same contract as the
  Windows branch above it. Consumed by `port/src/game_texture_stubs.c:108`
  (`ensure_pool()` — logs `"pool base=%p size=%zu"` via `RX_LOG("tex", ...)` on
  success, `"POOL ALLOC FAILED"` on `NULL`).

- [ ] **Step 1: Write the Linux implementation**

Replace the `#else` branch (currently lines 73-78 of `port/src/tex_pool_alloc.c`):

```c
#else
#include <sys/mman.h>

void* recvx_alloc_low4g(size_t bytes) {
#ifdef MAP_32BIT
    /* Linux-only flag: restricts the mapping to the first 2 GiB of the
     * address space, comfortably under the 4 GiB Uint32 texaddr limit. */
    void* p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p != MAP_FAILED) {
        RX_LOG("tex", "mmap MAP_32BIT base=%p", p);
        return p;
    }
    RX_LOG("tex", "mmap MAP_32BIT failed, trying fixed low addresses");
#endif
    /* Fallback: hint a fixed low address. MAP_FIXED_NOREPLACE (Linux 4.17+)
     * fails cleanly instead of silently mapping elsewhere if the address
     * is already used, mirroring the Windows VirtualAlloc-at-candidate loop. */
    static const uintptr_t candidates[] = {
        0x10000000, 0x20000000, 0x30000000, 0x40000000,
        0x50000000, 0x60000000, 0x70000000,
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        void* hint = (void*)candidates[i];
        void* p = mmap(hint, bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                        -1, 0);
        if (p != MAP_FAILED) {
            RX_LOG("tex", "mmap fixed base=%p", p);
            return p;
        }
    }
    RX_LOG("tex", "all low-4g mmap attempts failed");
    return NULL;
}
#endif
```

- [ ] **Step 2: Smoke-test the allocator in isolation (throwaway, not committed)**

```bash
cd /tmp && cat > alloc_test.c <<'EOF'
#include <stdio.h>
extern void* recvx_alloc_low4g(unsigned long bytes);
int main(void) {
    void* p = recvx_alloc_low4g(4096);
    printf("ptr=%p below_4g=%d\n", p, p != 0 && (unsigned long)p < 0x100000000UL);
    return p ? 0 : 1;
}
EOF
gcc -DRX_LOG_STUB -c -x c -o /tmp/alloc_impl.o - <<'EOF'
#include <stddef.h>
static void recvx_log(const char* tag, const char* fmt, ...) { (void)tag; (void)fmt; }
#define RX_LOG(tag, ...) recvx_log(tag, __VA_ARGS__)
#include <sys/mman.h>
void* recvx_alloc_low4g(size_t bytes) {
#ifdef MAP_32BIT
    void* p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p != MAP_FAILED) return p;
#endif
    static const unsigned long candidates[] = {0x10000000,0x20000000,0x30000000,0x40000000,0x50000000,0x60000000,0x70000000};
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); ++i) {
        void* hint = (void*)candidates[i];
        void* p = mmap(hint, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != MAP_FAILED) return p;
    }
    return (void*)0;
}
EOF
gcc /tmp/alloc_test.c /tmp/alloc_impl.o -o /tmp/alloc_test && /tmp/alloc_test
```

Expected: `ptr=0x...` non-null, `below_4g=1`, exit 0. This isolates "does
`MAP_32BIT`/`MAP_FIXED_NOREPLACE` work on this kernel" from "does it wire into
the real build" — if this fails, the kernel/toolchain lacks the flag and Step 1's
code needs a different fallback (raise as a blocker, don't guess).

- [ ] **Step 3: Apply the real edit to `port/src/tex_pool_alloc.c`, rebuild Phase 0 to confirm no regression**

```bash
cd ~/projects/recvx-decomp-port
docker run --rm -v "$(pwd)":/workspace -w /workspace/port recvx-port-dev bash -c "sudo rm -rf build"
docker run --rm -v "$(pwd)":/workspace -w /workspace/port recvx-port-dev bash -c "cmake --preset x64-linux-vcpkg -DRECVX_BUILD_GAME=OFF && cmake --build --preset x64-linux-debug"
```

Expected: still links clean (Phase 0 doesn't call `recvx_alloc_low4g` — this just
confirms the edit didn't break the always-compiled `recvx_port_stubs` library).

- [ ] **Step 4: Commit**

```bash
cd ~/projects/recvx-decomp-port
git add port/src/tex_pool_alloc.c
git commit -m "$(cat <<'EOF'
build: port recvx_alloc_low4g to Linux via mmap(MAP_32BIT)

game_texture_stubs.c's texture pool needs an allocation below 4GiB so
NJS_TEXNAME.texaddr (a Uint32) can hold the pointer losslessly. The
Windows implementation used VirtualAlloc2 with an address-range
constraint; Linux has no equivalent Win32 call but does have
MAP_32BIT (restricts to the first 2GiB, well within the 4GiB budget),
with a MAP_FIXED_NOREPLACE low-address-candidate fallback mirroring
the existing Windows fallback loop.
EOF
)"
git push fork pc-port
```

---

### Task 2: Build with `RECVX_BUILD_GAME=ON` on Linux, fix GCC-only compile errors until it links

**This task is inherently exploratory** — the ~16 game-source files
(`src/ps2/veronica/prog/main.c`, `system.c`, `adv.c`, `padman.c`, `vibman.c`,
`flag.c`, `fileview.c`, `itemview.c`, `sub1.c`, `item.c`, `ps2_texture.c`,
`ps2_dvd_image.c`, `message.c`, plus port-side `game_texture_stubs.c`,
`ninja_3d.c`, `ninja_cnk.c`, `binfunc.c` — see `port/CMakeLists.txt:226-303`)
have **never been compiled with GCC** — only MSVC via the `x64-vcpkg` preset. No
plan can predict GCC-specific errors in advance (we found one such bug per file
in Phase 0's much smaller stub set: `tex_dump.c`'s `<direct.h>`, `stub_ninja.c`'s
struct redeclaration). Treat this as a bounded debug loop with a hard verification
gate, not a fixed list of edits.

**Files:** unknown until errors surface — likely candidates are the ~16 files above.

**Interfaces:**
- Consumes: Task 1's `recvx_alloc_low4g` (must be committed first).
- Produces: a linked `port/build/recvx_pc` with `RECVX_BUILD_GAME=ON`.

- [ ] **Step 1: Attempt the configure + build, capture full output**

```bash
cd ~/projects/recvx-decomp-port
docker run --rm -v "$(pwd)":/workspace -w /workspace/port recvx-port-dev bash -c "sudo rm -rf build"
docker run --rm -v "$(pwd)":/workspace -w /workspace/port recvx-port-dev \
  bash -c "cmake --preset x64-linux-vcpkg -DRECVX_BUILD_GAME=ON && cmake --build --preset x64-linux-debug" \
  2>&1 | tee /tmp/recvx-game-build.log
```

- [ ] **Step 2: If it fails, categorize each error by file + error class**

Common GCC-vs-MSVC classes seen so far in this project (for reference, not
exhaustive): MSVC-only headers included unconditionally (fix: `#ifdef _WIN32`
guard + POSIX equivalent, as done for `<direct.h>`); struct/type redeclaration
GCC treats as conflicting where MSVC didn't (fix: use the header's canonical
type, as done for `recvx_gfx_vtx`); GCC being stricter about implicit
declarations, `__declspec`, MSVC pragma pack differences, or `#pragma comment(lib,...)`
(no GCC equivalent — needs `#ifdef _WIN32` guard, same pattern already used for
the crash handler in `main_pc.c:24-29`).

For each error: fix minimally (smallest change that satisfies GCC without
altering decomp-game logic — this is platform-compat code, not gameplay code),
rebuild, repeat.

- [ ] **Step 3: Repeat Step 1 until the build produces zero errors and links**

Expected terminal state: `cmake --build` exits 0, ending in `Linking C executable
recvx_pc` (or C++ if any `.cpp` involved), and:

```bash
ls -la ~/projects/recvx-decomp-port/port/build/recvx_pc
```
shows a file with the executable bit set.

- [ ] **Step 4: Commit each meaningful fix batch separately (not one giant commit)**

Group by root cause, same granularity as this session's `tex_dump.c` /
`stub_ninja.c` commits — e.g. one commit per file-class of fix, referencing
which GCC error it resolves in the commit body.

```bash
cd ~/projects/recvx-decomp-port
git add <fixed files>
git commit -m "build: fix GCC compile error in <file> (<one-line what/why>)"
```

- [ ] **Step 5: Push to fork once fully linked**

```bash
git push fork pc-port
```

- [x] **Step 6: Update this plan's progress table**

Set P1 to ✅ 100% once `recvx_pc` links with `RECVX_BUILD_GAME=ON`. Record actual
time spent and error count found (for calibrating how risky P2/P3 will be).

**Actual result (2026-07-12):** linked successfully after 4 fix commits, 5 rebuild
iterations. Bug classes found, in order: (1) 9 case-sensitivity include mismatches
(KATANA headers `#include`-ing a sibling with different case than the file on
disk — MSVC's case-insensitive filesystem never noticed) — fixed via one-line
forwarding shims in `port/include/compat/`; (2) a `-D`/`-include` command-line
ordering bug in `port/CMakeLists.txt` that pre-armed KATANA's CRT shadow-header
guards before the prelude's real `#include <stddef.h>`/etc ran, leaving
`size_t`/`memcpy`/etc undeclared across every game-source file; (3) GCC 14
hardening `-Wimplicit-function-declaration`/`-Wint-conversion`/
`-Wincompatible-pointer-types` from warnings to hard errors for this 1990s
MWCC-era code — downgraded to non-fatal warnings for `recvx_game` only; (4) one
GNU C lvalue-cast-increment idiom in `ps2_texture.c` that GCC has since removed
as an extension — split into two statements, identical behavior; (5)
`recvx_port_stubs` never received the `RECVX_BUILD_GAME` define, so its
always-built stub globals collided with `system.c`'s real ones under GCC 10+'s
`-fno-common` default — added the missing define and guarded the stubs; (6) one
genuinely missing symbol (`GetFileSize`, real impl in uncompiled `gdlib.c`) —
resolved by delegating to the already-working `GetIsoFileSize` in
`afs_mount.c` rather than adding a dummy stub, since the call site's exact
`rm_*.rdx` room-file pattern matches what that function already handles.

No case where the risk assessment ("no gameplay logic changes, only
platform/toolchain-compat fixes") was violated — every fix is behavior-
preserving on the semantics the original MWCC/MSVC toolchains already assumed.

---

### Task 3: Smoke-test the game boot on Linux (headless, `RECVX_BUILD_GAME=ON`)

**Files:** none (verification-only task).

**Interfaces:**
- Consumes: Task 2's linked `recvx_pc`.
- Produces: a documented boot log — either "boots to title/gameplay" (great) or
  "crashes at X" (expected per the source comment in `main_pc.c:11`: "*The
  --game path will crash / misrender until more of the decomp is brought in*" —
  this task's job is to find out exactly where, not to fix it yet).

- [x] **Step 1: Run against the real ISO under xvfb, capture the boot log**

```bash
cd ~/projects/recvx-decomp-port
docker run --rm \
  -v "$(pwd)":/workspace \
  -v "/home/skitzo/Documents":/iso-src:ro \
  -e SDL_AUDIODRIVER=dummy \
  -w /workspace/port \
  recvx-port-dev \
  bash -c "apt-get update -qq && apt-get install -y -qq xvfb >/dev/null 2>&1; \
    timeout 30 xvfb-run -a ./build/recvx_pc --iso '/iso-src/Resident Evil - Code - Veronica X (USA).iso' --gamedata /iso-src --game 2>&1 | tee /tmp/recvx-game-boot.log"
```

(Note: `--gamedata` needs a real extracted directory per `parse_args`'s default
`C:\Claude\codeveronica\gamedata` — if no loose-file gamedata dir exists yet,
this step also tells us whether `--game` mode can run purely from the ISO or
genuinely requires extracted AFS files first; adjust based on what
`MountSoundAfs`/`InitAdvSystem` actually need once Task 2 links.)

- [x] **Step 2: Read the log, note the exact crash point (or confirm it boots further than expected)**

**Actual result (2026-07-12):** `--gamedata` needed real extracted AFS data —
confirmed by first running with the (nonexistent) default `--gamedata` path:
no crash, but `MountSoundAfs` fails 0/7 partitions and the engine spins forever
in the `Warning`/`Firstmovie` boot-monitor state, since every `RequestReadInsideFile`
call returns `bad part=N` and nothing progresses. This is a stable, permanent
stall, not a hang bug — a symptom of missing data, not broken code.

All 8 AFS files the port needs (`BGM1.AFS`, `VOICE1.AFS`, `MULTSPQ1.AFS`,
`ADV.AFS`, `ITEM1.AFS`, `MRY.AFS`, `SYSTEM.AFS`, `RDX_LNK.AFS`) turned out to
ship as plain top-level files directly on the ISO9660 filesystem — extracted
them read-only (`isoinfo -x`, no ISO modification) into a scratch `--gamedata`
dir to get a real test. Result: **`MountSoundAfs` now mounts 8/7 partitions
("boot OK")**, and the boot chain progresses much further — Warning screen
loads and decodes real TIM2 textures (512×512) into the Task-1 `mmap`
allocator, transitions cleanly into the `Ipl` task (frame 432, `tk_flg`
`active=[Ipl,Monitor,SndMonitor]`), reads `ADV.AFS` for a 1024×512 texture,
and advances `AdvWork.Mode` 1→7 over ~200 more frames — **then segfaults at
frame 646**, right after the `Mode=6→7` transition, with no further log output
(`Segmentation fault (core dumped)`; no core file survived the `--rm`
container). Not yet diagnosed — this is Phase 2's starting point, not fixed
here per this task's scope (verification only).

This becomes the seed for Phase 2's task list: the crash is deep inside the
`Ipl` task's `AdvWork.Mode` state machine (`adv.c`, likely `Adv_Ipl` or
similar per `main_pc.c`'s task-name table), well past the point Phase 0 (FMV
demo) or Phase 1 (compiles) ever exercised, and only reachable with real
extracted AFS gamedata — so P2 should start with (a) making gamedata
extraction part of the normal dev workflow (not a one-off manual `isoinfo`
scratch step) and (b) a debugger-attached repro of the frame-646 segfault.

---

## Phase 2 progress log (informal — P2 not detailed into tasks yet)

**2026-07-12 — ASan-based repro of the frame-646 crash, two bugs found + fixed:**

Rebuilt `recvx_pc` with `-fsanitize=address` (separate `port/build-asan` dir,
gitignored) and re-ran the Task 3 real-gamedata smoke test under it. ASan
caught the actual corrupting write, well before the deferred SIGSEGV:

1. **`palbuf` undersized (root cause of the frame-646 crash).**
   `stub_game.c`'s placeholder `palbuf[256]` was 16x smaller than the
   `palbuf[4096]` the already-compiled real `ps2_texture.c`
   (`bhSetMemPvpTexture` → `ClutCopy`) indexes into. `ps2_dummy.c` — the file
   that would provide the correctly-sized real definition — isn't compiled
   (it's PS2 GS/VU0 rendering code with raw MIPS/VU inline asm, not portable
   to x86-64). Every CLUT copy silently wrote past the small stub buffer into
   whatever static happened to sit next in BSS, eventually corrupting the FMV
   glue's `g_movie_fmv` static ~600 frames later — that's what actually
   crashed at frame 646, in a completely unrelated subsystem (`recvx_fmv_close`
   via `PlayStartMovieEx`, first Capcom-logo movie call). Fixed by sizing the
   stub to match (`palbuf[4096] __attribute__((aligned(64)))`).

2. **`Pad_act` wrong type/size + stale `pdVibMx*` stub signatures.**
   Same bug class, found immediately after fixing #1 (game then got much
   further, into the title/attract loop). `stub_game.c` stood in a scalar
   `unsigned int Pad_act` for the real `PAD_ACT Pad_act[20]` struct array, and
   its `pdVibMx*` no-op stubs had signatures that didn't match what the
   already-compiled `vibman.c` actually calls. Fixed by wiring in the real
   `ps2_sg_pdvib.c` (100% matched, self-contained, no PS2 asm — just `scePad*`
   SDK calls) instead of guessing at another stub size, matching this
   codebase's existing convention of replacing stubs with real files as they
   become compilable. Needed one new PS2 SDK stub (`scePadSetActDirect`) and
   three missing `compat/libpad.h` constants to compile.

Both fixes verified via ASan (no more overflows) and the plain debug binary
(commit `9f575ea2`). The binary now survives frame 646 and a full first
attract-loop cycle (title → Capcom logo → title-screen movie attempt → back
to title), reaching frame ~2582 — a second cycle through the same loop —
before hitting a **new, different** SIGSEGV.

**Next bug found (not yet fixed):** `bhDispMessage` (`message.c:589`, called
from `bhSysCallMonitor`) crashes on the *second* pass through the
`sysmes.ald` message-table load sequence. That load is a multi-frame state
machine (`system.c:1409`, `sys->mn_md1` 1→2→3): state 1 issues
`RequestReadIsoFile("sysmes.ald", ...)` and immediately sets `sys->mes_ip`;
state 2 waits for `GetReadFileStatus() == 0` before finishing the real parse
(`sys->mes_sp` etc.). `bhDispMessage` got called with `sys->mes_sp` read
before that parse completed. Suspect this is a port-layer (not decomp-logic)
timing bug: our `RequestReadIsoFile`/`GetReadFileStatus` in
`afs_mount.c` may be resolving synchronously in a way the original PS2's
genuinely-async DMA read didn't, breaking an invariant the state machine
relies on across repeated load cycles (e.g. a flag not reset between the
first and second attract-loop pass). Not yet root-caused — this is where P2
diagnosis should pick up next.

**2026-07-12 (evening) — bug #3 fixed + REAL ROOM LOADING works end to end:**

1. **`bhDispMessage` crash root-caused and fixed (`e6e59f3e`).** Not an
   async-timing bug after all: at game start `bhInitGame` re-arms the
   monitor's sysmes.ald reload (`mn_mode0=1`, `memp=keepmem`). State 1
   updates `mes_ip` but `mes_sp` is only re-derived one frame later in
   state 2; in that window the NOW LOADING draw (`system.c:2257`, gated
   only on `mes_sp != NULL`) dereferences the stale `mes_sp`, reads
   message text bytes (`0xFFFF00C5`) as a table offset, and walks `dp`
   4 GB into unmapped memory. On PS2 the wild read stayed inside the
   mirrored 32 MB address space (one frame of garbage glyphs); on x64 it
   faults. Fix: NULL `mes_sp` when the reload is issued so the existing
   guard covers the window. Verified under gdb (crash gone, game proceeds
   to first room load).

2. **`Expand` implemented for real (`ad13ea5f`).** The decomp `expand.c`
   is 100% MIPS EE inline asm; the no-op stub left `rdtsz` garbage and
   stalled the game right after the first `rm_*.rdx` read. `port/src/
   expand_pc.c` re-implements the LZSS bit format decoded from the asm.
   Validated standalone under ASan against 4 real RDX_LNK.AFS entries
   (clean termination, version-10.0 float header in every output).

3. **Real room loading (`69f19992`).** `room.c`, `objitm.c`, `eneset.c`,
   `player.c`, `dread.c`, `ps2_NaColi.c` compiled in (all zero PS2 asm).
   Three x64 "PS2 32-bit file image" ports were required:
   - `bhSetRoom`: explicit ROM_WORK header conversion (the PS2 in-place
     u32 relocation sweep shears every field on x64) + a converted
     CUT_WORK array copy (32-bit `cuttp`, stride 0x2A8).
   - `bhMnbBinRealize`: raw-byte parse + native NJS_MDATA2_MOD calloc
     (PS2 in-place walk wrote 8-byte pointers over the next entry).
   - MN_WORK allocs: hardcoded `12288` (= 512 × 24 PS2 bytes) →
     `sizeof(MN_WORK) * 512`; pointer-truncation aligns → `uintptr_t`;
     `dread.c` NULL-page derefs PS2 tolerated → guarded (`RX_MLWP_OK`).
   Support: real nj math in `ninja_3d.c` (njSqrt/njCalcVector/
   njUnitVector/njInnerProduct/njRotateXYZ [Z,Y,X]/njUnitRotPortion/
   Push/PopMatrixEx), new `game_room_stubs.c` (~90 no-op gameplay stubs:
   bhEne01..71 AI handlers, hitchk, lights, effects, weapon SE), texture
   pool 64 → 512 slots.

   **Result: 300 s headless run, zero crashes.** Boot → warning → title →
   attract → game start → `rm_0130.rdx` decompressed, ROM_WORK/cameras/
   models/motions realized, ~127 texture slots decoded+uploaded (room,
   player, weapons, enemies), player+weapon data loaded, gameplay loop
   stable. P2's "boots to gameplay with real room load" structural goal
   is met headless.

**Where P2 picks up next:** visible rendering of the loaded room (the 3D
draw path `Ps2DrawOTag`/OT-walk is still stubbed — the loop runs but draws
little), real input driving the player (input.c exists; player motion
handlers are live but collision `hitchk.c` and camera `camera.c` are
stubbed), then enemy AI files (`bhEne*` — 34 no-op stubs to replace file
by file). Texture pool still never recycles per room (512 slots ≈ a few
room changes before exhaustion).

**2026-07-12 (morning) — room mesh actually renders on screen (`f25fc33d`):**

Picked up exactly where the log above left off ("visible rendering of the
loaded room"). Root-caused and fixed 4 separate bugs to get there:

1. **Room load was stuck on NOW LOADING forever.** `bhSysCallSndMonitor`'s
   `sdm_flg` drain state machine (`system.c:2296`) waits for
   `CheckTransEndSoundBank() == 0`; the stub always returned 1. Fixed —
   real semantic is 0 = idle, 1 = transition in progress.
2. **Wired `game.c` (room draw entry, `bhAllDrawModel`), `camera.c`
   (`bhControlCamera`), `cut.c` (fixed-camera-cut selection)** into the
   build — all zero PS2 asm. `cmmat`/`crmat` (real def lives in
   uncompiled `ps2_dummy.c`) stubbed with correct size in `stub_game.c`
   (same lesson as the earlier `palbuf` bug).
3. **`ninja_cnk.c`'s chunk walker was wrong for room meshes.** Didn't
   handle SHORT chunks (types 0-15, no size field — walking them as
   sized chunks desyncs into the rest of the blob, producing 3.7M
   garbage triangles) or the Capcom PS2 vertex format (type 51,
   `pCnkFuncTbl[51]=njCnkCvVnPs2`). Also the vlist is a CHAIN of vertex
   chunks (multiple base-index ranges) — only the first was ever
   decoded. All fixed; `CNK_VBUF_MAX` bumped 4096→32768.
4. **`gfx_row_to_col` (backend_gl.c) had a wrong transpose.** Ninja
   matrices are row-vector convention (`v' = v*M`); GL wants `M^T`,
   whose column-major bytes equal M's row-major bytes as-is — no
   transpose needed. The old explicit transpose sent translation into
   the projective row, collapsing every triangle into slivers after
   the perspective divide (visible as scattered thin lines, not a
   filled room). Fixed to a straight `memcpy`.

**Verified:** 250s headless run, zero crashes, geometry fills the 3D
viewport in the correct silhouette/proportions for `rm_0130`. Screenshot
confirms real geometry at real scale.

**Not yet done — deliberately deferred:** the room renders **solid white,
untextured, unlit** — `cnk_emit_strip_tri` always draws with `slot=-1`
(white fallback texture) and vertex color forced to `0xFFFFFFFF`. Next
concrete step: decode the `NJD_CM_*` material chunks (type 16-31) to pick
up per-chunk color/texture-id and wire that into the texture pool
(`bhSetMemPvpTexture` already populates it per-room), then real lighting
via `njCnkSetEasyLight*`/`SimpleMultiLight*` (currently no-op stubs in
`game_room_stubs.c`).

**2026-07-12 (same morning, continued) — TEXTURED room renders: it's
recognizably Resident Evil (`878ebdd2`):**

Picked up the very next item on the list above. Decoded the two CNK
chunk types the room plist actually carries:
- `NJD_CT_TID` (type 8, 4-byte tiny chunk): texture id in `usSize &
  0xFFF`, was silently skipped as a no-op short chunk — now latched
  into a `g_cnk_cur_texid` global.
- `NJD_CM_D`/`DA`/`DS`/`DAS` (types 17/19/21/23): diffuse ARGB material
  chunks, byte order confirmed against the real `njCnkCmD` (B,G,R,A) —
  repacked into `g_cnk_cur_diffuse`.

New `recvx_cnk_resolve_texture_slot()` (game_texture_stubs.c) resolves a
texture id against whichever texlist `njSetTexture` last latched — that's
already `rom->mdl.texP`, set right before the room draw call — reusing
the same pool-slot machinery the 2D quad path uses. `cnk_emit_strip_tri`
now passes the resolved slot + latched color instead of the hardcoded
white placeholder.

**Result: the loaded room is now a recognizable, fully-textured RE
Code:Veronica corridor** — stone brick walls/floor, archway, staircase,
ceiling lamps, wall piping, correct scale/perspective/ambient tone.
Verified stable across 250s+, frame-identical, zero crashes.

**Still deferred:** real per-vertex/per-light lighting (currently just
ambient — `njCnkSetEasyLight*` family is still no-op), player input
driving movement, collision (`hitchk.c`), enemy AI (34 `bhEne*` stubs).

**2026-07-12 (same morning, continued again) — player input pipeline wired
(`94387db9`):**

Wired `pad.c` (`bhSetPad`, zero PS2 asm) into the build: reads
`njGetPeripheral(0)` — already real and SDL-backed via
`port/src/input/input.c` — into `sys->pad_on`/`pad_ax`/`pad_ay`, which
the already-compiled `bhControlPlayer` (player.c) reads every frame.
`pd_port` forced to 0 directly in `main_pc.c` (the real path to set it,
`bhCheckPadPort` in `sync.c`, also owns ~600 lines of async pad-DMA-poll
machinery we don't need — our SDL peripheral is always synchronously
present). Also wired `MdlPut.c` (`bhPutModel`/`bhCalcModel`/`bhCalcTree`
— per-entity draw for player/enemies/objects) and the remaining
`njCnk*DrawModel` variants in `ninja_cnk.c`.

Testing under **actual synthesized key input** (`xdotool` into the Xvfb
X11 session — not just a static headless run) immediately surfaced a
real crash: `bhDrawEnemy → bhPutModel → EasyMultiDrawTreeCnk` dereferenced
a NULL `owP` (per-instance model array). Traced to `sys->ewk_n` (enemy
slot watermark) observed **higher than** `rom->ene_n` — some `ene[]`
slots reach the draw path with model data set (`bhSetEneMdl`, room load)
but never got an `owP` allocated (`bhFinishRoom`'s `bhKeepObjWork` loop,
bounded by `rom->ene_n`). **Not yet root-caused** to the exact multi-room
enemy-slot bookkeeping divergence — added `RECVX_PC_PORT`-guarded NULL
checks at the three call sites assuming `owP` is always valid (same
defensive pattern as `dread.c`'s earlier `RX_MLWP_OK` guard) so
unallocated entities are skipped rather than crashing.

**Verified:** 200s+ run with continuous synthesized key input, zero
crashes, title/attract-mode text and the 3D corridor render correctly
throughout. Did not yet confirm the player actually walking in real
gameplay (would require navigating NEW GAME from the title menu first) —
that's the natural next verification step.

**Still open:** the `ewk_n > rom->ene_n` enemy-slot root cause (currently
band-aided, not fixed), real lighting, collision (`hitchk.c`), confirming
in-gameplay player movement end-to-end.

---

### 2026-07-12 (same morning, continued again) — ewk_n root-caused, full boot-to-loop stability, real gap found (`b30b5cd2`)

Went back to the `ewk_n > rom->ene_n` bug marked "not yet root-caused" above.
Found it: `bhInitEnemy()` zeroes `ene[]` with a hardcoded `180224` byte
count — that's `128 * sizeof(BH_PWORK)` on the PS2 32-bit ABI (1408 bytes/
struct). Confirmed via `gdb -batch -ex 'p sizeof(BH_PWORK)'` against the
real build: on x64 it's 1936 bytes (pointer fields doubled), so the
hardcoded byte count only zeroes `ene[0..92]` — `ene[93..127]` are left
as whatever was previously on the heap. `bhCheckEneWorkNum()` scans all
128 slots for `flg&0x1`, so garbage in the unzeroed tail can spuriously
read as "occupied" and inflate `sys->ewk_n` past `rom->ene_n`. Fixed by
zeroing `sizeof(ene)` instead of the literal constant, gated under
`RECVX_PC_PORT` (original PS2 path untouched).

With that fixed, did a real end-to-end test: built a small `gdb` script
(`break PlayStartMovieEx if no==0`, then rewind `g_movie_start_ms` to
fast-forward through the ~3:30 post-New-Game movie without touching
Start — pressing Start mid-movie was itself found to desync the
`bhSysCallMovie` state machine into a stray `bhReturnTitle()`, a
separate, real, but lower-priority bug worth another look later).

Fast-forwarding through the full boot -> title -> New Game -> post-game
movie sequence surfaced 4 real NULL-deref crashes, all the same shape:
our `bhSysCallMovie` case-6 port-shortcut unsuspends the Game/Map tasks
(so `bhMainSequence`'s per-frame control calls start running) all at
once, instead of the staggered order the real PS2's typewriter/event
script chain would enforce (still uncompiled). Found and guarded, one
crash at a time, via repeated gdb-batch run/break/backtrace cycles:

  - `bhStandPlayerMotion` / `bhControlPlayer` (`player.c`): `plp->exp0`
    dereferenced before `bhInitPlayer()` (which runs later, inside
    `bhSysCallGame`'s own `mn_md1` file-load state machine) has run.
  - `bhPutModel` / `bhCalcTree` (`MdlPut.c`): `mlwP` itself NULL (not
    just `owP`, already guarded) — hit for the *player's own* model,
    before `bhSetPlayer`/`bhReadPlayerData` assign `ply.mlwP`.
  - `bhControlActiveCamera` (`cut.c`): `rom->cutp` NULL — confirms the
    actual remaining gap (see below).

All four guarded the same way as the pre-existing `owP==NULL` pattern.
Verified stable across several *full* cycles (title -> New Game confirm
-> post-game movie -> back to attract-mode loop) with **zero crashes**.

**The real gap, now clearly isolated:** nothing in our port ever
triggers an actual room load for New Game. `rom->cutp`/`rom->ene_n`/etc.
stay NULL/0 the whole time — the opening sequence runs to completion
(`AdvWork.Mode` 0 through 10) and then falls back to the attract-mode
loop, because the event-script opcode chain that would normally call
`bhSetRDT`/`bhInitReadRDT`/`bhSetRoom` for room 1 is the same
not-yet-compiled typewriter/event chain referenced throughout this log.
This is *not* a crash to guard around — it's a missing trigger. Next
step for actual player-movement-in-a-room verification is to find (or
synthesize, PC-port-shortcut style) that first-room-load call, not to
keep adding NULL guards to deeper systems that all currently starve for
the same reason.

**P2 progress: 92% -> 95%** (crash-free through the entire opening
sequence now; the one remaining gap is well-understood and scoped —
triggering the actual first room load — rather than an open-ended pile
of unknown crashes).

**Still open:** trigger the first room load for New Game (the real
blocker for confirming in-room player movement), real lighting
(`njCnkSetEasyLight*` family still no-op stubs), collision (`hitchk.c`
not compiled), the Start-mid-movie desync bug in `bhSysCallMovie`
(lower priority — avoidable by not pressing Start during FMV playback).

---

### 2026-07-12 (same day, continued) — real room-load root cause found and fixed: it was never the event/typewriter chain

The previous entry's hypothesis — "room load never fires because the
uncompiled typewriter/event script chain is what triggers it" — was
**wrong**, or at least not the actual blocker. Traced the real mechanism:

`bhFirstGameStart()` (`system.c:461`, called unconditionally from
`bhSysCallOpening`) already sets `sys->mn_mode0 = 1` directly — no
event-script involvement needed. That request flows into
`bhSysCallMonitor` (`system.c`, the `mn_md0`/`mn_md1` nested state
machine), which is **already fully compiled and 100%-matching**: it
reads player/weapon data, then (case 10) requests `mn_mode0 = 4`, which
drives the room-load state machine (`bhInitReadRDT`/`bhSetRDT`, building
`"rm_%1d%02d%1d.rdx"` and reading it via `RequestReadIsoFile`/
`GetFileSize`). None of this needed porting — it was already there and
already wired to run every frame (the Monitor task is unsuspended the
entire time, confirmed via `tk_flg`/`ts_flg` bit inspection).

The actual reason it stalled forever: **we never mount a real `.iso`**.
Every test run so far launched with `--gamedata <dir>` only. `GetIsoFileSize`
and `RequestReadIsoFile` (`port/src/afs/afs_mount.c`) only had two data
paths — the `RDX_LNK.AFS`-backed room-file special case, and a real
mounted ISO via `recvx_iso_global()`. With no `.iso` file (we only have
an extracted directory, confirmed: no `.iso` exists anywhere under
`recvx-decomp/iso/`), `recvx_iso_global()` is always `NULL`, so
`GetIsoFileSize("sysmes.ald")` always returned 0 — and `bhSysCallMonitor`'s
`mn_md0==1` chain (case 1, waiting on exactly that file) stalled on
frame 1 forever, every single run. That single stall is *why* `bhInitPlayer`/
`bhInitEnemy` never ran with real data (explaining the whole family of
NULL-guard crashes fixed in the previous entries) and why room load
(`mn_mode0=4`) was never reached.

**Fix:** added a loose-file fallback to both functions in
`port/src/afs/afs_mount.c` — `port_loose_find_path()` does a
case-insensitive scan of the gamedata dir root (flat, matches how these
ISO-root files — `SYSMES.ALD`, etc. — actually sit on disk) and
`port_try_loose_iso_read`/`port_try_loose_iso_size` read/size the file
directly via `fopen`/`fseek`, tried after the real-ISO path fails/is
absent. Mirrors the existing loose-file pattern `recvx_fmv_open_loose`
already used for movies.

**Verified:** rebuilt, re-ran the same gdb fast-forward test as the
previous entries. Log now shows:
```
[iso] loose ISO-root read OK: sysmes.ald -> 89088 bytes (/iso-src/data/SYSMES.ALD)
...
[iso] rdx lookup OK: rm_0130.rdx -> idx=15 bytes=1285767
...
[iso] rdx lookup OK: rm_0100.rdx -> idx=12 bytes=3257704
```
Real room geometry (`rm_0130`/`rm_0100` — the courtyard/gate area) loads
and renders correctly (screenshot-verified: "DEMO PLAY" attract-mode
gameplay demo rendering the actual 3D room, textures and models intact),
sustained over a long run with **zero crashes** — all four of the
previous session's NULL guards are now confirmed no-ops in the normal
path, since the data they were guarding against now actually loads.

**Not yet done:** a live-input-confirmed interactive New Game session
(catching the "Press Start" window with synthesized `xdotool` input to
go through the real menu rather than the attract-mode auto-timeout path)
to directly observe player movement. Attempted this but the headless
test environment (Xvfb + software GL rendering under `gdb`, plus verbose
per-model-per-frame logging) runs frames at roughly 1 every several
seconds, making it impractical to reliably catch a ~15-second input
window within a single session. This is a test-environment throughput
problem, not a sign of a code defect — the underlying room-load/render
pipeline is now proven correct with real data. Fast-follow: either trim
per-frame log verbosity for test runs or drive the Start-press via a
`gdb` breakpoint override (`break CheckStartButton` + `return 1`) using
a *fresh* process rather than one already `gdb -batch`-attached (can't
double-attach ptrace to the same PID).

**P2 progress: 95% -> 98%.**

### 2026-07-12 (same day, continued further) — real live-input New Game confirmed; new blocking crash found in room-texture upload

Picked back up on the one remaining P2 item: live-input-confirmed player
movement. Two real findings this round, one fix and one new bug.

**Fix — `[cnk]` log spam was the actual throughput problem.**
`njCnkEasyMultiDrawModel`/`njCnkEasyMultiDrawObjectI` log unconditionally,
once per model per frame (hundreds/frame during real room rendering), and
`recvx_log()` (`port/src/stubs/stub_log.c`) does an unconditional
`fflush(stdout)` on every call. That combination, not Xvfb/software-GL
rendering itself, was why frames crawled at ~1 every few seconds. Gated
the `"cnk"` tag behind an opt-in env var (`RECVX_LOG_CNK=1`, off by
default) in `stub_log.c`. Frame throughput went from single digits/minute
to ~30-40 fps immediately after rebuilding — this is what made the rest
of this session's testing tractable at all.

**Root cause of last session's "SENT START never worked" — found and fixed.**
`xdotool key Return` sends a keydown+keyup pair fast enough that both
land in the *same* `SDL_PollEvent` drain inside one frame's `gl_pump()`.
Our edge-detection model (`recvx_input_new_frame()`: `press = on & ~prev`)
computes the press bit from `on` *after* `gl_pump()` returns for that
frame, so a same-frame down+up cancels out — `on` is back to 0 by the
time the latch runs, and `press` never goes 1. This is why every previous
attempt (this session and the last) saw the Start press silently ignored
and the title screen fall through to its natural 900-frame timeout into
attract/"DEMO PLAY" every time, indistinguishable in the logs from "no
input arrived at all". Confirmed via a `CheckStartButton` probe
breakpoint (0 hits during the fake early Mode-cycles that turned out to
belong to the warning-message/logo screens, not `Adv_BioCvTitle` at all —
`Adv_BioCvTitle` itself is only entered once, well after two earlier
non-interactive passes reuse the same `AdvWork.Mode` field). **Fix:**
`xdotool keydown --window <id> Return`, `sleep 0.5`, `xdotool keyup
--window <id> Return` — spacing the two events across a frame boundary
makes the press register correctly. Also needed `xdotool windowfocus`
first (no window manager running under Xvfb, so nothing has input focus
by default even though `SDL_PollEvent` does receive *some* events
unconditionally — e.g. the F1-F10 sample-audition debug keys worked
even before this fix, which is what exposed that the transport wasn't
the problem, only the down/up timing was).

**Result: genuine, button-confirmed New Game now reproduces on demand.**
With the input fix plus a `gdb` breakpoint on `CheckButton` that ORs in
the Start bit (`Pad[AdvWork.PortId].press |= 0x800`) to auto-confirm
menu navigation (Cursor already defaults to "New Game" when
`FindFirstVmDrive() < 0`, i.e. no memory card — real decompiled logic,
not a port shortcut), the sequence now reliably reaches:
`bhSysCallFirstmovie: Adv_BioCvTitle returned 2` (2 = real confirmed
New Game, as opposed to 1 = natural timeout into demo) →
`tk_flg=0x0073dfc0` (the real `bhFirstGameStart` signature) →
`[iso] rdx lookup OK: rm_0000.rdx` (the actual New Game starting room,
distinct from the demo path's `rm_0130`/`rm_0100`) → the MV_000 intro
movie starts playing, real-time. A real in-game Start press during the
movie (`rmi.MVCancelButton`/`MovieInfo.MovieCancelFlag` — `sdfunc.c`,
100%-matching, not a port shortcut) skips it too: `[fmv] PlayMovieMain:
Start-press skip`. All of this is now reproducible on demand with real
synthesized input, no `gdb` variable-forcing needed for the Start press
itself (only used for the menu-confirm step, to avoid needing a second
precisely-timed real keypress).

**New blocker found: SIGSEGV in `bhCopyMainmem2Texmem` during the
movie-to-room texture handoff.** Immediately after the Start-press movie
skip, reproduces every time:
```
Thread 1 "recvx_pc" received signal SIGSEGV, Segmentation fault.
0x... in bhCopyMainmem2Texmem (tlp=...) at ps2_texture.c:520
520	            addr[no] = *tmp;
#0  bhCopyMainmem2Texmem: i=0 num=32 addr=0x0 no=0
#1  bhSysCallMovie () at system.c:1233
#2  njUserMain () at main.c:199
```
Called from `system.c:1233` (`mvi_md` case 5 — `if (sys->mvi_flg != 0 &&
rom->mdl.texP != NULL) bhCopyMainmem2Texmem((NJS_TEXLIST*)rom->mdl.texP);`,
100%-matching real code, the step that uploads the just-loaded room's
textures right after the intro movie ends). `addr = Ps2_tex_info` is
NULL. `Ps2_tex_info` is a plain global pointer (`ps2_NaTextureFunction.c`)
set exactly once, at boot, by `njInitTexture(tbuf, 256)` from
`njUserInit()` (`main.c:166`, confirmed hit via breakpoint, `tbuf` is a
real static `NJS_TEXMEMLIST tbuf[256]` array in `main.c` — not port
code). It stays valid through the entire demo/menu/logo flow (hundreds
of successful `njLoadTexture` calls in every log, this session and
prior) and is provably still valid checks after boot. It goes NULL
somewhere between boot and this specific call, and — critically — only
on the **real New Game** path (`rm_0000`); the demo/attract path
(`rm_0130`/`rm_0100`) loads and renders fine, repeatedly, no crash. Tried
a hardware watchpoint (`watch Ps2_tex_info`) to catch the exact
clobbering write; it never fired before the crash, meaning the
corruption (if that's what it is) happens *before* our earliest
practical watch point (right at `njUserMain` entry) rather than during
steady-state play — possibly inside `bhFirstGameStart()` itself
(`system.c:461`, called from the same `bhSysCallOpening` chain, resets
`sys->memp = keepmem` and bump-allocates `sys->obwp`/`sys->itwp` via
`bhGetFreeMemory` — plausible but unconfirmed adjacency/overflow
candidate). A software watchpoint (single-stepping) was tried as a
fallback and was too slow to reach the crash in practical time — this
sandboxed container appears not to expose hardware debug registers to
`gdb` despite `--cap-add=SYS_PTRACE`.

**Not yet done:** finding the exact write that nulls `Ps2_tex_info` (or
confirming it's actually zero-since-boot on this path for some other
reason — not yet ruled out) and fixing it. This is now the concrete,
correctly-scoped next blocker for P2 completion — replacing the
previous entry's "test-environment throughput" framing, which is now
resolved. Fast-follow options for next session: bisect with targeted
breakpoints between `bhFirstGameStart()` and the crash site rather than
a blanket watchpoint; or check whether `Ps2_tex_info` and
`Ps2_tex_save`/other `ps2_texture.c` globals end up link-adjacent in
BSS (a bounds bug in a neighboring global's write could scribble over
it) by checking `nm`/`objdump` symbol addresses on the built binary.

**P2 progress: 98% -> 99%** (both the input-transport bug and the
log-throughput bug that were blocking *investigation itself* are fixed;
the remaining 1% is this one concrete crash, now fully reproducible and
backtraced rather than hidden behind test-environment noise).

---

### 2026-07-12 (same day, continued yet again) — both texture-handoff SIGSEGVs root-caused and fixed (`d2f...` / see commit below)

**Root cause #1 (the one from the previous entry): `Ps2_tex_info` is
NULL because the real `njInitTexture` is never linked at all.**

Bisected by (a) tracing `Ps2_tex_info` at every `recvx_pump_pad` call
from frame 0 — it reads `(nil)` on the *very first* frame, before any
gameplay logic runs; (b) confirming via register inspection at the
`njInitTexture` breakpoint (`$rdi`/`$rsi`) that `main.c:166`'s call
(`njInitTexture(tbuf, 256)`) passes the correct, valid `tbuf` address
every time; (c) `nm`-scanning the actual build output
(`port/build/CMakeFiles/recvx_game.dir`) for every object file defining
or referencing `njInitTexture`:

```
=== .../recvx_port_stubs.dir/src/stubs/stub_ninja.c.o ===
0000000000000086 T njInitTexture
=== .../recvx_game.dir/.../main.c.o ===
                 U njInitTexture
```

`ps2_NaTextureFunction.c` — the file with the real, "100% matching"
`njInitTexture` we'd been reading from and assumed was running (finish/
step landed back in `njUserInit` correctly, which is consistent with
*any* implementation returning normally, not proof of which one ran) —
**is not in `RECVX_GAME_SOURCES` at all.** `stub_ninja.c`'s no-op
(`void njInitTexture(void* buf, int count) { (void)buf; (void)count; }`)
is the only definition that exists in the link, so it's the one that
runs, and it never touches `Ps2_tex_info`. `game_texture_stubs.c` even
has a comment from an earlier session flagging exactly this ("defined
for real only in ... `ps2_NaTextureFunction.c` which aren't in
`RECVX_GAME_SOURCES` yet") — it just never got acted on for this
specific function.

**Fix:** followed the existing precedent in this same file (the
`njCnkEasyMultiDrawModel`/`njCnkEasyMultiDrawObjectI` stubs were already
removed from `stub_ninja.c` in favor of real impls in
`port/src/ninja_cnk.c`). Removed the no-op from `stub_ninja.c` and added
a real minimal impl in `port/src/game_texture_stubs.c` (which already
has the KATANA include path + `Ps2_tex_info`'s actual definition).

**Root cause #2 (found by the fix above): pointing `Ps2_tex_info` at
`tbuf` immediately produced a second, different SIGSEGV.** `tbuf` is a
plain high-address static array (`NJS_TEXMEMLIST tbuf[256];` in
`main.c`), and `ps2_texture.c`'s PS2-era code truncates
`NJS_TEXMEMLIST*` through a `Uint32 texaddr` field and back (the same
x64 pointer-width gotcha already documented at the top of
`game_texture_stubs.c`, which is why the port's *other* texture pool
(`g_tex_pool`) is deliberately allocated via `recvx_alloc_low4g`).
Round-tripping a real (>4 GiB) `tbuf` address through that 32-bit field
truncates it, and the next read reconstructs a wild pointer — confirmed
by backtrace: the crashing `tmp` value was exactly the low 32 bits of
the `Ps2_tex_info`-derived address seen in the previous run's log.

**Fix:** gave `Ps2_tex_info` its own dedicated `recvx_alloc_low4g`
buffer (sized to the `n` the caller passes) instead of using `tbuf` or
sharing `g_tex_pool` — sharing `g_tex_pool` was considered and rejected,
since `SearchNullNumber()` is stubbed to always return slot 0, and
`bhCopyMainmem2Texmem` would then stomp a `g_tex_pool` slot that
`njLoadTexture` may already have bound for real on-screen rendering.

**Verification:** rebuilt, reran the same real-input New-Game repro
(force-Start only until `sys->tk_flg == 0x0073dfc0`, i.e. exactly the
`bhFirstGameStart` signature, then stop forcing and drive
`recvx_input_set_stick(0, -100)` every frame instead). Both previous
crash sites are confirmed clear: ran 5000+ frames past New Game
(previously crashed within ~40 frames of the movie ending) with no
segfault.

**Not yet done — new, distinct blocker:** `plp->px/py/pz` never move
despite `sys->pad_ax`/`pad_ay` correctly reading the forced stick input
(`pad_ay=100` confirmed every frame) and `sys->gm_flg & 0x80001 == 0`
(the movement-threshold block in `bhSetPad`, `pad.c:158`, is not
gated off). `sys->ts_flg` does change once (`0x0007ce00` ->
`0x00000600` around frame 4380, meaning *something* unsuspends), but
position stays frozen at the spawn value (`41.74, 0, 36.97`) for
1000+ further frames. Leading hypothesis, **not yet confirmed**:
`bhSysCallGame`'s own internal state machine (referenced in existing
comments, e.g. `player.c:1375` — "`bhSysCallGame`'s `mn_md1` file-load
state machine reaches case 4") may still be working through room/player
init sub-states rather than dispatching real per-frame control yet, or
there's a missing piece of the same typewriter->event task-unsuspend
chain already flagged as incomplete in `bhSysCallOpening`'s
`RECVX_PC_PORT` comment (`system.c:424-429`). Not yet bisected.

**Files changed:** `port/src/stubs/stub_ninja.c` (removed the
`njInitTexture` no-op), `port/src/game_texture_stubs.c` (real impl +
dedicated low4g buffer).

**P2 progress: 99% -> 99.5%** (the two concrete crashes blocking all
forward progress past the movie-to-room handoff are fixed and verified
stable for 5000+ frames; the final piece for 100% — actual player
movement responding to input — is now isolated to a state-machine
gating question inside `bhSysCallGame`/task-suspend flags, not a crash).

---

### 2026-07-12 (same day, continued once more) — player movement root-caused and fixed: a second shadowed no-op stub (`bhAddSpeed`)

**Root cause:** ruled out the task-suspend/`bhSysCallGame` mode-machine
hypothesis from the previous entry first — traced `sys->sp_flg`,
`plp->stflg`, `plp->exp0`, `plp->mode0/1/2/3` and `plp->spd` frame-by-frame
via gdb (`recvx_pump_pad` + a `bhAddSpeed` breakpoint) with the same
force-Start-then-force-stick repro as before. All the gating conditions
in `bhControlPlayer` (`sys->sp_flg & 0x1`, `plp->stflg & 0x1000000`) and
`pad.c`'s analog-to-digital synthesis (`sys->gm_flg & 0x80001`) turned
out fine: `plp->mode2` correctly settled to `9` (`bhCPM2_act_bak`,
backward-walk — the forced stick direction happened to map there) with
`mode3==1` and a real nonzero `plp->spd` (`0.27`) computed from the
motion table every single frame. Yet `plp->px/pz` stayed bit-identical
across 600+ consecutive `bhAddSpeed` calls, even though a manual `call
njSin(...)`/`njCos(...)` at the breakpoint with the exact same angle
returned proper nonzero values (`-0.865898`/`-0.500221`) — meaning the
inputs to the position-integration math were all correct, but the write
wasn't taking effect.

Checked whether `bhAddSpeed` (`src/ps2/veronica/prog/pwksub.c:133`,
"100% matching") was actually the code executing, using the same `nm`
technique from the `njInitTexture` bug: `pwksub.c` has **no compiled
object file at all** in `port/build/CMakeFiles/recvx_game.dir` — it was
never added to `RECVX_GAME_SOURCES`. `grep`-ing for a second definition
found it: `port/src/game_room_stubs.c:184` had
`void bhAddSpeed(BH_PWORK* pp, int r) { (void)pp;(void)r; }` — a
blanket no-op covering the whole not-yet-ported collision/AI subsystem
(`game_room_stubs.c`'s header comment explicitly says so), silently
shadowing the one function in that file that's pure math with no
collision/model dependency and was already fully decompiled. Exact same
bug class as the `njInitTexture` fix earlier this session: a real,
completed decomp function never linked because its source file isn't in
the build, sitting behind a subsystem-wide stub.

**Fix:** did **not** add all of `pwksub.c` to `RECVX_GAME_SOURCES` —
most of its other functions (`bhSearchNearEnemy*`, `bhCheckL2Wall`,
`bhCheckC2Wall*`, etc.) are genuinely unported collision/AI code that
`game_room_stubs.c` intentionally still stubs (P4 scope), and adding the
whole file would collide with those stub definitions at link time.
Instead, followed the same "move just this one function" precedent as
`njInitTexture`: deleted the no-op body in `game_room_stubs.c` and
replaced it with `bhAddSpeed`'s real 3-line body (copied verbatim from
`pwksub.c`, which needs nothing beyond `njSin`/`njCos` — already
available via `game_room_stubs.c`'s existing `ninja.h` include).

**Verification:** rebuilt, reran the identical force-Start/force-stick
repro. `plp->px/pz` now advance every logged frame in a straight line
(e.g. `px` climbing from `41.74` to `173.14` over ~600 frames of held
input, `pz` from `36.97` to `112.87`) — real, continuous player movement
confirmed for the first time in this port.

**Files changed:** `port/src/game_room_stubs.c` (real `bhAddSpeed` impl
replacing the no-op).

**P2 progress: 99.5% -> 100%.** Boot, real input, New Game, room load,
and confirmed player movement all work end-to-end with no crashes.
Lighting and collision remain stubbed by design — that's P4 ("full room
traversal, combat"), not P2 ("game boots to gameplay").

---

## Phase 3 detail: Windows/MSVC parity pass

**Goal:** get the exact same `RECVX_BUILD_GAME=ON` config (already 100% working
on Linux/GCC as of P2) building and booting under MSVC on Windows, since the
`x64-vcpkg` preset (`port/CMakePresets.json`) and its `RECVX_BUILD_GAME`
option (defaults `ON`, `port/CMakeLists.txt:226`) have **never actually been
exercised** this whole project — every build/run so far happened in the Linux
devcontainer. Per the risk note already in this doc's intro (line 14) and the
original roadmap line, this is expected to be **lower-risk than P1**: the
decomp game source was written against MSVC/MWCC originally, so most of P1's
fixes (case-sensitive includes, `-fno-common`, GNU lvalue-cast extension,
implicit-declaration hardening) are GCC-only problems that simply won't exist
on MSVC. The realistic risk surface is narrower: (a) anything P1/P2 touched
with a Linux-only fix that might have broken the MSVC path by accident, since
neither was rebuilt on MSVC after those edits landed; (b) the reverse-class
bug already seen once (`stub_ninja.c`) — code that silently relied on
MSVC-specific behavior GCC happened to tolerate differently.

**No local Windows machine is available in this environment** (Linux
devcontainer + Docker only). Task 1 below sets up a `windows-latest` GitHub
Actions job as the test harness instead of assuming local MSVC access — this
also gives a durable, re-runnable regression check for future changes rather
than a one-off manual verification. If the user has their own Windows/Visual
Studio machine, Tasks 2+ can equally be run there instead of via CI; the fix
loop itself doesn't depend on which one is used.

**Tech Stack:** MSVC (Visual Studio 17 2022 generator, per `x64-vcpkg` preset),
vcpkg `x64-windows` triplet, same CMake presets file already in the repo.

**Files:** unknown until errors surface (same "bounded debug loop" framing as
P1 Task 2) — likely candidates are the same ~16 game-source files P1 touched,
plus anything P2 changed (`port/src/game_room_stubs.c`,
`port/src/tex_pool_alloc.c`, the texture-handoff fixes, `pad.c`/`player.c`
callers) since none of those were compiled with MSVC before.

**Interfaces:**
- Consumes: the fully-linked, boot-to-gameplay-confirmed Linux build from P1/P2
  (nothing new to build on — this is a parity/regression pass on existing code).
- Produces: a `windows-latest` CI workflow that builds `RECVX_BUILD_GAME=ON`
  with MSVC and fails the job on any compile/link error, plus whatever source
  fixes are needed to make it pass.

### Task 1: Add a `windows-latest` GitHub Actions build workflow

**Files:**
- Create: `.github/workflows/windows-build.yml`

- [ ] **Step 1: Write the workflow**

```yaml
name: Windows MSVC Build

on:
  push:
    branches: [pc-port]
  pull_request:
    branches: [pc-port]
  workflow_dispatch: {}

jobs:
  build:
    runs-on: windows-latest
    defaults:
      run:
        shell: pwsh
    steps:
      - uses: actions/checkout@v4

      - name: Install vcpkg
        run: |
          git clone https://github.com/microsoft/vcpkg C:\vcpkg
          C:\vcpkg\bootstrap-vcpkg.bat

      - name: Configure (RECVX_BUILD_GAME=ON, MSVC x64)
        working-directory: port
        env:
          VCPKG_ROOT: C:\vcpkg
        run: cmake --preset x64-vcpkg -DRECVX_BUILD_GAME=ON

      - name: Build
        working-directory: port
        run: cmake --build --preset x64-debug 2>&1 | Tee-Object -FilePath build-log.txt

      - name: Upload build log
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: windows-build-log
          path: port/build-log.txt
```

- [ ] **Step 2: Commit and push to trigger the first run**

```bash
cd ~/projects/recvx-decomp-port
git add .github/workflows/windows-build.yml
git commit -m "ci: add windows-latest MSVC build workflow for P3 parity pass"
git push fork pc-port
```

- [ ] **Step 3: Watch the run and fetch the result**

```bash
gh run list --repo deafcatsteam/recvx-decomp --workflow windows-build.yml -L 1
gh run watch --repo deafcatsteam/recvx-decomp <run-id>
```

Expected: either a green build (skip to Task 3) or a failed job with a
`build-log.txt` artifact showing the first MSVC error — that becomes Task 2's
input.

---

### Task 2: Fix MSVC compile/link errors until the workflow is green

**This task is inherently exploratory**, same framing as P1 Task 2 — no error
list can be written in advance since MSVC has never compiled this exact
current source tree (including all of P1's and P2's edits). Treat as a
bounded debug loop with a hard verification gate (the CI job passing), not a
fixed list of edits.

**Files:** unknown until errors surface.

**Interfaces:**
- Consumes: Task 1's CI workflow and its failure logs.
- Produces: a green `windows-build.yml` run building `recvx_pc.exe` with
  `RECVX_BUILD_GAME=ON`.

- [ ] **Step 1: Download and read the failing build log**

```bash
gh run download --repo deafcatsteam/recvx-decomp <run-id> -n windows-build-log -D /tmp/recvx-win-build
cat /tmp/recvx-win-build/build-log.txt | grep -i "error"
```

- [ ] **Step 2: For each distinct error, find root cause before fixing**

Apply the same fidelity rule as P1/P2 (line 25 of this doc): only touch
platform/toolchain-compat code, never gameplay logic. Given the risk analysis
above, expect most errors (if any) to come from P1/P2's Linux-motivated edits
rather than from the original decomp source — check `git log -p` on the
specific line if the cause isn't obvious:

```bash
git log -p --follow -- <file with the error>
```

For each error: fix minimally, re-push, re-watch the CI run.

- [ ] **Step 3: Repeat Step 1 until the workflow job is green**

Expected terminal state: `windows-build.yml` run status is `success`, and the
build log's tail shows `recvx_pc.vcxproj -> ...\recvx_pc.exe`.

**Actual results (2026-07-12, 4 CI rounds):**

1. `windows-latest` now resolves to a windows-2025 image shipping VS 2026 by
   default; our `x64-vcpkg` preset hardcodes generator `"Visual Studio 17
   2022"`, so `project()` failed with "could not find any instance of Visual
   Studio" even though vcpkg's own port builds (freetype, sdl2-ttf) succeeded
   fine on that same image (vcpkg autodetects whichever VS is present).
   Fixed by pinning `runs-on: windows-2022` instead of touching the shared
   preset (real VS2022 devs also use it locally). Commit `e7b9eb30`.
2. `stub_game.c`: MSVC doesn't parse GCC's `__attribute__((aligned(64)))`
   syntax at all. Added a real portable `RX_ALIGN64` prefix macro to
   `recvx_port.h` (`__declspec(align(64))` / `__attribute__((aligned(64)))`)
   rather than swallowing the attribute, since `cmmat`/`palbuf` are
   alignment-sensitive (misalignment silently corrupts adjacent BSS per the
   existing comments). Commit `992db819`.
3. `afs_mount.c`: MSVC has no `dirent.h`. Added a `_WIN32` branch using
   `FindFirstFileA`/`FindNextFileA`/`FindClose` for the flat case-insensitive
   directory scan; POSIX path unchanged. Commit `2195151e`.
4. `recvx_game` target failed entirely with `Cannot open include file:
   'ninja.h'` — root cause: `include/recvx-decomp-katana` (and cri/mwcc/
   ps2_sdk) are git submodules the workflow's `actions/checkout@v4` never
   initialized. Fixed with `submodules: recursive`. Commit `39fdc435`.
5. With submodules present, hit a deeper, structural failure: `error C2371:
   'size_t'/'ptrdiff_t' redefinition; different basic types` (KATANA's
   `stddef.h` vs MSVC's own) plus `error C1014: too many include files:
   depth = 1024` in `port/include/compat/ninjacnk.h` — an include-recursion
   loop. This is the KATANA CRT-shadow-header masking system (built and
   tuned around GCC's `-include`-then`-D` command-line ordering trick, see
   the `recvx_game` CMakeLists.txt comments around line 392) behaving
   differently under MSVC's `/FI` force-include mechanism. Not a one-line
   fix — same class of problem as 3 fixes in, each in a different file,
   per systematic-debugging's "question the architecture" threshold.

**Status: paused, not green.** Per explicit user decision (given a real
Windows machine exists but "flemme de l'allumer", and CI-only compile-parity
was already judged non-blocking for P4/P5 gameplay work), stopped after this
one agreed extra attempt rather than continuing to chase the include-order
tangle blind. The `windows-build.yml` workflow stays in the repo (harmless,
runs on push/PR/dispatch) so it can be picked back up later — either by
someone debugging the KATANA-shadow-header/MSVC interaction properly, or by
testing directly on the user's own Windows machine instead of CI trial-and-
error. 3 of the 4 fixes above are genuine, valuable, already-merged
cross-platform bugs independent of whether P3 is ever finished.

- [ ] **Step 4: Commit each fix batch separately**

```bash
git add <fixed files>
git commit -m "build: fix MSVC compile error in <file> (<one-line what/why>)"
git push fork pc-port
```

---

### Task 3: Smoke-test the Windows boot (manual, since headless Windows CI can't drive a real window easily)

**Files:** none (verification-only task).

**Interfaces:**
- Consumes: Task 2's green `recvx_pc.exe`.
- Produces: a documented boot log/screenshot from a real Windows run,
  confirming (or refuting) that the Linux P2 behavior (boot to title, New
  Game, room load, player movement) reproduces on Windows.

- [ ] **Step 1: Download the CI build artifact, or build locally on a Windows machine if the user has one**

```bash
gh run download --repo deafcatsteam/recvx-decomp <run-id> -n windows-build -D ./win-build
```

- [ ] **Step 2: Run against the real ISO/gamedata, same `--gamedata` flag as the Linux smoke test**

```powershell
.\recvx_pc.exe --gamedata <path-to-extracted-gamedata>
```

- [ ] **Step 3: Compare against the Linux P2 baseline**

Check: does it boot to title? Does New Game load the first room? Does forced
input move the player? Note any Windows-specific divergence (e.g. a crash
Linux didn't hit, or vice versa) — that's new P3 scope, not a re-run of P1/P2.

- [ ] **Step 4: Update this plan's progress table**

Set P3 to done once Task 3 confirms parity (or documents specific,
Windows-only gaps to track separately).

---

## Phase 4 detail: "Playable" gate

**Goal:** full room traversal, combat, save/load, and no
`RECVX_BUILD_GAME`-only crashes — the last gate before P5 (post-playable
polish, not scoped here).

**This section is the single gathering point for all P4 work, including
work paused under other phases.** Sub-tasks that already have their own
detailed plan doc are linked, not duplicated, below.

| Sub-task | Status | Detail |
|---|---|---|
| P3 — Windows/MSVC parity | 🟠 Paused, ~40% — 3 real cross-platform bugs fixed, blocked on a KATANA CRT-shadow-header vs MSVC `/FI` structural include tangle (`error C2371`, `error C1014`). Stopped per explicit user agreement after the "one last CI push"; **resume here, on a real Windows machine, only if/when needed** — see Task 4.0 below. | This doc, "Phase 3 detail" above |
| Task 1 — Collision (`hitchk.c`) | ✅ Done | `2026-07-12-recvx-p4-collision-lighting-plan.md` |
| Task 2 — Lighting setters + CPU shading | ✅ Done | `2026-07-12-recvx-p4-collision-lighting-plan.md` |
| Task 3 — `light.c` dynamic lighting | ✅ Done | `2026-07-12-recvx-p4-collision-lighting-plan.md` |
| Task 4.1 — Combat core (`weapon.c` + `playpch.c`) | ✅ Done — also pulled in `pwksub.c`/`effsub3.c`/new `njplus_coli.c` to close gaps | below |
| Task 4.2 — Effects (`effect.c`) | ✅ Done — `effect.c` + all 8 `effsub*.c` (0/1/1b/2/3/4/5/6) compiled real; rendering primitives (`njDraw*3D*`/`Ps2Shadow*`/`njCnkModDrawModel`/particle draw) no-op, see finding below | below |
| Task 4.3 — Enemy AI roster (`eneset.c` dispatch + `en*.c`) | ✅ Done — all 34/34 enemies real, plus shared helper libraries `zonzon.c`/`zonzon1.c`/`hitchkl.c`/`en01sub.c`/`en01b.c` | below |
| Task 4.4 — Save/load (8 files: `ps2_MemoryCard..c`/`ps2_sg_bup.c`/`ps2_McSaveFile.c`/`ps2_SaveScreen.c`/`ps2_LoadScreen.c`/`ps2_SystemSaveScreen.c`/`ps2_SystemLoadScreen.c`/`bup_00.c`) | ✅ Done — real `sceMc*`/`gdFsOpen` PC reimplementation in new `save_mc_shim.c`, shim-tested standalone | below |
| Task 4.5 — Full-playthrough crash sweep | 🟡 In progress — 9 real bugs found+fixed across two passes (2 memory-card shim bugs, 3+1 x64 pointer-truncation/width sites, message-table stale-pointer hazard across 3 call sites, an undecompiled-enemy-stub crash, a 9-function/11-call-site pointer-array-stride bug in `effect.c`); first real menu-driven New Game reached (real Start/Up/cursor input, not a gdb shortcut); a new architectural bug (overlapping 8-byte writes at 4-byte-spaced legacy offsets in `en01.c`, 10 sites) found and documented as the next blocker | below |

**Investigation done so far (this pass):** grepped every remaining P4 source
file for `asm`/`__asm__` (the tell for a VU0/EE-asm boundary that needs a
CPU reimplementation, same as `njCnkSetEasyLight*`/`njProjectScreen` in
Tasks 2–3). Result — **all of it is pure C**:

```
effect.c:            0 asm hits (1812 lines)
weapon.c:             0 asm hits (1896 lines)
playpch.c:            0 asm hits (1283 lines)
ps2_McSaveFile.c:     0 asm hits (1096 lines)
ps2_SaveScreen.c:     0 asm hits (1807 lines)
ps2_SystemSaveScreen.c: 0 asm hits (1302 lines)
```

This is a real, meaningful difference from Tasks 1–3: no CPU
reimplementation work is expected here, only the same
integrate-and-delete-shadowing-stubs mechanics already proven twice
(`hitchk.c`, `light.c`). The proven procedure, repeated per task below:

1. `grep -c "asm\|__asm__" <file>` — confirm no VU0/EE asm (already done above).
2. `grep -oE "^[A-Za-z_][A-Za-z0-9_ ]*\(" <file>` to list real function names,
   `comm -12` against `game_room_stubs.c`'s stub names to find the exact
   shadowing set (same technique used for `hitchk.c`'s 16-function overlap).
3. Add the file to `port/CMakeLists.txt`'s `RECVX_GAME_SOURCES`, with a
   comment documenting what it replaces and why (established convention —
   see the `hitchk.c`/`light.c` entries already there).
4. Build `recvx_game` + `recvx_pc`; fix any *new* symbol gaps the same way
   Tasks 1–3 did (real body if cheap/pure-C, no-op stub with a comment if
   genuinely out of scope) — do not silently re-stub something the file
   itself defines.
5. Delete the now-dead shadowing stubs from `game_room_stubs.c`.
6. Smoke-test under Xvfb (no crash, same or better behavior than before).
7. Update this table's status and commit.

---

### Task 4.0: Resume P3 (Windows/MSVC parity) — conditional, do last or on-demand

**Files:** `.github/workflows/windows-build.yml`, and whatever the KATANA
include-tangle fix touches once investigated (not yet known — this is
genuinely a "return to Phase 1 (root-cause investigation)" situation per
`systematic-debugging`, not a known fix).

**Interfaces:** none — this is CI/build-config work, not game code.

- [ ] **Step 1:** If a real Windows/Visual Studio machine becomes available,
  reproduce the `error C2371`/`error C1014` locally first — a local repro
  loop is far faster than iterating via GitHub Actions CI, which is how P3
  was forced to work so far.
- [ ] **Step 2:** Root-cause the KATANA CRT-shadow-header vs MSVC `/FI`
  force-include ordering conflict (see "Phase 3 detail" above for the full
  prior investigation). This is the point P3 stopped at per explicit user
  agreement ("un dernier push CI, puis stop") — treat it as a fresh Phase 1
  investigation, not a continuation of the same failed fix attempts.
- [ ] **Step 3:** Once fixed, re-run Phase 3's Task 3 (manual Windows smoke
  test) and mark P3 done in the Progress overview table at the top of this
  doc.

**This task is optional and may never trigger** — folded into P4 only so
all outstanding work lives in one place, per the project owner's own call
("P4 ouais on finira P3 sur le windows si besoin").

---

### Task 4.1: Combat core — `weapon.c` + `playpch.c`

**Files:**
- Modify: `port/CMakeLists.txt` (add both files to `RECVX_GAME_SOURCES`)
- Modify: `port/src/game_room_stubs.c` (delete shadowed stubs)
- Create/Modify: whatever new symbol gaps step 4 below surfaces

**Interfaces:**
- Consumes: `BH_PWORK`, `O_WRK`, `GA_WORK`, `WPN_TAB` types (already defined
  in existing headers — used elsewhere in already-compiled files); the
  collision primitives from `ps2_NaColi.c` (already compiled, Task 1);
  `njCalcPoint`/`njInnerProduct`/etc from `ninja_3d.c` (already real).
- Produces: real `bhActionWeapon`, `bhObjWpn`, `bhSetWeapon`,
  `bhCountBullet`, `bhCheckGunAtari`, `bhCheckKnifeAtari`,
  `bhCheckFlyAtari`, `bhSetBowDamage`, `bhCheckBombAtari`,
  `bhCheckCapCol2Capsule`, `bhSetGunSplash`, `bhSetExplosion`,
  `bhSetExplosionEffect(Ex)`, `PlyPchInit`, `PlyPchMain`,
  `bhCPM2_act_atk_pch`, `bhCPM2_act_suw_pch`, `bhCPM2_act_wsc_pch`,
  `bhCPM2_SearchPch`, `bhArmIkMdk` — currently the ~18 no-op stubs in
  `game_room_stubs.c`'s "player.c dependencies (weapons...)" block
  (`port/src/game_room_stubs.c:213`-ish).

- [ ] **Step 1:** Confirm the shadow set via `comm -12` between `weapon.c`'s
  + `playpch.c`'s top-level function names and the stub names in that block
  (listed above from a direct read — re-verify before deleting, the file
  may have grown since this plan was written).
- [ ] **Step 2:** Add both files to `RECVX_GAME_SOURCES` in
  `port/CMakeLists.txt`, following the existing comment convention.
- [ ] **Step 3:** Build `recvx_game`. Expect new symbol gaps for anything
  `weapon.c`/`playpch.c` call that isn't compiled yet — likely candidates
  given what's already stubbed: `bhSetExplosion` already has a real body
  (moved into `game_room_stubs.c` earlier per an existing comment there —
  check whether `weapon.c`'s own definition now collides with it, same
  bug class as the `njCnkSetEasyMultiLight`/`lgttab` duplicate-symbol
  errors hit in Task 3) and effect-system calls (`bhSetEffect`,
  `bhLinkBlood`) that depend on Task 4.2 below — may need to stay
  no-op stubs until Task 4.2 lands, or land 4.1+4.2 together.
- [ ] **Step 4:** Fix each gap: real body if pure C and cheap, otherwise a
  clearly-commented no-op stub (same standard as Tasks 1–3 — don't fake
  correctness).
- [ ] **Step 5:** Delete the now-real stubs from `game_room_stubs.c`.
- [ ] **Step 6:** Build clean, smoke-test under Xvfb (no crash).
- [ ] **Step 7:** Update the status table above, commit.

---

### Task 4.2: Effects — `effect.c`

**Files:**
- Modify: `port/CMakeLists.txt` (add `effect.c`)
- Modify: `port/src/game_room_stubs.c` (delete shadowed stubs)

**Interfaces:**
- Consumes: `njCnkEasyMultiDrawObject`/model draw path (`ninja_cnk.c`,
  real), `NJS_PRIM`/`NJS_POINT2COL` 2D draw primitives (currently only
  declared — `njDrawPolygon2D`/`njDrawLine2D` are still stubbed in
  `ps2_NaDraw2D.h`'s corresponding `.c`, not yet investigated; may be a
  new gap this task surfaces).
- Produces: real `bhInitEffect`, `bhSetFontTexture`, `bhClearEffect`,
  `bhClearEventEffect`, `bhClrEff_YT`, `bhPushEffectWork`,
  `bhPopEffectWork`, `bhDeleteYakkyou`, `bhDrawPARAM2D`, `bhSetEffect`,
  `bhSetEffectTb`, `bhSetEffectEvt`, `bhSetShadow`, `bhLinkBlood`,
  `bhControlEffect`, `bhDrawEffect`, `bhDrawPolEffect`, `bhDrawMdfEffect`,
  `bhDrawLinEffect`, `bhDrawNtxEffect3D` — replaces the "effects
  (effect.c)" stub block in `game_room_stubs.c:65`-ish (`bhClearEffect`,
  `bhSetEffect`, `bhSetEffectTb`, `bhSetExplosion`) plus `bhDrawEffect`/
  `bhSetShadow` currently stubbed elsewhere in the file — re-verify exact
  overlap via `comm -12` before deleting, same as every prior task.

- [x] **Step 1:** `comm -12` shadow-set check — confirmed 5-function
  overlap (`bhClearEffect`, `bhDrawEffect`, `bhSetEffect`, `bhSetEffectTb`,
  `bhSetShadow`).
- [x] **Step 2:** Added `effect.c` + all 8 `effsub*.c` files
  (`effsub0/1/1b/2/3/4/5/6.c` — `effsub3.c` was already in the build from
  Task 4.1) to `RECVX_GAME_SOURCES`. Re-scoping note: the earlier "~150
  more handlers" estimate was pessimistic — `bhJumpEffect[150]` has 64
  `bhEffDmy` no-op slots, so only 77 unique handlers were actually
  needed, plus two more dispatch tables (`bhJumpEffect0[100]` for IDs
  150-249, `bhJumpEffect3[50]` for IDs 300-349) not mentioned in the
  original finding — between them these cover every `bhEffNNN` handler
  across all 8 `effsub*.c` files, i.e. essentially the whole subsystem
  needed to be compiled together, not a subset.
- [x] **Step 3 — compile-error fixes:**
  - `effect.c:1775` `bhDrawThunder` — decomp header (`effect.h:40`)
    declared it non-static but the definition is `static`; nothing
    outside `effect.c` calls it (grep-confirmed). Removing the header
    prototype alone wasn't enough — the call site at `effect.c:944`
    (before the `static` definition at line 1775) then creates its own
    implicit non-static declaration, same conflict via a different path.
    Fixed by adding a `static void bhDrawThunder();` forward declaration
    directly above `bhDrawEffect` (effect.c), ahead of first use.
  - `effsub4.c:1863` `bhEff_AllocOwork` — same class of bug
    (`effsub4.h:1449` declared it non-static, definition is `static`,
    nothing else calls it). Fixed by deleting the stray header
    prototype (no forward-decl needed — the only reference is the
    definition itself).
- [x] **Step 4 — link-gap fixes**, after the two compile fixes above the
  build reached link stage cleanly for every `effsub*.c`. Undefined
  references fell into three buckets:
  - **Real, cheap, implemented for real:** `njFraction` (`ps2_NaMath.c:224`,
    "100% matching!" one-liner `n - floorf(n)`) added to `ninja_3d.c`
    alongside `njSqrt`/`njInvertSqrt`. `npCopyVlist` (`njplus.c:1722`,
    "100% matching!", only calls already-real `njMemCopy4`) extracted
    into `njplus_coli.c` following that file's existing
    extract-from-njplus.c precedent (same as `npSetAllMatColor`).
    `bhDrawScopeNumber` (sniper-scope HUD digits) and 8 more
    `bhSetScreenFade`/`bhControlCinesco`/etc functions all live in
    `screen.c` (1044 lines, zero PS2 asm, fully self-contained) — added
    the whole file to `RECVX_GAME_SOURCES` and deleted the now-shadowed
    stubs from `stub_game.c`/`game_room_stubs.c`.
  - **Real but not worth pulling in:** `CallYakkyouSe`/`StopSystemSe`
    (shell-casing SFX / stop-BGM triggers) live in `sdfunc.c` (3466
    lines) — tried adding it, but it transitively requires the real
    `sceMpeg`/ADX movie-audio SDK types (`sceMpegCbDataStr` etc in
    `ps2_MovieFunc.h`), an unrelated and much bigger gap. Backed
    `sdfunc.c` back out; `CallYakkyouSe`/`StopSystemSe` get clearly-
    commented no-op stubs instead (affected SFX just don't play).
  - **Genuine new rendering work, scoped out (as originally predicted):**
    `njDrawLine3D`, the `njDrawPolygon3DEx` family, the
    `njDrawTexture3DEx`/`njDrawTexture3DHEx`/`njDrawTexture` family,
    `njSetTextureNumG`, `njGetSystemAttr`/`njSetSystemAttr`,
    `njCnkModDrawModel`/`lCnkModClipFace`, `Ps2Shadow*` (5 functions),
    `njPtclPolygonStart`/`njPtclDrawPolygon`/`njPtclPolygonEnd`,
    `njPtclSpriteStart`/`njPtclDrawSprite`/`njPtclSpriteEnd`,
    `_nj_screen_`, `PS2_Render_Tex_Sub`/`PS2_Render_tex_sub_flag`/
    `Ps2CalcScreenCone`/`njRenderTextureNum(G)`/`njSetRenderWidth`/
    `njSetScreenProjection` (screen.c's render-to-texture chain) — all
    got clearly-commented no-op stubs in `game_room_stubs.c`, same
    "logic real, pixels deferred" shape as `light.c`'s Multi-light.
    `njRotateEx`/`njScaleEx`/`njTranslateEx` (originally flagged as part
    of this gap) turned out to already be real, added to `ninja_3d.c`
    back in Task 4.3's `zonzon1.c` integration.
  - **Duplicate-symbol cleanup:** `bhSetFontTexture` had an earlier
    hand-rolled substitute in `game_texture_stubs.c` (written before
    `effect.c` compiled, hardcoding "skip 4 blocks" since `ef_info[]`
    wasn't available) — deleted now that the real `effect.c` version
    (which reads the real `ef_info[21]` table) is compiled; the hardcode
    turned out to match anyway (4 of `ef_info`'s 21 entries have
    `flg & 1` set).
- **Real bug found+fixed (x64 pointer truncation), not a decomp
  inaccuracy:** first Xvfb smoke-test run segfaulted in
  `bhSetMemPvpTexture` reading a garbage `datp` pointer
  (`0xffffffffb8094120` — a sign-extended 32-bit value). Root cause:
  `effect.c`'s `bhInitEffect`/`bhSetFontTexture` both 32-bit-align a
  pointer via `(unsigned char*)(((int)dp + 31) & ~0x1F)` — harmless on
  the real PS2 (32-bit pointers) but on x64 this truncates `dp` to its
  low 32 bits before rebuilding the pointer, losing the high bits
  entirely. Same bug class already fixed elsewhere in this codebase
  (`system.c`'s `ALIGN_UP`/`ALIGN_DOWN` macro usage) — fixed both call
  sites the same way: `(unsigned char*)ALIGN_UP((uintptr_t)dp,
  (uintptr_t)32)`. This was invisible until now because `effect.c`
  itself was never compiled before this task.
- [x] **Step 5:** Deleted all now-real shadowed stubs: `bhClearEffect`/
  `bhSetEffect`/`bhSetEffectTb`/`bhSetShadow`/`bhDrawEffect` (effect.c),
  `bhInitEffect`/`bhControlEffect`/`bhDeleteYakkyou` (effect.c,
  `stub_game.c`), `bhSetScreenFade`/`bhControlScreenFade`/
  `bhDrawScreenFade`/`bhSetScreenSaver`/`bhControlScreenSaver`/
  `bhInitScreenSaver`/`bhDrawScreenSaver`/`Ps2_rendertex_initflag`
  (screen.c, `stub_game.c`), `bhControlCinesco`/`bhDrawCinesco`/
  `bhDrawScope`/`bhDrawThermometer`/`bhDrawSmallScreenRenderTexture`/
  `bhDrawFullScreenRenderTexture` (screen.c, `game_room_stubs.c`),
  `bhSetFontTexture` substitute (effect.c, `game_texture_stubs.c`).
- [x] **Step 6:** Build clean (zero errors/warnings-as-undefined-refs).
  Xvfb smoke test: same known item-select-screen ceiling as prior tasks
  (`timeout` exit 124, `[boot] clean exit`), no crash.
- [x] **Step 7:** Status table + progress overview updated above,
  committed.

---

### Task 4.3: Enemy AI roster — `eneset.c` dispatch + `en*.c` files

**Files:**
- Modify: `port/CMakeLists.txt` (add files incrementally, one commit per
  enemy or small batch — do not land all ~30 in one commit, per this
  project's own "surgical, verifiable steps" convention)
- Modify: `port/src/game_room_stubs.c` (delete each `bhEneNN` stub as its
  real file lands)

**Interfaces:**
- Consumes: `BH_PWORK`, collision (`hitchk.c`/`ps2_NaColi.c`, real),
  combat (`weapon.c`, Task 4.1), effects (`effect.c`, Task 4.2) — **this
  task should land after 4.1/4.2**, since enemy behavior calls into both.
- Produces: real `bhEne01`...`bhEne30`, `bhEne53`...`bhEne55`, `bhEne71`,
  `bhEne_InitDamage` (currently ~34 no-op stubs in `game_room_stubs.c`'s
  "enemy AI handlers" block, `port/src/game_room_stubs.c:17`-ish) wired
  into `eneset.c`'s `bhJumpEnemy[100]` dispatch table
  (`src/ps2/veronica/prog/eneset.c:51`).

Per-enemy files found so far (re-enumerate at execution time, this list
may be incomplete): `en01b.c`, `en02.c`, `en03.c`+`en03sub.c`, `en04.c`,
`en05.c`+`en05sub.c`, `en06.c`+`en06sub.c`, `en07.c`, `en09.c`, `en10.c`,
`en12.c`, `en13sub.c`, `en14.c`, `en15.c`, `en16.c`, `en17.c`, `en18.c`,
`en19.c`, `en20.c`, `en21.c`, `en22.c`, `en24.c`, `en25.c`, `en27.c`,
`en30.c`, `en54.c`, `en71.c`, plus shared helper `subpl.c`.

- [x] **Step 1:** file-level `grep -c "asm"` confirmed zero for every one
  of the 34 enemy dispatch files plus every helper file pulled in
  (`en01sub.c`, `en01b.c`, `zonzon.c`, `zonzon1.c`, `hitchkl.c`,
  `subpl.c`) — the entire enemy AI roster is asm-free.
- [x] **Step 2:** `en71.c` (114 lines, smallest) picked and landed as the
  proof-of-pattern integration, plus `Motion.c`/`subpl.c` as its real
  dependencies (`bhSetMotion`, `bhEne28`).
- [x] **Step 3/4: all 34/34 enemies real. Done.**
  - `en71.c` → `bhEne71` (+ `Motion.c`, `subpl.c`/`bhEne28`)
  - `en54.c`/`en55.c` → `bhEne54`/`bhEne55` (+ `zonzon.c`, first shared
    helper library: wall/floor/water collision wrappers, blood/fire/
    particle setup, motion-change helpers; added `njScalor2`)
  - `en20.c`/`en10.c`/`en27.c` → `bhEne20`/`bhEne10`/`bhEne27` (+
    `zonzon1.c`, second shared helper library: SE/voice triggers,
    blood/mince/acid effects, damage calc; added `njTranslateEx`/
    `njScaleEx`/`njRotateEx`; + `hitchkl.c`, line/segment collision)
  - `en08.c` → `bhEne08` (its `bhEne03_Collision` dep is UNIMPLEMENTED
    in the original decomp too — scePrintf + no return, en03.c:5046 —
    stubbed rather than pulling in en03.c just for that)
  - `en16.c` → `bhEne16` (fully self-contained, no deps beyond scePrintf)
  - `en24.c`/`en25.c` → `bhEne24`/`bhEne25` (same, self-contained)
  - `en11.c`/`en30.c` → `bhEne11`/`bhEne30` (same, self-contained)
  - `en12.c`/`en13.c`/`en14.c`/`en18.c`/`en21.c`/`en29.c` → six more,
    all self-contained
  - `en02.c`/`en04.c`/`en05.c`/`en06.c`/`en07.c`/`en09.c`/`en17.c`/
    `en19.c`/`en22.c`/`en23.c`/`en26.c` → eleven more in one batch;
    `en05.c`/`en07.c` needed `bhCalcModel`/`bhCheckEnemies`/
    `bhCheckPlayer`/`bhSetMotion`/`bhEne_GetPartsPos`/
    `bhEne_SetWeponAtr`, all already real from earlier tasks;
    `bhEne06_BR00` (en06.c:1727) turned out fully commented-out in the
    decomp (disassembly notes only, no C body ever generated) — same
    "never reversed" shape as `bhEne03_Collision`, stubbed to match
  - `en15.c` → `bhEne15` **and** `bhEne53` (both dispatch entries share
    this one file) — uses unprefixed generic helper names (`Init`/
    `Move`/`Die`/`Attack`/`Damage`/`Chase`/`Stand`/etc, all non-static)
    instead of the `bhEneNN_`-prefixed convention everything else
    follows; checked every one against the whole `prog/` and
    `port/src` tree for collisions before adding — none found
  - `en01.c` (9945 lines, biggest file) → `bhEne01`, needed two helper
    files: `en01sub.c` (3202 lines, defines `bhEne01Head`/`Arm`/`Leg`/
    `Cap`/`Worm`/`Bom`/`Scope`/`Parent`) and `en01b.c` (1983 lines,
    defines the `bhEne01_*TypeB` dispatch arrays + `bhEne01_RotNeck`);
    also needed `npSetAllMatColor`, added as a 5th verbatim extraction
    to `njplus_coli.c` (`njplus.c` lines 1375-1469, pure C, no asm)
  - `en03.c` (6671 lines, last of the 34) → `bhEne03`, fully
    self-contained; its `bhEne03_Collision` is the same
    UNIMPLEMENTED-in-source function `en08.c` depends on — the stub
    that used to live in `game_room_stubs.c` was deleted once the real
    (still scePrintf-only) definition came from `en03.c` itself
- [x] **Step 5:** all 34 `bhEneNN` stubs deleted from
  `game_room_stubs.c`'s "enemy AI handlers" block — only
  `bhEne06_BR00` remains as a stub, and it's a genuine
  never-reversed decomp gap, not an integration gap.
- [x] **Step 6:** status table above updated to "34/34 enemies — done".

**Behavioral verification note:** compiling an enemy file only proves it
links and doesn't crash on load — confirming each enemy's *actual combat
behavior* (attack patterns, damage taken/dealt) needs live gameplay
testing, which is still blocked by the same gdb pseudo-input harness
limitation noted in Tasks 1 and 3 (stuck at item-select screen). Static
verification (clean build + no crash) is the accepted bar until that
harness limitation is fixed or a real controller/keyboard test is done —
same standard the user already approved for collision.

---

### Task 4.4: Save/load — `ps2_McSaveFile.c` + `ps2_SaveScreen.c` + `ps2_SystemSaveScreen.c`

**Files:**
- Modify: `port/CMakeLists.txt` (added 8 decomp files, not 3 — see Step 1)
- Modify: `port/src/stubs/stub_game.c` / `port/src/game_texture_stubs.c`
  (deleted shadowed stubs)
- Create: `port/src/save_mc_shim.c` (real port-side reimplementation)

**Interfaces:**
- Consumes: PS2 memory-card I/O (`sceMc*` KATANA calls) and GD-ROM sector
  reads (`gdFsOpen`/`gdFsGetFileSize`/`gdFsRead`/`gdFsClose`) — both real
  hardware boundaries needing a **port-side reimplementation**, not just a
  CPU stand-in, following the `afs_mount.c` async-collapse precedent.
- Produces: `sceMc*`/`gdFs*` symbols the 8 decomp files below call.

- [x] **Step 1:** Grepped `game.c`/`room.c`/`adv.c`/`system.c`/`bup_00.c`
  for the actual save/load entry points and found the real scope is
  **8 files, not 3**: `ps2_MemoryCard..c` (low-level card state machine),
  `ps2_sg_bup.c` (`buInit`/`sceMcInit` wrapper), `ps2_McSaveFile.c`
  (SAVEFILE/CONFIGFILE/ICONINFORMATION init + file-select UI),
  `ps2_SaveScreen.c` + `ps2_LoadScreen.c` (item-menu "typewriter"
  save/load, dispatched from `bup_00.c`'s `TypewriterMode[]`, called via
  `bhSysCallTypewriter` in `system.c`), `ps2_SystemSaveScreen.c` +
  `ps2_SystemLoadScreen.c` (pause-menu system save/load, already called
  for real from `adv.c`), and `bup_00.c` itself. All 8 are zero PS2 asm.
  `stub_game.c` already had placeholder stubs for `CreateMemoryCard`/
  `GetMcSelectPortType`/`CheckMcSelectPortInfoState`/`CreateSysLoadScreen`/
  `ExecuteSysLoadScreen`/`CreateSysSaveScreen`/`ExecuteSysSaveScreen`/
  `TypewriterKeepMemory`, and `game_texture_stubs.c` had one for
  `ControlTypewriter` — all shadow-confirmed and deleted once the real
  files compiled.
- [x] **Step 2:** Identified the real API surface and designed
  `save_mc_shim.c` (game-target file, needs KATANA's real `GDFS_HANDLE`
  type from `sg_gd.h`, so can't live in the stub library):
  - `sceMc*` (`Init`/`GetInfo`/`Sync`/`Open`/`Close`/`Read`/`Write`/
    `Mkdir`/`Chdir`/`GetDir`/`Format`) — a virtual memory card backed by a
    real `<gamedata>/SAVE/` directory on disk. `ps2_MemoryCard..c`'s
    ~20 call sites already retry-loop on `sceMcSync` before touching the
    result, so (same as `afs_mount.c`) every `sceMcXxx` does its file I/O
    synchronously and `sceMcSync` always reports "done" on the next call.
    Extended `port/include/compat/libmc.h` with the full `sceMcTblGetDir`
    struct (`ps2_MemoryCard..c` declares a real `static sceMcTblGetDir
    CardInfo[21]` and reads `.EntryName`/`.AttrFile`, not just a pointer)
    and the rest of the prototypes.
  - `gdFsOpen`/`gdFsGetFileSize`/`gdFsRead`/`gdFsClose` — discovered via
    `ps2_McSaveFile.c`'s `mcReadIconData`, which is **not** a stubbable
    dead path: `ExecuteStateSysSaveWriteSysData` case 2 treats a failed
    read as a card error, so every system save goes through it. It's only
    ever asked for one file, `"bio_cv.ico"`, which ships as a loose file
    at the gamedata root (`BIO_CV.ICO`, confirmed present in
    `/iso-src/data`) — implemented as a case-insensitive scan of the
    gamedata dir, same technique as `afs_mount.c`'s
    `port_loose_find_path`.
  - Found and worked around a latent, pre-existing bug in
    `recvx_game_prelude.h`: its own "real CRT first" `#include <string.h>`/
    `<stdio.h>` actually resolves to KATANA's SH-series copies (the KATANA
    include dir is on the -I path for the whole TU, ahead of the system
    default, and the `_STRING_SHC` etc. masking guards aren't defined
    until after those includes) — invisible until now because no
    previously-compiled file happened to call plain `strcpy`/`strcmp`
    (KATANA's copy macros them to `_builtin_strcpy`/`_builtin_strcmp`,
    the original Hitachi SH-C compiler's intrinsic names, meaningless to
    GCC) or `snprintf` (predates C99). `ps2_MemoryCard..c`/
    `ps2_McSaveFile.c` are "100% matching!" decomp and do call plain
    `strcpy`/`strcmp`, so rather than touching the shared prelude (bigger
    blast radius), `save_mc_shim.c` provides real `_builtin_strcpy`/
    `_builtin_strcmp` definitions (manual loops, not `strcpy`/`strcmp`
    calls — those would macro right back into themselves) and an
    explicit `snprintf` prototype.
- [x] **Step 3:** Compiled all 8 files + `save_mc_shim.c`. Docker build
  clean, Xvfb smoke test clean (`[boot] clean exit`, same known
  item-select-screen input-harness ceiling as every prior task).
- [x] **Step 4:** Full UI round-trip is still blocked by the same
  gdb-pseudo-input harness limitation as Tasks 1/4.3, so verified the
  shim directly instead: a standalone test linked straight against
  `save_mc_shim.c.o` did `sceMcMkdir` → `sceMcOpen`(create) →
  `sceMcWrite` → `sceMcClose` → `sceMcOpen`(read) → `sceMcRead` →
  `sceMcClose` and got back the exact bytes written; `sceMcGetDir`
  wildcard listing found the file with the correct name/size; opening a
  nonexistent file for read correctly returned -1. A second standalone
  test called `gdFsOpen("bio_cv.ico", ...)` against the real `/iso-src/
  data` gamedata dir and got the correct 46072-byte size and valid PS2
  `.ICO` magic bytes back. Stronger bar than the usual "compiles, doesn't
  crash" since this task introduced new PC-side logic, not just
  recompiled decomp.
- [x] **Step 5:** Status table updated, commit made.

**Behavioral verification note:** same caveat as Task 4.3 — compiling and
shim-testing proves the plumbing is correct, not that the in-game save/
load UI is fully walkable, which needs the same real-input testing this
whole plan has deferred since Task 1.

---

### Task 4.5: Full-playthrough crash sweep

**Files:** `port/src/save_mc_shim.c`, `src/ps2/veronica/prog/{system.c,bup_00.c,
pwksub.c,hitchkl.c,ranking.c,map.c,message.c}`.

- [x] **Step 1 (harness):** The gdb-pseudo-input ceiling from Tasks 1/4.3
  turned out to be a real, fixable bug, not a test-environment limit (see
  findings below) — once fixed, **real synthesized `xdotool` input**
  (window-focus + spaced keydown/keyup, the technique proven in the P2
  log) reliably drives the actual title menu: Start dismisses the
  first-boot "system file broken" prompt, `Up` moves the cursor from the
  default Load-game selection to New Game (a memory card is now
  genuinely detected since Task 4.4 landed, so the old "no card → default
  to New Game" shortcut no longer fires — correct, authentic behavior),
  Start confirms it (`Adv_BioCvTitle` returns 2, the real
  confirmed-New-Game code), and a `CheckButton`-forcing gdb breakpoint
  (`set variable AdvWork.Cursor[0] = 1` on every hit) makes this
  reproducible on demand instead of racing a ~1s input window.
- [x] **Step 2: six real bugs found and fixed, root-caused via
  `systematic-debugging` (read error → reproduce → trace backward → one
  fix at a time → rebuild → reverify), each one moving the crash point
  further into previously-unreached territory:**
  1. **`sceMcSync` never reported "idle."** (`save_mc_shim.c`)
     `ExecuteMemoryCardStandby` (`ps2_MemoryCard..c`) gates its
     background card-connect poll on `sceMcSync(1,...) == -1` ("nothing
     kicked"); the shim always returned 1, so that poll never ran and
     `GetMemoryCardSelectPortState` stayed at its zero-initialized
     default forever — the system-load screen's card-awareness check
     span an infinite error-recovery loop with **no timeout**, a hard,
     deterministic stall present since Task 4.4 landed (this is what all
     of Tasks 4.2–4.4's "same known item-select-screen ceiling" log
     entries were actually stuck on — never really an item-select
     screen). Fixed with a `g_mc_pending` flag: `sceMcSync` now only
     reports "done" for an op the shim actually kicked, `-1` otherwise,
     matching real hardware semantics.
  2. **`sceMcGetInfo`'s `format` output was inverted.** (`save_mc_shim.c`)
     Returned `0` with a `/* formatted */` comment; every real caller
     (`ps2_SystemLoadScreen.c`) checks `== 1` for "formatted OK" and `==
     0` for "needs format," so the shim was telling the game its own
     virtual card was corrupt. Fixed to `1`.
  3. **`bhFirstGameStart`'s `sys->memp = keepmem` reset staled
     `sys->mes_sp`.** (`system.c`) Same hazard class as the file's own
     pre-existing case-1 fix, at a second site nothing had guarded yet.
  4. **`bhSysCallMonitor`'s case 3 overwrites the same `sys->memp` the
     case-2 `mes_sp` still points into**, and does so *before*
     `NowLoadDisp` is even set (case 4). On real hardware the disc read
     this kicks off takes many real frames — long enough that the "Now
     Loading" text never actually gets drawn against the still-fresh
     data. Our synchronous I/O shim collapses that wait to nothing, so
     the draw can happen against already-overwritten memory within the
     same handful of frames, walking an **unbounded** loop
     (`cd = *dp++` with no bounds check, only a `0xFFFF` sentinel) until
     it wanders off the allocated pool — a real, x64-only SIGSEGV.
     `bup_00.c`'s Typewriter task has its own near-identical copy of this
     exact case0-3 sequence with the same hazard at its own case 2. Both
     fixed the same way as the file's own precedent: null `mes_sp` right
     at the overwrite point rather than leaving it dangling.
  5. **Fixing #3/#4 by nulling `mes_sp` exposed that `bhDispMessage`,
     `bhDispMessageEx`, and `bhSetMessage` (`message.c`) never actually
     checked it for NULL themselves** (only one specific caller,
     `bhSysCallMonitor`'s NowLoadDisp, happened to guard its own call
     site) — so the fix in #3/#4 just moved the same crash from "read
     stale garbage" to "read through NULL," now from a third call site
     (`ps2_SaveScreen.c`'s `SetDispSelectMessage`, i.e. the save screen's
     own on-screen message text). Fixed at the root by guarding all
     three consumers directly instead of chasing individual call sites:
     skip the draw (return early) when `mes_sp`/`mes_dp` is NULL. Every
     affected caller is a cosmetic text label, so a skipped draw for the
     handful of frames the pointer is intentionally NULL is the same
     "one frame of visual glitch, not a functional loss" trade the real
     hardware already makes with its stale-but-mapped mirrored memory.
  6. **Three more x64 pointer-truncation sites** in the established
     `(int)ptr + N) & ~mask` family (same bug class fixed repeatedly in
     P2-P4): `pwksub.c`'s `bhGetFreeMemory` itself (the single most
     widely-called allocator in the entire codebase — this one had been
     silently truncating `sys->memp` on **every single allocation** the
     whole time), `hitchkl.c` (line-segment collision, would have hit
     during real combat), and four sites in `bup_00.c`/`ranking.c`/
     `map.c` written in the `(unsigned int)(ptr + N) & ~mask` variant
     form. Fixed with the established `ALIGN_UP((uintptr_t)ptr, N)`
     pattern; `ranking.c` isn't currently compiled into
     `RECVX_GAME_SOURCES` so its fix is inert until that file is pulled
     in, but correct and harmless to have landed now.
- [x] **Step 3: verified a real, menu-driven, input-confirmed New Game
  now reaches actual room gameplay with zero crashes** — title screen →
  Start → cursor to New Game → Start confirms (`Adv_BioCvTitle` returns
  2) → MV_000 intro movie plays and Start-skips cleanly → real room load
  (`rm_0000.rdx`, the authentic New Game starting room, not the demo
  path's `rm_0130`) → sustained real `Up`/`Left` movement input with
  correct edge-detected press/release logging and sound effects firing
  → Cross (attack/action) and Triangle (inventory) presses register and
  are handled without crashing. This is the first time in the project's
  history this state was reached through **real synthesized menu input**
  end-to-end, not a gdb-forced shortcut.
- [ ] **Step 4 (new finding, not yet fixed):** walking with `Up`+`Left`
  triggers a **new, distinct SIGSEGV** in enemy AI, unrelated to the
  memory-card/message-table bugs above:
  ```
  SetMtnNormal (ewP=<ene>, datP=<NormalTbl>, mode=32) at Motion.c:156
  156:            if (md2P->p[0] != NULL)
  #1 bhSetMotion (...) at Motion.c:56
  #2 em60_init (epw=<ene>) at subpl.c:127
  #3 init_subpl (epw=<ene>) at subpl.c:91
  #4 bhSubpl (epw=<ene>) at subpl.c:81
  #5 bhControlEnemy () at eneset.c:388
  #6 bhMainSequence () at game.c:48
  ```
  `md2P` (`ewP->mnwP[ewP->mtn_no].md2P`) and `obj_no` both hold obviously
  garbage values (a wild pointer and a >1.6M "object count") at the
  crash, meaning this specific `ene[]` slot's `mnwP` (motion-work array)
  was never populated by the room-load enemy-linkage loop
  (`room.c:444`, bounded by `rom->ene_n`) before `bhControlEnemy`'s own
  loop (`eneset.c:369`, bounded by `sys->ewk_n`) tried to run its motion
  init — the same *shape* of bug as the P2-era `ewk_n > rom->ene_n`
  class (already fixed for `bhInitEnemy`'s zero-fill), but not yet
  confirmed to be the *same* root cause here; needs its own
  `systematic-debugging` pass (trace how/where this specific enemy
  `id`/slot enters `ene[]` outside the room's counted list) rather than
  a guessed guard. Intermittent — reproduces on some movement paths, not
  the first attempt, consistent with proximity-based enemy activation.
  This is the concrete next blocker for Task 4.5's "full loop survives"
  bar.
- [x] **Step 4b (this pass, 3 more real bugs found+fixed, 1 more found
  and documented unfixed):** re-ran the same gdb-driven New Game repro
  (no manual movement this time — these all surfaced from the game's own
  background enemy processing, which `bhControlEnemy` runs
  unconditionally every frame regardless of player input) and hit
  crashes in sequence, each unblocking the next:
  1. **`bhEne07` (en07.c) — entire enemy type is an undecompiled stub.**
     `bhEne07_Init`/`_Move`/`_Nage`/`_Damage`/`_Die`/`_CollisionWalls`/
     `_FloorCollision`/`_PlayerControl` are all bare
     `scePrintf("...UNIMPLEMENTED!\n")` stubs — none ever populate
     `epw->exp0`/`exp1`/`mtn_tp`. The wrapper (`bhEne07`, "100%
     matching") unconditionally dereferences all three every frame.
     First guarded `exp0`/`exp1` individually — that just moved the
     crash to the next untouched field (`mtn_tp`, via `bhSetMotion` →
     `SetMtnFast`, `Motion.c:357`). Recognized the whack-a-mole and
     replaced both guards with one root-level early return right after
     the (no-op) `Mode0` dispatch, `#ifdef RECVX_PC_PORT`-gated, leaving
     the original decompiled logic untouched for a future real port of
     this enemy.
  2. **`en01.c:3078` — read narrower than its own write.** Line 2929
     stores a linked-enemy pointer with `*(void**)(epw->exp0 + 0x14) =
     epp;` (correct, full pointer width); line 3078 read it back with
     `*(int*)(...)` (4 bytes only). Fixed the read to match the write's
     width (`*(void**)`). Real, necessary, and correct — but not
     sufficient alone (see finding 3 below).
  3. **`effect.c` — 9 draw functions read pointer arrays at the wrong
     stride.** `bhDrawPolEffect`/`MdfEffect`/`LinEffect`/`NtxEffect3D`/
     `TrsEffect3D`/`OpqEffect3D`/`ThlEffect3D`/`NtxEffect2D`/`TrsEffect2D`
     all took `unsigned int* owp` and did `op = (O_WRK*)*owp++;` over
     arrays declared `O_WRK* ef_xxx[512]` in `types.h` — real 8-byte
     pointers walked with a 4-byte stride/read. Harmless on PS2 (pointer
     == unsigned int width there) but on x64 either truncates every
     entry to its low 32 bits or reads two entries per real one,
     corrupting the walk. Crashed in `bhDrawTrsEffect3D` the first time
     any transparent effect (`ef_trs`) was queued. Fixed at the type
     level: all 9 signatures (header + definitions) changed to
     `O_WRK** owp`, all 11 call sites' now-redundant `(unsigned int*)`
     casts dropped — zero changes to the "100% matching" function
     bodies, since `*owp++` already does the right thing once `owp` has
     the right type.
  4. **`en01.c` — NEW, unfixed, architectural: overlapping 8-byte writes
     at 4-byte-spaced legacy offsets.** Once the read-width fix above
     was in place, the *same* crash site (line 3078/3083) still faulted,
     now on a garbage-but-non-NULL `epp`. Root cause: `bhEne01_Init`
     stores SIX linked-enemy pointers via `*(void**)(epw->exp0 + N)` at
     `N = 0x0, 0x4, 0x8, 0xc, 0x10, 0x14` — offsets only 4 bytes apart,
     sized correctly for 32-bit PS2 pointers packed tightly, but each
     `void**` store on x64 writes a full 8 bytes. Every write's upper 4
     bytes stomp the *next* slot's lower 4 bytes: the write at `0x10`
     spans bytes `0x10`-`0x17`, so by the time code reads `0x14` back,
     it gets the high 32 bits of whatever pointer was stored at `0x10`
     instead of the intended (usually-unwritten, should-be-zero) value.
     Confirmed by direct memory inspection: the "garbage" pointer read
     was exactly `0x0000573b` — the upper 32 bits of this run's ASLR
     base, i.e. the high half of the `0x10` slot's real pointer, exactly
     as the overlap theory predicts. This is contained to `en01.c` (10
     occurrences of the pattern, all in one function), but fixing it
     properly means widening every slot to 8-byte spacing and updating
     every hardcoded offset (and any `EXP0_I(N)` reader elsewhere that
     assumes the old 4-byte packing) consistently — a real restructuring
     job, not a one-line guard. Left unfixed and documented rather than
     patched with a guess, per the same "root cause first" standard as
     the rest of this task; **this is now the concrete next blocker**
     (the previous blocker, item below, is superseded — this session's
     repro never reached the point of retriggering it, since these three
     bugs crash the game earlier in the same automatic enemy-processing
     path).
- [ ] **Step 5:** Once the `en01.c` packed-offset corruption above is
  fixed (or the enemy types that hit it are confirmed unreachable on the
  tested path) and a full loop (start → fight → save → reload) survives
  without a `RECVX_BUILD_GAME`-only crash, mark P4 done in the Progress
  overview table at the top of this doc and open P5 scoping.

**Previous blocker (Step 4 finding, not retested this pass):** walking
with `Up`+`Left` previously triggered a distinct SIGSEGV in
`SetMtnNormal` (`Motion.c:156`) via `em60_init`/`subpl.c` — this pass's
repro used only the `Start`-button gdb harness (no manual movement), so
this specific path was never re-exercised; it may still exist
independently of the three bugs above.
```
SetMtnNormal (ewP=<ene>, datP=<NormalTbl>, mode=32) at Motion.c:156
156:            if (md2P->p[0] != NULL)
#1 bhSetMotion (...) at Motion.c:56
#2 em60_init (epw=<ene>) at subpl.c:127
#3 init_subpl (epw=<ene>) at subpl.c:91
#4 bhSubpl (epw=<ene>) at subpl.c:81
#5 bhControlEnemy () at eneset.c:388
#6 bhMainSequence () at game.c:48
```
`md2P` and `obj_no` both held garbage at the time, consistent with the
established `ewk_n > rom->ene_n` enemy-linkage bug shape from P2, but not
confirmed to share the exact root cause.

**Separate finding, out of scope for this pass:** a broad grep for the
`(int)ptr + offset)` x64-truncation pattern turned up roughly two dozen
more occurrences project-wide (`binfunc.c`, `door.c`, `adv.c`,
`ps2_loadtim2.c`, `ps2_sg_sd.c`, `ps2_dummy.c`, `ps2_NaTextureFunction.c`,
`ps2_sg_gd.c`, `event.c`, `njplus.c`), mostly in binary-blob/model-offset
math rather than the `sys->memp` bump allocator specifically. None of
these were on the call path for this session's crashes, so none were
touched — but this is clearly a systemic pattern worth its own dedicated
sweep-and-fix task rather than opportunistic fixes each time one is
tripped over.

---

## Phase 5 (roadmap only — not detailed yet)

| Phase | What it needs | Depends on |
|---|---|---|
| P5 | Post-playable improvements — network (Battle Mode), audio/graphics upgrades. Not scoped: needs its own spec once P4's actual codebase shape is known | P4 |

---

## Self-Review

**Spec coverage:** P1 (the only currently-blocking, concretely-scoped phase) is
fully broken into tasks with real code, real commands, real verification gates.
P2-P5 are intentionally left as a roadmap, not fake bite-sized steps — writing
fictional TDD steps for gameplay code we haven't seen compile yet would be
placeholder-plan territory ("implement whatever crashes says to implement").

**Placeholder scan:** no TBD/"handle appropriately" text in Task 1 or the Task 1
smoke test. Task 2's steps are explicitly framed as a debug loop (not disguised
as fixed steps) because the errors are genuinely unknown — flagged rather than
faked.

**Type consistency:** `recvx_alloc_low4g(size_t) -> void*` matches the existing
Windows branch's signature exactly (`port/src/tex_pool_alloc.c:31`) and the
consumer's extern declaration (`port/src/game_texture_stubs.c:103`).

**2026-07-13 update — Phase 4 detail added:** P4 was expanded from a
one-line roadmap row into a full task breakdown once Tasks 1–3 (collision,
lighting, `light.c`) landed and the remaining scope's file list was known.
Kept honest to the "no fake TDD steps" rule above: Tasks 4.1/4.2/4.4 give
real file/function lists (verified by direct `grep`/`wc -l` against the
actual decomp source during this planning pass) and a concrete, proven
integration procedure (shadow-check via `comm -12`, add to CMakeLists,
build, fix gaps, delete stubs, smoke-test, commit) rather than invented
code, since the exact compile errors each file will hit are genuinely
unknown until attempted — same reasoning as the original P2 debug-loop
framing. Task 4.3 (34 enemy files) is deliberately a repeatable procedure
applied per-file rather than 34 bespoke task blocks, since writing unique
steps for AI behavior not yet read in detail would itself be
placeholder-plan territory. Task 4.0 folds P3's paused Windows/MSVC work
into P4 per the project owner's explicit instruction to gather all P4
work, including paused sub-tasks, into one place — it stays conditional
and may never trigger.
