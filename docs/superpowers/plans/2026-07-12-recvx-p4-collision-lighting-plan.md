# RECVX P4 — Collision (`hitchk.c`) & Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring real room/entity collision (`hitchk.c`) and a first working pass of
chunk-model lighting (`njCnkSetEasyLight*`) into the `recvx-decomp-port` Linux build,
replacing the no-op stubs that have covered both subsystems since P1/P2.

**Architecture:** Both tasks follow the established "extract/compile real decomp
file, delete the now-conflicting stub, fix whatever the compiler finds" pattern
used throughout P1/P2 (see the `bhAddSpeed`/`njInitTexture` fixes). Collision is a
straight file-addition — every dependency `hitchk.c` needs is already compiled.
Lighting is different: the real per-vertex lighting math lives in PS2 VU0
microcode (`vsm/ps2_vu0.vsm`) that cannot be compiled for PC, so this plan scopes
lighting down to a from-scratch CPU-side Lambertian approximation using data the
renderer already parses but doesn't use yet (per-vertex normals in `ninja_cnk.c`).

**Tech Stack:** C (decomp source + port layer), CMake/Ninja, GCC (Linux dev loop;
MSVC parity is P3, currently paused).

## Global Constraints

- Fidelity rule (unchanged from P1, `2026-07-11-recvx-playable-port-plan.md` line 25):
  only touch platform/toolchain-compat code when adjusting decomp source; never
  change original gameplay logic/values.
- Build/verify on Linux only (`x64-linux-vcpkg` preset) — P3/MSVC is paused.
- Every stub removed in this plan is a hard duplicate-symbol **link** error if
  missed, not a silent runtime bug — if the linker is clean, the removal was
  complete.

---

## Task 1: Real collision — compile `hitchk.c`, delete the 16 shadowing stubs

**Files:**
- Modify: `port/CMakeLists.txt:332` (insert new source line after the
  `ps2_NaColi.c` entry)
- Modify: `port/src/game_room_stubs.c` (delete 16 duplicate function bodies —
  exact lines below)
- Test: manual smoke-test via the existing gdb input-forcing harness (no
  automated test framework exists in this repo; matches how P1/P2 verified
  gameplay fixes)

**Interfaces:**
- Consumes: `ATR_WORK`/`BH_PWORK` types (already in `types.h`), `njSqrt`/
  `njUnitVector`/`njInnerProduct`/`njScalor`/`njSinCos` (already real, compiled
  in `port/src/ninja_3d.c`), `njDistanceP2L`/`njDistanceL2PL`/
  `njCollisionCheckBS` (already real, compiled via `ps2_NaColi.c`),
  `bhSetMessage` (already real, `message.c`), `rom->walp`/`rom->wal_n` (already
  populated every room load by `room.c:92,126`).
- Produces: real `bhCheckWall`, `bhCheckWallEx`, `bhCheckWall2Box`,
  `bhCheckWallType`, `bhCheckWallType2`, `bhCheckWallRefAngle`,
  `bhSetWallRefAngle`, `bhGetGroundPosition`, `bhCheckInnerTriangle{,2,3}`,
  `bhCheckBox`, `bhCheckBox2Box`, `bhCheckInnerP4`, `bhCheckExmAtari`,
  `bhSetUseKaidanFlag`, `bhClrUseKaidanFlag`, `bhSetDansaLimitAtari`,
  `bhCheckDansaAtari`, `bhCheckFloorP`, `bhCheckDansa`, `bhCheckFloorSound`,
  `bhCheckFloorEnemy`, `bhCheckFloorEffect`, `bhCheckWater`, `bhCheckL2Water`,
  `bhResetAtariAttr`, `bhCheckPlayer`, `bhCheckEnemies`, `bhCheckWallAttrB89` —
  consumed by `player.c`/`eneset.c`/`game.c` (already compiled, currently
  calling the no-op stubs).

- [ ] **Step 1: Delete the 16 duplicate stub bodies from `game_room_stubs.c`**

