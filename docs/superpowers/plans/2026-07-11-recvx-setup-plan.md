# RE:CVX decomp + PC port setup — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fork, clone, sync, and get both `recvx-decomp` and `recvx-decomp-port` building in Docker on this Linux machine, using the user's existing ISO.

**Architecture:** Two independent GitHub repos forked to `deafcatsteam`, cloned side by side under `~/projects/`. `recvx-decomp` builds unmodified via its existing devcontainer. `recvx-decomp-port`'s `pc-port` branch is first fast-forwarded with upstream decomp progress, then gets a new Linux CMake preset (Ninja + vcpkg `x64-linux`) added so its "Phase 0" (non-game, FMV-only) target builds in the same kind of container, deferring the Windows-only low-4GiB allocator port as future work.

**Tech Stack:** git, gh CLI, Docker, Python (splat/wibo toolchain), CMake + Ninja + vcpkg, C/C++.

## Global Constraints

- User's ISO is at `/home/skitzo/Documents/Resident Evil - Code - Veronica X (USA).iso` — never modify or move this file, only read from it.
- Both repos forked to GitHub account `deafcatsteam` (has read-only access to upstream `AshfordFamily` org repos).
- Clone destination: `~/projects/recvx-decomp` and `~/projects/recvx-decomp-port`.
- `recvx-decomp-port`'s default/working branch is `pc-port`, not `master`.
- Do not attempt to enable `RECVX_BUILD_GAME=ON` on Linux in this plan — `tex_pool_alloc.c`'s low-4GiB allocator and `game_texture_stubs.c`'s `windows.h` usage are Windows-only and out of scope; Phase 0 build only (`RECVX_BUILD_GAME=OFF`).
- Never force-push; never rewrite the `pc-port` branch's history — only fast-forward merges.

---

### Task 1: Fork and clone both repos, add upstream remotes, commit design docs

**Files:**
- Create: `~/projects/recvx-decomp/` (clone)
- Create: `~/projects/recvx-decomp-port/` (clone)
- Create: `~/projects/recvx-decomp-port/docs/superpowers/specs/2026-07-11-recvx-setup-design.md`
- Create: `~/projects/recvx-decomp-port/docs/superpowers/plans/2026-07-11-recvx-setup-plan.md`

**Interfaces:**
- Produces: two local git repos with `origin` = `deafcatsteam` fork, `upstream` = `AshfordFamily` original, ready for Task 2 onward.

- [ ] **Step 1: Fork `recvx-decomp`**

Run: `gh repo fork AshfordFamily/recvx-decomp --clone=false`
Expected: output ends with `✓ Created fork deafcatsteam/recvx-decomp`

- [ ] **Step 2: Fork `recvx-decomp-port`**

Run: `gh repo fork AshfordFamily/recvx-decomp-port --clone=false`
Expected: output ends with `✓ Created fork deafcatsteam/recvx-decomp-port`

- [ ] **Step 3: Create projects directory and clone both forks recursively**

```bash
mkdir -p ~/projects
gh repo clone deafcatsteam/recvx-decomp ~/projects/recvx-decomp -- --recursive
gh repo clone deafcatsteam/recvx-decomp-port ~/projects/recvx-decomp-port -- --recursive
```
Expected: both commands exit 0; `~/projects/recvx-decomp/compile.py` and `~/projects/recvx-decomp-port/port/CMakeLists.txt` both exist.

- [ ] **Step 4: Verify the port repo checked out `pc-port` by default**

Run: `cd ~/projects/recvx-decomp-port && git branch --show-current`
Expected: `pc-port`

- [ ] **Step 5: Add `upstream` remotes and fetch**

```bash
cd ~/projects/recvx-decomp
git remote add upstream https://github.com/AshfordFamily/recvx-decomp.git
git fetch upstream

cd ~/projects/recvx-decomp-port
git remote add upstream https://github.com/AshfordFamily/recvx-decomp-port.git
git fetch upstream
```
Expected: both `git fetch` calls exit 0 with no errors.

- [ ] **Step 6: Copy the design spec and this plan into the port repo and commit**

