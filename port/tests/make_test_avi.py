#!/usr/bin/env python3
"""Writes a small uncompressed AVI used as a replacement movie by test_movie
(no game data involved): 64x48 pictures at 29.97 per second, green for the
first half second, then yellow.

    make_test_avi.py OUT.avi
"""
import struct
import sys

W, H, FRAMES = 64, 48, 30


def chunk(fourcc, data):
    return fourcc + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')


def lst(kind, data):
    return chunk(b'LIST', kind + data)


def main(path):
    size = W * H * 3
    frames = []
    for i in range(FRAMES):
        bgr = bytes((0, 255, 0)) if i < FRAMES // 2 else bytes((0, 255, 255))
        frames.append(bgr * (W * H))

    avih = struct.pack('<IIIIIIIIIIIIII', 33367, size * 30, 0, 0x10, FRAMES, 0, 1, size, W, H,
                       0, 0, 0, 0)
    strh = b'vids' + b'DIB ' + struct.pack('<IHHIIIIIIIIhhhh', 0, 0, 0, 0, 1001, 30000, 0,
                                             FRAMES, size, 0xFFFFFFFF, 0, 0, 0, W, H)
    strf = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 24, 0, size, 0, 0, 0, 0)
    hdrl = lst(b'hdrl', chunk(b'avih', avih) + lst(b'strl', chunk(b'strh', strh) +
                                                       chunk(b'strf', strf)))
    movi_data = b''
    index = b''
    for f in frames:
        index += b'00db' + struct.pack('<III', 0x10, 4 + len(movi_data), len(f))
        movi_data += chunk(b'00db', f)
    movi = lst(b'movi', movi_data)
    riff = b'AVI ' + hdrl + movi + chunk(b'idx1', index)
    with open(path, 'wb') as out:
        out.write(b'RIFF' + struct.pack('<I', len(riff)) + riff)


if __name__ == '__main__':
    main(sys.argv[1])