These exact lines currently shadow functions `hitchk.c` will provide. Delete
each (confirmed via `comm -12` against `hitchk.c`'s real defined-symbol list —
this is the complete and exact set, not an approximation):

```c
/* DELETE from the "---- collision / floor (hitchk.c) ----" block: */
void bhCheckEnemies(BH_PWORK* pp)     { (void)pp; }
void bhCheckPlayer(BH_PWORK* pp)      { (void)pp; }
void bhCheckWall(BH_PWORK* pw)        { (void)pw; }
void bhCheckWall2Box(BH_PWORK* pw)    { (void)pw; }
ATR_WORK* bhCheckWallType(NJS_POINT3* pos, unsigned int flg, float ar, float ah)
                                      { (void)pos;(void)flg;(void)ar;(void)ah; return NULL; }
ATR_WORK* bhCheckWallType2(NJS_POINT3* pos, unsigned int flg, float aw, float ad, float ah, int idx_ct)
                                      { (void)pos;(void)flg;(void)aw;(void)ad;(void)ah;(void)idx_ct; return NULL; }
void bhResetAtariAttr(void)           {}

/* DELETE from the "---- player.c dependencies ----" block further down: */
void  bhCheckExmAtari(BH_PWORK* pp) { (void)pp; }
ATR_WORK* bhCheckFloorEffect(int flr_no, float px, float pz) { (void)flr_no;(void)px;(void)pz; return NULL; }
void  bhCheckFloorP(BH_PWORK* pp) { (void)pp; }
int   bhCheckFloorSound(BH_PWORK* pp, int flr_no, float px, float pz) { (void)pp;(void)flr_no;(void)px;(void)pz; return 0; }
int   bhCheckWallEx(BH_PWORK* pw, NJS_POINT3* npos, NJS_POINT3* opos, float par, float pah)
                                      { (void)pw;(void)npos;(void)opos;(void)par;(void)pah; return 0; }
ATR_WORK* bhCheckWater(NJS_POINT3* pos) { (void)pos; return NULL; }
void  bhClrUseKaidanFlag(BH_PWORK* pp) { (void)pp; }
float bhGetGroundPosition(NJS_POINT3* pos) { return pos ? pos->y : 0.0f; }
void  bhSetUseKaidanFlag(BH_PWORK* pp, ATR_WORK* exp, int idx) { (void)pp;(void)exp;(void)idx; }
```

**Do NOT delete** `bhCheckFloorNum`, `bhCheckL2Wall`, `bhSetFloorNum`,
`bhCheckClipModel` — these four are declared in `pwksub.h`, not `hitchk.h`;
their real implementations live in `pwksub.c`, which stays uncompiled. They
are not touched by this task.

- [ ] **Step 2: Add `hitchk.c` to `RECVX_GAME_SOURCES`**

In `port/CMakeLists.txt`, immediately after line 332 (the `ps2_NaColi.c` entry
and its comment), insert:

```cmake
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
```

- [ ] **Step 3: Rebuild and fix whatever the compiler finds**

This step is inherently exploratory, same framing as every prior "compile a new
decomp file for the first time" task in this project (P1 Task 2, P3 Task 2) —
`hitchk.c` has never been compiled outside MWCC before. Treat as a bounded
debug loop:

```bash
cmake --build port/build 2>&1 | tail -100
```