```bash
mkdir -p ~/projects/recvx-decomp-port/docs/superpowers/specs
mkdir -p ~/projects/recvx-decomp-port/docs/superpowers/plans
cp /tmp/claude-1000/-home-skitzo/5d07e766-127f-4d41-a340-6770ba1b9324/scratchpad/2026-07-11-recvx-setup-design.md \
   ~/projects/recvx-decomp-port/docs/superpowers/specs/
cp /tmp/claude-1000/-home-skitzo/5d07e766-127f-4d41-a340-6770ba1b9324/scratchpad/2026-07-11-recvx-setup-plan.md \
   ~/projects/recvx-decomp-port/docs/superpowers/plans/
cd ~/projects/recvx-decomp-port
git add docs/superpowers/specs/2026-07-11-recvx-setup-design.md docs/superpowers/plans/2026-07-11-recvx-setup-plan.md
git commit -m "docs: add initial setup design and implementation plan"
```
Expected: commit succeeds, `git log -1 --oneline` shows the new commit.

---

### Task 2: Sync `pc-port` branch with `recvx-decomp` upstream master

**Files:**
- Modify: `~/projects/recvx-decomp-port/` (merge commit touching whatever files diverged — expected mainly `report.json`, `compile_config.json`, and any `src/`/`include/` files both branches touched)

**Interfaces:**
- Consumes: `~/projects/recvx-decomp-port` repo from Task 1, with `upstream` remote = `AshfordFamily/recvx-decomp-port` (NOT the decomp repo — a second remote is added here for the decomp repo specifically).
- Produces: `pc-port` branch containing all of `recvx-decomp` master's decompilation progress, pushed to `origin` (the user's fork), ready for Task 4's build.

