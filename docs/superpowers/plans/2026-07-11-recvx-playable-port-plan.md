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
| **P1** | `RECVX_BUILD_GAME=ON` compiles & links on Linux | 🟡 Not started | 0% |
| **P2** | Game actually boots to title/gameplay on Linux (real input, real room load) | ⬜ Blocked on P1 | 0% |
| **P3** | Windows parity pass (MSVC build of the same `RECVX_BUILD_GAME=ON` config) | ⬜ Blocked on P1/P2 | 0% |
| **P4** | "Playable" gate: full room traversal, combat, save/load, no `RECVX_BUILD_GAME`-only crashes | ⬜ Blocked on P2/P3 | 0% |
| **P5** | Post-playable improvements (network Battle Mode, HD assets, graphics) | ⬜ Not scoped yet | 0% |

This document details **P1** task-by-task (concrete, plannable now). P2–P5 are
listed as a roadmap only — they get their own detailed plan once P1 lands, since
their scope depends on what P1 turns up (matches how `recvx-decomp-port`'s own
`RECVX_BUILD_GAME` option was never actually exercised standalone before today).

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

- [ ] **Step 6: Update this plan's progress table**

Set P1 to ✅ 100% once `recvx_pc` links with `RECVX_BUILD_GAME=ON`. Record actual
time spent and error count found (for calibrating how risky P2/P3 will be).

---

### Task 3: Smoke-test the game boot on Linux (headless, `RECVX_BUILD_GAME=ON`)

**Files:** none (verification-only task).

**Interfaces:**
- Consumes: Task 2's linked `recvx_pc`.
- Produces: a documented boot log — either "boots to title/gameplay" (great) or
  "crashes at X" (expected per the source comment in `main_pc.c:11`: "*The
  --game path will crash / misrender until more of the decomp is brought in*" —
  this task's job is to find out exactly where, not to fix it yet).

- [ ] **Step 1: Run against the real ISO under xvfb, capture the boot log**

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

- [ ] **Step 2: Read the log, note the exact crash point (or confirm it boots further than expected)**

No fixed expected output — this step's deliverable is a written note (in this
plan's progress table or a new dated entry in
`docs/superpowers/specs/2026-07-11-recvx-setup-design.md`) of where it stops:
clean menu, crash with signal, hang, or garbage render. This becomes the seed
for Phase 2's task list once P1 finishes.

---

## Phase 2–5 (roadmap only — not detailed yet)

| Phase | What it needs | Depends on |
|---|---|---|
| P2 | Fix whatever Task 3 found; wire real `--gamedata` extraction if needed; get from boot to actual controllable movement | P1 |
| P3 | Same `RECVX_BUILD_GAME=ON` config built with MSVC on Windows — likely far less risky than P1 since the game source was written against MSVC originally; mostly a "does it still build" check plus any regressions from P1/P2's Linux-motivated edits | P1, P2 |
| P4 | Definition of "playable": full room traversal, save/load, combat, no engine-level (not gameplay-content) crashes | P2, P3 |
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
