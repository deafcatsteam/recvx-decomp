#!/usr/bin/env python3
"""Generate case-correct symlinks for headers included with the wrong case.

The Katana/CRI headers come from Windows SDKs, where file names are case
insensitive. This scans every #include in the given trees and, for any name
that only exists with a different case, creates a symlink with the requested
spelling inside OUT_DIR (which is then added to the include path).
"""
import os
import re
import sys
from pathlib import Path

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def main() -> int:
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} OUT_DIR SEARCH_DIR [SEARCH_DIR...]")
        return 1
    out = Path(sys.argv[1])
    roots = [Path(p) for p in sys.argv[2:]]

    # lowercase relative path -> real file, for every header under each root
    index: dict[str, Path] = {}
    for root in roots:
        for f in root.rglob("*"):
            if f.is_file():
                index.setdefault(f.relative_to(root).as_posix().lower(), f.resolve())

    wanted: set[str] = set()
    for root in roots:
        for f in root.rglob("*"):
            if f.is_file() and f.suffix.lower() in (".h", ".c"):
                wanted.update(INCLUDE_RE.findall(f.read_text(errors="replace")))

    made = 0
    for name in sorted(wanted):
        if name.startswith("..") or any((r / name).exists() for r in roots):
            continue
        real = index.get(name.lower())
        if real is None:
            continue
        link = out / name
        link.parent.mkdir(parents=True, exist_ok=True)
        if link.is_symlink() or link.exists():
            link.unlink()
        os.symlink(real, link)
        made += 1
    print(f"gen_case_links: {made} links in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