- [ ] **Step 1: Add a remote for the decomp repo (separate from `recvx-decomp-port`'s own upstream)**

```bash
cd ~/projects/recvx-decomp-port
git remote add decomp-upstream https://github.com/AshfordFamily/recvx-decomp.git
git fetch decomp-upstream
```
Expected: exit 0, no errors.

- [ ] **Step 2: Merge `decomp-upstream/master` into `pc-port`**

```bash
cd ~/projects/recvx-decomp-port
git checkout pc-port
git merge decomp-upstream/master --no-edit -m "Merge upstream recvx-decomp master into pc-port"
```
Expected: either a clean merge commit, or a conflict report listing files under `<<<<<<<` markers.

- [ ] **Step 3: If conflicts occurred, resolve them**

Check which files conflicted:
```bash
git status --porcelain | grep '^UU\|^AA\|^DD'
```

For each conflicted file, apply this rule:
- `report.json`, `compile_config.json`, `elf/report.txt` (auto-generated build-progress artifacts): these should reflect the newest decompilation state, so take `decomp-upstream`'s version entirely:
  ```bash
  git checkout --theirs report.json compile_config.json 2>/dev/null
  git add report.json compile_config.json 2>/dev/null
  ```
  (only run `git add` for files that actually exist/conflicted — skip any not present)
- Any `src/ps2/veronica/prog/*.c` or `include/**` file that conflicts: open it, look at the `<<<<<<< HEAD` / `=======` / `>>>>>>> decomp-upstream/master` markers. `pc-port`'s side (`HEAD`) will only ever contain PC-port-specific additions (e.g. `#ifdef RECVX_PC_PORT` blocks) inside a function that `decomp-upstream` may have also changed elsewhere in the same file. Keep both sets of changes: the newly-decompiled logic from `decomp-upstream`, with the `RECVX_PC_PORT` guards from `HEAD` preserved around the specific lines that introduced them. Remove the conflict markers after combining.
- Any file under `port/` (the port-specific source, e.g. `port/src/`, `port/CMakeLists.txt`): these only exist on `pc-port`, so a conflict here would be unexpected — if it happens, keep `HEAD`'s version (`git checkout --ours <file>`) and inspect afterward.

After resolving each file:
```bash
git add <resolved-file>
```

- [ ] **Step 4: Complete the merge**

```bash
git status
```
Expected: `All conflicts fixed but you are still merging.` or a clean working tree if there were no conflicts.

If merging (conflicts existed):
```bash
git commit --no-edit
```
Expected: merge commit created, `git status` now shows a clean working tree.

- [ ] **Step 5: Verify no unresolved conflict markers remain anywhere**

```bash
grep -rl '^<<<<<<<\|^=======$\|^>>>>>>>' --include='*.c' --include='*.h' --include='*.json' . || echo "clean"
```
Expected: `clean`

- [ ] **Step 6: Push the synced branch to the fork**

```bash
git push origin pc-port
```
Expected: exit 0, push succeeds (fast-forward or merge commit push, never forced).

---

### Task 3: Build `recvx-decomp` in Docker against the user's ISO

**Files:**
- Modify: `~/projects/recvx-decomp/config/SLUS_201.84` (new file, extracted from ISO — gitignored, not committed)
- No source files modified in this task.

**Interfaces:**
- Consumes: `~/projects/recvx-decomp` from Task 1 (unmodified, `master` branch).
- Produces: `~/projects/recvx-decomp/elf/main.elf` — a working PS2 ELF build, confirming the existing devcontainer/toolchain works standalone in Docker.

- [ ] **Step 1: Build the devcontainer image**

```bash
cd ~/projects/recvx-decomp
docker build -t recvx-decomp-dev -f .devcontainer/Dockerfile .
```
Expected: exit 0, final line `naming to docker.io/library/recvx-decomp-dev` (or similar "writing image" success line).

- [ ] **Step 2: Extract `SLUS_201.84` from the user's ISO using the repo's own `mkiso.py`**

This requires the Python/splat toolchain, so run it inside the container:
```bash
cd ~/projects/recvx-decomp
docker run --rm \
  -v "$(pwd)":/workspace \
  -v "/home/skitzo/Documents":/iso-src:ro \
  -w /workspace \
  recvx-decomp-dev \
  bash -c "python3 -m venv .venv && .venv/bin/pip install -r config/requirements.txt && .venv/bin/python mkiso.py -m extract --iso '/iso-src/Resident Evil - Code - Veronica X (USA).iso'"
```
Expected: exit 0, output ends with `dumping finished`, and `iso/SLUS_201.84` now exists on the host (`ls iso/SLUS_201.84`).

- [ ] **Step 3: Copy the extracted executable into `config/`**

```bash
cd ~/projects/recvx-decomp
cp iso/SLUS_201.84 config/SLUS_201.84
```
Expected: `config/SLUS_201.84` exists and is a non-empty binary file (`ls -la config/SLUS_201.84`).

- [ ] **Step 4: Run the objdiff setup and compile inside the container**

```bash
cd ~/projects/recvx-decomp
docker run --rm \
  -v "$(pwd)":/workspace \
  -w /workspace \
  recvx-decomp-dev \
  bash -c ".venv/bin/python compile.py --setup && .venv/bin/python compile.py"
```
Expected: exit 0. `compile.py --setup` generates `objdiff.json` and/or `config/asm/`. `compile.py` finishes without a Python traceback.

- [ ] **Step 5: Verify the build artifact exists**

```bash
ls -la ~/projects/recvx-decomp/elf/main.elf
```
Expected: file exists, size > 0.

- [ ] **Step 6: No commit needed**

This task only produces build artifacts (`elf/main.elf`, `iso/`, `config/SLUS_201.84`) which are gitignored — confirm with:
```bash
cd ~/projects/recvx-decomp
git status --porcelain
```
Expected: empty output (nothing to commit), or only untracked-but-ignored build artifacts.

---

### Task 4: Add a Linux build preset to `recvx-decomp-port` and build Phase 0 against the ISO

**Files:**
- Modify: `~/projects/recvx-decomp-port/port/CMakePresets.json`
- Modify: `~/projects/recvx-decomp-port/.devcontainer/Dockerfile`
- No changes to `port/CMakeLists.txt` — it already supports non-MSVC builds via its `if(MSVC)/else()` branches.

**Interfaces:**
- Consumes: `~/projects/recvx-decomp-port` from Task 2 (synced `pc-port` branch), and `iso/SLUS_201.84`-style extraction technique from Task 3 (reused here for the port's own ISO needs, using the same ISO file).
- Produces: `~/projects/recvx-decomp-port/port/build/recvx_pc` — a working Linux Phase 0 executable (FMV playback demo, `RECVX_BUILD_GAME=OFF`).

- [ ] **Step 1: Add Linux packages to the devcontainer Dockerfile**

Read current content first, then add after the existing `wibo` install block in `~/projects/recvx-decomp-port/.devcontainer/Dockerfile`:

```dockerfile
# Linux build tooling for the PC port (Phase 0: Ninja + vcpkg x64-linux)
RUN apt-get update \
    && apt-get install -y \
        ninja-build \
        curl zip unzip tar pkg-config \
        libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
        libgl1-mesa-dev libasound2-dev libpulse-dev

# vcpkg, pinned to a fixed path so CMakePresets.json can reference it via VCPKG_ROOT
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg
```

- [ ] **Step 2: Add a Linux configure/build preset to `port/CMakePresets.json`**

Current file has one `configurePresets` entry (`x64-vcpkg`, MSVC) and one `buildPresets` entry (`x64-debug`). Add a second entry to each array:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "x64-vcpkg",
      "displayName": "MSVC x64 + vcpkg",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
        "VCPKG_TARGET_TRIPLET": "x64-windows"
      }
    },
    {
      "name": "x64-linux-vcpkg",
      "displayName": "Ninja x64 Linux + vcpkg",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
        "VCPKG_TARGET_TRIPLET": "x64-linux",
        "RECVX_BUILD_GAME": "OFF"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "x64-debug",
      "configurePreset": "x64-vcpkg",
      "configuration": "Debug"
    },
    {
      "name": "x64-linux-debug",
      "configurePreset": "x64-linux-vcpkg",
      "configuration": "Debug"
    }
  ]
}
```

- [ ] **Step 3: Rebuild the devcontainer image with the new Dockerfile**

```bash
cd ~/projects/recvx-decomp-port
docker build -t recvx-port-dev -f .devcontainer/Dockerfile .
```
Expected: exit 0, image builds (this step compiles vcpkg's bootstrap and will take a few minutes).

- [ ] **Step 4: Configure and build the Phase 0 target inside the container**

```bash
cd ~/projects/recvx-decomp-port
docker run --rm \
  -v "$(pwd)":/workspace \
  -w /workspace/port \
  recvx-port-dev \
  bash -c "cmake --preset x64-linux-vcpkg && cmake --build --preset x64-linux-debug"