For each error: find root cause before fixing (check `git log -p --follow --
<file>` if the cause isn't obvious), apply the same fidelity rule as always
(platform/toolchain-compat fixes only), fix minimally, rebuild, repeat until
clean. Expected error classes based on prior files in this codebase: x64
pointer-truncation in struct-offset math (same pattern as `dread.c`/
`eneset.c`'s `ALIGN` fixes), possibly an unmatched `bhSetWallRefAngle`/
`bhCheckWallRefAngle` if those two (present in `hitchk.h` but not in the
16-stub overlap, meaning they were never stubbed at all — first-time link) hit
a missing dependency.

- [ ] **Step 4: Link check — confirm zero duplicate-symbol errors**

```bash
cmake --build port/build 2>&1 | grep -i "multiple definition\|duplicate symbol"
```

Expected: no output. Any hit here means Step 1 missed a stub — go back and
delete it.

- [ ] **Step 5: Smoke-test real wall collision**

Using the same gdb input-forcing technique established for prior movement
verification (force a held analog-stick direction via
`recvx_input_set_stick`), walk the player directly into a known room wall for
~2 seconds of forced input and screenshot the result.

**Before this task:** player position was unclamped — forced input would walk
the player through walls indefinitely (no collision existed).
**After this task, expected:** player position stops advancing into the wall
(clamped by `bhCheckWall`'s real `ATR_WORK` box test) while forced input
continues, instead of clipping through geometry.

```bash
docker exec recvx_p3 bash -lc 'cd /workspace/port && cmake --build build 2>&1 | tail -30'
# then re-run the gdb-forced-movement harness from the P2 screenshot session,
# aimed at a wall instead of open floor, and diff player x/z logged per-frame
# before vs. after this change.
```

- [ ] **Step 6: Commit**

```bash
git add port/CMakeLists.txt port/src/game_room_stubs.c
git commit -m "$(cat <<'EOF'
feat: compile real hitchk.c, remove 16 shadowing collision stubs

Room/floor/entity collision was fully stubbed since P1 despite every
dependency it needs (ninja_3d.c math, ps2_NaColi.c line/plane tests,
room.c's already-relocated wall/floor attribute arrays) already being
compiled and populated every room load. hitchk.c itself has zero PS2
asm. Deletes the 16 no-op stubs hitchk.c now shadows; the remaining
game_room_stubs.c placeholders (bhCheckFloorNum, bhSetFloorNum,
bhCheckClipModel, bhCheckL2Wall — all really pwksub.c, still
uncompiled) are untouched.
EOF
)"
```

**Actual results (2026-07-13):** Steps 1-4 completed exactly as planned —
compiled clean on the first try (only pre-existing benign warnings: implicit
`bhSetEffect`/`bhSetMessage`/`scePrintf` declarations, matching the plan's
prediction that those two calls fall back to existing stubs), zero
duplicate-symbol or undefined-reference errors on rebuild. Committed as
`507c9110`.

Step 5 (live wall-collision smoke-test) did not complete: the generic
Start-mashing gdb harness (`screens.gdb`/`movetest7.gdb`-style — force Start
every frame until `sys->tk_flg == 0x0073dfc0`, then force a held stick
direction) reliably idles on the post-intro item-select screen
(`AdvWork.Mode=0 Mode2=10`) around frame ~7100-7500 and never reaches
free-roam gameplay, in two separate attempts. This matches a dead-end already
noted in the P2 screenshot session. Reaching real controlled gameplay before
(the P2 movement-fix work) used interactive `xdotool` key events, not this
gdb-only script — reproducing that here was judged not worth the added time
for this task; user decision was to accept static verification (clean
compile/link, every external symbol independently confirmed already
compiled) as sufficient for this commit. Live-in-room verification of real
wall collision remains an open follow-up if/when the interactive test harness
is revisited.

---

## Task 2: Lighting — real `njCnkSetEasyLight*` setters + CPU-side shading

**Scope note:** this task deliberately does **not** attempt full dynamic
per-room lighting (that requires compiling `light.c`, which itself calls ~20
more `njCnk*`/`njCnkSetSimple*` variants whose real-vs-VU0-asm status hasn't
been individually verified — out of scope here, flagged as follow-up at the
end of this task). This task's target is the smallest change that replaces
flat, always-white rendering with real directional shading, proving the
pipeline works.

**Files:**
- Modify: `port/src/game_room_stubs.c` (remove 3 stub functions + their
  backing globals' stub state, replace with the real decomp bodies)
- Modify: `port/src/ninja_cnk.c` (add Lambertian shading in
  `cnk_emit_strip_tri`)
- Modify: `port/src/main_pc.c` or `port/src/ninja_3d.c`'s room-load path
  (one hardcoded initial light call — see Step 3)

**Interfaces:**
- Consumes: `njCalcVector(NJS_MATRIX* m, NJS_VECTOR* vs, NJS_VECTOR* vd)`
  (already real, `port/src/ninja_3d.c:293` — rotates a vector by `m`'s 3x3
  part, ignoring translation, exactly what's needed to bring an object-space
  normal into the same space the model matrix already puts vertex positions
  in), `njInnerProduct`/`njUnitVector` (already real, `ninja_3d.c`),
  `pNaMatMatrixStuckPtr` (already an extern in `ninja_cnk.c:54`), `CNK_LIGHT`/
  `VU1_COLOR` types (already declared, `include/ps2/veronica/prog/types.h:1787-1826`).
- Produces: real `njCnkSetEasyLight`, `njCnkSetEasyLightColor`,
  `njCnkSetEasyLightIntensity`, plus the `NaCnkLightEs`/`NaCnkAmbientEs`
  globals they write, now consumed by `ninja_cnk.c`'s triangle emitter.

- [ ] **Step 1: Replace the 3 stub setters with their real decomp bodies**

In `port/src/game_room_stubs.c`, delete:

```c
void njCnkSetEasyLight(Float x, Float y, Float z) { (void)x;(void)y;(void)z; }
void njCnkSetEasyLightColor(Float r, Float g, Float b) { (void)r;(void)g;(void)b; }
void njCnkSetEasyLightIntensity(Float inten, Float ambient) { (void)inten;(void)ambient; }
```

Replace with (moved verbatim from `src/ps2/veronica/prog/ps2_NinjaCnk.c:161-267`,
marked "100% matching!" in the decomp comments — same "extract the matched
function, leave the VU0-asm rest of the file uncompiled" pattern as the
`bhAddSpeed` fix):

```c
/* ---- ninja chunk lighting: real setters (matched, moved from
 * ps2_NinjaCnk.c since that file is otherwise saturated with VU1 DMA/EE
 * inline asm we can't compile). Consumed by ninja_cnk.c's CPU-side
 * Lambertian shading — see cnk_emit_strip_tri. ------------------------- */
CNK_LIGHT NaCnkLightEs = { 1.401298464f, 0, 1.0f, 10.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0 };
VU1_COLOR NaCnkAmbientEs = { 1.0f, 1.0f, 1.0f, 1.0f };

void njCnkSetEasyLight(Float x, Float y, Float z)
{
    NaCnkLightEs.fCx = -x;
    NaCnkLightEs.fCy = -y;
    NaCnkLightEs.fCz = -z;
}
void njCnkSetEasyLightIntensity(Float inten, Float ambient)
{
    NaCnkLightEs.fI = inten;
    NaCnkAmbientEs.fB = ambient;
    NaCnkAmbientEs.fG = ambient;
    NaCnkAmbientEs.fR = ambient;
}
void njCnkSetEasyLightColor(Float r, Float g, Float b)
{
    NaCnkLightEs.fR = r;
    NaCnkLightEs.fG = g;
    NaCnkLightEs.fB = b;
}
```

The 7 remaining stubs (`njCnkSetEasyMultiLightSwitch/Vector`,
`njCnkSetSimpleMultiAmbient/LightColor/Matrices/Switch/Vector`) stay as-is —
out of scope, per this task's scope note.

- [ ] **Step 2: Wire Lambertian shading into `ninja_cnk.c`'s triangle emitter**

In `port/src/ninja_cnk.c`, add near the top (after the existing `extern
NJS_MATRIX* pNaMatMatrixStuckPtr;` at line 54):

```c
#include "types.h"   /* already included — CNK_LIGHT/VU1_COLOR live here */
extern CNK_LIGHT NaCnkLightEs;
extern VU1_COLOR NaCnkAmbientEs;
```

Modify `cnk_emit_strip_tri` (currently `port/src/ninja_cnk.c:223-249`) to
shade each vertex before emitting. Replace the three `tri[N].color = X->color
& base_color;` lines with a helper call:

```c
/* CPU-side approximation of the PS2's VU0 CALCPOINT per-vertex lighting
 * (real microcode: vsm/ps2_vu0.vsm, not portable). Rotates the object-space
 * normal into the same space njCalcPoint already puts positions in (via the
 * current model matrix), dots with the light direction NaCnkLightEs already
 * stores pre-negated, clamps to [0,1], and blends diffuse+ambient — not
 * exact hardware parity, just a first visible lighting pass. */
static uint32_t cnk_shade_vertex(const cnk_vert_t* v, uint32_t base_color)
{
    NJS_VECTOR n_obj = { v->nx, v->ny, v->nz };
    NJS_VECTOR n_world;
    njCalcVector(pNaMatMatrixStuckPtr, &n_obj, &n_world);
    njUnitVector(&n_world);

    NJS_VECTOR light = { NaCnkLightEs.fCx, NaCnkLightEs.fCy, NaCnkLightEs.fCz };
    float ndotl = njInnerProduct(&n_world, &light);
    if (ndotl < 0.0f) ndotl = 0.0f;

    float diff = ndotl * NaCnkLightEs.fI;
    float r = diff * NaCnkLightEs.fR + NaCnkAmbientEs.fR;
    float g = diff * NaCnkLightEs.fG + NaCnkAmbientEs.fG;
    float b = diff * NaCnkLightEs.fB + NaCnkAmbientEs.fB;
    if (r > 1.0f) r = 1.0f;
    if (g > 1.0f) g = 1.0f;
    if (b > 1.0f) b = 1.0f;

    uint32_t base = v->color & base_color;
    uint32_t a8 = (base >> 24) & 0xFF;
    uint32_t r8 = (uint32_t)(r * ((base >> 16) & 0xFF));
    uint32_t g8 = (uint32_t)(g * ((base >> 8) & 0xFF));
    uint32_t b8 = (uint32_t)(b * (base & 0xFF));
    return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
}
```

Then in `cnk_emit_strip_tri`, replace:
```c
    tri[0].color = a->color & base_color;
```
with:
```c
    tri[0].color = cnk_shade_vertex(a, base_color);
```
(and the same for `tri[1]`/`b` and `tri[2]`/`c`).

- [ ] **Step 3: Give the light a real initial direction/color**

Nothing currently calls `njCnkSetEasyLight*` with real values (that's
`light.c`'s job, and `light.c` is deliberately out of scope for this task —
see the scope note). Without a call, `NaCnkLightEs.fCx/fCy/fCz` stay at their
struct initializer `0,0,0`, so `ndotl` is always `0` and Step 2 would render
pure ambient (no visible change). Add one explicit, clearly-commented
placeholder call in `port/src/main_pc.c`'s room-load path (wherever
`bhSetRoom`/the room-change handler already runs — same place `recvx_gfx_set_view_matrix`
or similar per-room setup already happens), immediately after a room finishes
loading:

```c
/* TEMPORARY: light.c (the real per-room/player-relative light selector,
 * ~20 njCnk*/njCnkSetSimple* calls, not yet individually verified for
 * VU0-asm-free status) isn't compiled yet, so nothing else calls
 * njCnkSetEasyLight*. Hardcode a fixed overhead "sun" light so the new
 * CPU shading path (ninja_cnk.c: cnk_shade_vertex) has something to show.
 * Replace this call site once light.c lands. */
njCnkSetEasyLight(0.0f, -1.0f, 0.0f);
njCnkSetEasyLightIntensity(0.7f, 0.35f);
njCnkSetEasyLightColor(1.0f, 1.0f, 1.0f);
```

- [ ] **Step 4: Rebuild**

```bash
docker exec recvx_p3 bash -lc 'cd /workspace/port && cmake --build build 2>&1 | tail -40'
```

Fix any compile errors (exploratory, same bounded-loop framing as Task 1 Step
3 — expected to be minor, since every symbol used here was independently
confirmed to already exist and compile in this codebase).

- [ ] **Step 5: Visual verification**

Screenshot the same room used in the earlier P2 screenshot session (the
outdoor demo/attract-mode scene with the dock/gate/jeep) before and after
this change.

**Before:** flat per-vertex-color-only shading (no brightness variation tied
to surface orientation) — matches `ninja_cnk.c`'s existing "Phase 3a scope"
comment.
**After, expected:** surfaces facing toward `(0,1,0)` (up, since the light
vector is stored pre-negated) render brighter than surfaces facing away —
visible gradient across the dock/gate geometry instead of uniform flat tint.

```bash
docker exec recvx_p3 bash -lc 'DISPLAY=:99 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy /workspace/port/build/recvx_pc &'
sleep 5
docker exec recvx_p3 import -window root /tmp/p4_lighting_after.png
docker exec recvx_p3 cat /tmp/p4_lighting_after.png > /tmp/claude-1000/-home-skitzo-projects/*/scratchpad/p4shots/lighting_after.png
```

- [ ] **Step 6: Commit**

```bash
git add port/src/game_room_stubs.c port/src/ninja_cnk.c port/src/main_pc.c
git commit -m "$(cat <<'EOF'
feat: real njCnkSetEasyLight* setters + CPU-side Lambertian shading

Extracts the 3 matched njCnkSetEasyLight/Color/Intensity setters (and
their NaCnkLightEs/NaCnkAmbientEs globals) from ps2_NinjaCnk.c, same
"pull the matched function out of an otherwise VU0-asm-saturated file"
pattern as the bhAddSpeed fix. The real per-vertex lighting math is PS2
VU0 microcode (vsm/ps2_vu0.vsm) and can't be compiled for PC, so
ninja_cnk.c's cnk_emit_strip_tri gets a from-scratch CPU-side
Lambertian approximation instead: rotate the already-parsed per-vertex
object-space normal by the current model matrix (njCalcVector, already
real), dot with the light direction, blend diffuse+ambient. light.c
(the real per-room/player-relative light selector) isn't compiled yet,
so main_pc.c sets one hardcoded overhead light per room load as a
placeholder to prove the shading pipeline end-to-end.
EOF
)"
```

- [ ] **Follow-up (not part of this task, not started):** compiling
`light.c` for real dynamic lighting requires first verifying, one by one,
whether each of the ~20 additional `njCnk*`/`njCnkSetSimple*` functions it
calls (`njCnkSetEasyMultiLight*`, `njCnkSetSimpleLight*`,
`njCnkSetSimpleMultiLight*` — full list in this task's investigation, not
reproduced here) has a VU0-asm-free matched body in `ps2_NinjaCnk.c` the way
the 3 "Easy" setters did, or whether some require further CPU
reimplementation work like `ninja_3d.c`/`ninja_cnk.c` themselves needed. Scope
that verification as its own task before attempting it.

---

## Self-Review

**Spec coverage:** Task 1 covers all 27 `hitchk.c` functions (16 replace
existing stubs, 11 are net-new real code with no prior stub — e.g.
`bhCheckWallRefAngle`/`bhSetWallRefAngle`/`bhCheckInnerTriangle{,2,3}`/
`bhCheckBox{,2Box}`/`bhCheckInnerP4`/`bhSetDansaLimitAtari`/
`bhCheckDansaAtari`/`bhCheckDansa`/`bhCheckL2Water`/`bhCheckWallAttrB89` —
these were simply absent before, calling them was never reachable without
`hitchk.c`, so there's no stub to delete for them). Task 2 covers the 3
confirmed-matched lighting setters and a concrete, bounded shading
implementation; the explicitly out-of-scope remainder (full `light.c`,
`njCnkSetSimple*`/`njCnkSetEasyMulti*` families) is called out rather than
silently dropped.

**Placeholder scan:** the one intentionally-hardcoded value (Task 2 Step 3's
fixed overhead light) is a real, functioning value with a comment explaining
why and what replaces it later — not a TBD/stub. No other placeholders.

**Type consistency:** `CNK_LIGHT`/`VU1_COLOR` field names (`fCx/fCy/fCz`,
`fI`, `fR/fG/fB`) used in Task 2 Steps 1-2 match the verified struct
definition at `include/ps2/veronica/prog/types.h:1787-1815` exactly.

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-07-12-recvx-p4-collision-lighting-plan.md`.** Two execution options:

**1. Subagent-Driven (recommended)** - dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
