# Resident Evil: Code Veronica X - PC Port

PC port of *Resident Evil: Code Veronica X* (PS2) using the [recvx-decomp](https://github.com/fmil95/recvx-decomp) decompilation.

<img width="643" height="510" alt="image" src="https://github.com/user-attachments/assets/540c7629-e15c-4684-bb2c-9a33e7cd47ba" />

## Quick Start

### Requirements

- Visual Studio 2022 (MSVC x64)
- CMake 3.20+
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` environment variable
- Copy of Resident Evil: Code Veronica X USA (SLUS-20184) ISO

### Build

```bash
cd port
cmake --preset x64-vcpkg
cmake --build build --config Debug
```

Run with:
```bash
./build/Debug/recvx_pc.exe --iso path/to/recvx.iso --game
```

## Controls

| Input | Action |
|-------|--------|
| **Z** | Action / Confirm |
| **X** | Cancel |
| **Arrow Keys** | Move / Select |
| **S** | Menu |
| **A** | Aim (Combat) |
| **Q / W** | L1 / R1 |
| **E / R** | L2 / R2 |
| **Enter** | Start |
| **Backspace** | Select |

## Status

- ✅ Title screen + options
- ⚠️ FMV playback (in progress)
- ⚠️ Game logic (partial decompilation)

## Credits

- **Decompilation**: [fmil95/recvx-decomp](https://github.com/fmil95/recvx-decomp)
- **Sofdec/CRI reverse engineering**: recvx-decomp contributors
- **PC port**: This branch
- **Frameworks**: SDL2, OpenGL, FFmpeg
- **Original game**: Capcom

## References

- [recvx-decomp](https://github.com/fmil95/recvx-decomp) — PS2 decompilation & KATANA/CRI docs
- [PCSX2](https://pcsx2.net/) — PS2 emulation reference
- FFmpeg libavformat/libavcodec — video/audio decode

## Disclaimer

This repository is made available for preservational and educational purposes. No affiliation with Capcom Co., Ltd. or the Resident Evil franchise is claimed.
