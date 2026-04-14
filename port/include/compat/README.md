# compat/

MSVC-safe shadow headers for PS2 SDK / MWCC / CRI headers that can't compile on a PC toolchain. Placed **first** on the game target's include path so `#include <foo.h>` from game source finds our stub instead of the real PS2 SDK copy.

Each stub provides only the types and prototypes that game source actually references — not a full SDK reimplementation. Grow them incrementally as compile errors surface during phase 2+.

Things that stay **real** (not shadowed here, used from `include/recvx-decomp-katana/KATANA/Include/`):
- `ninja.h` and its transitive `ninja*.h`, `sg_*.h` where MSVC-clean
- `mathf.h`

Things shadowed here because their real versions use MWCC pragmas / `__int128` / MIPS asm / `__option`:
- `PREFIX_PS2*.h`, `PRAGMA_PS2.h`
- `eeregs.h`, `eekernel.h`, `eetypes.h`, `eestruct.h`
- `libvu0.h`, `libcdvd.h`, `libipu.h`, `libmpeg.h`, `libgraph.h`,
  `libdma.h`, `libpad.h`, `libmc.h`, `libsdr.h`, `libssyn.h`
- `csl.h`, `sif.h`, `sifrpc.h`, `sdrcmd.h`, `sdmacro.h`
- `cri_adxf.h`, `cri_adxt.h`
