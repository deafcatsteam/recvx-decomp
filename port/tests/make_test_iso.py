#!/usr/bin/env python3
"""Writes a tiny ISO 9660 image used by test_disc (no game data involved).

Layout: /RDX_LNK.AFS (an AFS archive of 3 files), /SOUND.AFS (ADX streams
from make_test_adx.py), /CF_ROM.TXT and /MOVIE/OPEN.SFD, plus
/MOVIE/MV_000.PSS when a movie file is given.

    make_test_iso.py OUT.iso [MOVIE.pss]
"""
import struct
import sys

import make_test_adx

SECTOR = 2048


def both16(v):
    return struct.pack('<H', v) + struct.pack('>H', v)


def both32(v):
    return struct.pack('<I', v) + struct.pack('>I', v)


def dir_record(name, lsn, size, is_dir):
    rec = bytearray(33 + len(name) + (len(name) + 1) % 2)
    rec[0] = len(rec)
    rec[2:10] = both32(lsn)
    rec[10:18] = both32(size)
    rec[25] = 2 if is_dir else 0
    rec[28:32] = both16(1)
    rec[32] = len(name)
    rec[33:33 + len(name)] = name
    return bytes(rec)


def afs(files):
    table, data, off = b'', b'', SECTOR
    for f in files:
        table += struct.pack('<II', off + len(data), len(f))
        data += f + b'\0' * (-len(f) % SECTOR)
    # The fourth magic byte is not always 0 on the real disc.
    head = b'AFS ' + struct.pack('<I', len(files)) + table
    return head + b'\0' * (SECTOR - len(head)) + data


def main(path, pss_path=None):
    pss = open(pss_path, 'rb').read() if pss_path else b''
    afs_data = afs([b'hello afs file 0', b'B' * 3000, b'third'])
    sound_afs = afs(make_test_adx.test_streams())
    txt = b'plain file contents\n'
    sfd = b'x' * 5000

    ROOT, MOVIE, DATA = 18, 19, 20
    files = []
    lsn = DATA
    for name, content in [(b'RDX_LNK.AFS;1', afs_data), (b'CF_ROM.TXT;1', txt),
                          (b'SOUND.AFS;1', sound_afs)]:
        files.append((name, lsn, content))
        lsn += (len(content) + SECTOR - 1) // SECTOR
    sfd_lsn = lsn
    lsn += (len(sfd) + SECTOR - 1) // SECTOR
    pss_lsn = lsn
    lsn += (len(pss) + SECTOR - 1) // SECTOR

    root = dir_record(b'\0', ROOT, SECTOR, True) + dir_record(b'\1', ROOT, SECTOR, True)
    root += dir_record(b'CF_ROM.TXT;1', files[1][1], len(txt), False)
    root += dir_record(b'MOVIE', MOVIE, SECTOR, True)
    root += dir_record(b'RDX_LNK.AFS;1', files[0][1], len(afs_data), False)
    root += dir_record(b'SOUND.AFS;1', files[2][1], len(sound_afs), False)
    movie = dir_record(b'\0', MOVIE, SECTOR, True) + dir_record(b'\1', ROOT, SECTOR, True)
    if pss:
        movie += dir_record(b'MV_000.PSS;1', pss_lsn, len(pss), False)
    movie += dir_record(b'OPEN.SFD;1', sfd_lsn, len(sfd), False)

    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[6] = 1
    pvd[80:88] = both32(lsn)
    pvd[128:132] = both16(SECTOR)
    pvd[156:156 + 34] = dir_record(b'\0', ROOT, SECTOR, True)
    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b'CD001'

    image = bytearray(lsn * SECTOR)
    image[16 * SECTOR:17 * SECTOR] = pvd
    image[17 * SECTOR:18 * SECTOR] = term
    image[ROOT * SECTOR:ROOT * SECTOR + len(root)] = root
    image[MOVIE * SECTOR:MOVIE * SECTOR + len(movie)] = movie
    for _, at, content in files + [(None, sfd_lsn, sfd), (None, pss_lsn, pss)]:
        image[at * SECTOR:at * SECTOR + len(content)] = content
    with open(path, 'wb') as f:
        f.write(image)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