```
Expected: exit 0. vcpkg will build SDL2, SDL2-image, SDL2-ttf, and FFmpeg from source on first run (can take 10-20+ minutes) — this is expected, not an error. CMake configure output should show `SDL2 not found` / `OpenGL not found` messages replaced by successful `find_package` results once vcpkg finishes installing.

- [ ] **Step 5: Verify the executable was produced**

```bash
ls -la ~/projects/recvx-decomp-port/port/build/recvx_pc
```
Expected: file exists, is executable (`x` permission bit set).

- [ ] **Step 6: Run the Phase 0 FMV demo against the user's ISO**

The build container has no display; use a virtual framebuffer:
```bash
cd ~/projects/recvx-decomp-port
docker run --rm \
  -v "$(pwd)":/workspace \
  -v "/home/skitzo/Documents":/iso-src:ro \
  -w /workspace/port \
  recvx-port-dev \
  bash -c "apt-get update && apt-get install -y xvfb && xvfb-run -a ./build/recvx_pc --iso '/iso-src/Resident Evil - Code - Veronica X (USA).iso'"
```
Expected: process starts, logs FMV/ISO-related output (e.g. reading `MV_000.PSS`) without crashing, and exits cleanly (or runs until manually stopped, since it's a playback loop — a clean startup with no immediate segfault/traceback counts as success for this task).

- [ ] **Step 7: Commit the new preset and Dockerfile changes**

```bash
cd ~/projects/recvx-decomp-port
git add port/CMakePresets.json .devcontainer/Dockerfile
git commit -m "build: add Linux (Ninja + vcpkg x64-linux) preset for Phase 0 port build"
git push origin pc-port
```
Expected: commit and push both succeed.

---

## Self-Review Notes

- **Spec coverage:** Task 1 = fork/clone; Task 2 = sync; Task 3 = decomp build; Task 4 = port Phase 0 build. All four spec sections covered. Low-4GiB allocator / `RECVX_BUILD_GAME=ON` explicitly called out as deferred, matching the spec's "out of scope" section.
- **No placeholders:** every step has literal commands and expected output; conflict resolution in Task 2 gives concrete per-file-type rules rather than "resolve conflicts as needed."
- **Consistency:** `recvx-decomp-dev` / `recvx-port-dev` image names, `x64-linux-vcpkg` / `x64-linux-debug` preset names, and file paths are used consistently between the steps that create them and the steps that reference them.
