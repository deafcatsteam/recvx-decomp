# RECVX PC Port

PC port of *Resident Evil: Code Veronica X* built on top of the [recvx-decomp](https://github.com/fmil95/recvx-decomp) project.

## Status

Phase 0 — skeleton. Compiles port stubs only. Game source not yet wired in.

## Requirements

- Visual Studio 2022 with C++ "Desktop development" workload (MSVC x86 32-bit toolset)
- CMake 3.20+
- A copy of *Resident Evil: Code Veronica X* (USA, SLUS-20184) ISO

## Build

From a "x86 Native Tools Command Prompt for VS 2022":

```
cd port
cmake -B build -A Win32
cmake --build build --config Debug
```

Place the ISO at `build/Debug/recvx.iso` (or pass `--iso path\to\file.iso`) before launching.

## Layout

- `port/src/main_pc.c` — entry point, SDL2 window, calls `njUserInit` / `njUserMain` loop
- `port/src/backend/` — renderer/audio/input backends (GL3.3 first; Vulkan slot reserved)
- `port/src/stubs/` — PS2 SDK / Sega Ninja / KATANA / CRI shims that satisfy the decomp's link references
- `port/src/iso/` — ISO9660 reader + `sceCd*` shim
- `port/src/fmv/` — FFmpeg-backed Sofdec replacement
- `port/include/` — port-only headers
- `port/cmake/` — CMake helpers

The port deliberately keeps the renderer behind an abstract backend interface so a Vulkan backend can be added later without touching game code.
