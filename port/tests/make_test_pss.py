#!/usr/bin/env python3
"""Writes port/tests/data/test.pss, a tiny movie in the PS2 .PSS layout used
by test_movie (no game data involved).

The picture is MPEG-2 encoded by ffmpeg (needed only to regenerate the
file): a quarter second of red, then a quarter second of blue, 320x240.
The sound is a 440 Hz sine, 8 kHz mono 16-bit PCM, with the "SShd"/"SSbd"
header of PSS files, in private stream 1 packets.

    python3 port/tests/make_test_pss.py port/tests/data/test.pss
"""
import math
import struct
import subprocess
import sys
import tempfile


def pack_header():
    # MPEG-2 pack header, SCR 0, mux rate 10080 bytes/s.
    return bytes([0, 0, 1, 0xBA, 0x44, 0, 4, 0, 4, 1, 0x01, 0x89, 0xC3, 0xF8])


def pes(stream_id, payload):
    head = bytes([0x80, 0x00, 0x00])  # MPEG-2 PES header, no time stamp
    body = head + payload
    return bytes([0, 0, 1, stream_id]) + struct.pack('>H', len(body)) + body


def main(path):
    with tempfile.TemporaryDirectory() as tmp:
        m2v = tmp + '/v.m2v'
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y',
                        '-f', 'lavfi', '-i', 'color=c=red:s=320x240:r=30000/1001:d=0.25',
                        '-f', 'lavfi', '-i', 'color=c=blue:s=320x240:r=30000/1001:d=0.25',
                        '-filter_complex', '[0][1]concat=n=2:v=1[v]', '-map', '[v]',
                        '-c:v', 'mpeg2video', '-b:v', '400k', '-f', 'mpeg2video', m2v],
                       check=True)
        video = open(m2v, 'rb').read()

    rate, interleave = 8000, 512
    samples = [int(12000 * math.sin(2 * math.pi * 440 * i / rate)) for i in range(rate // 2)]
    pcm = b''.join(struct.pack('<h', s) for s in samples)
    pcm += b'\0' * (-len(pcm) % interleave)
    header = (b'SShd' + struct.pack('<IIIIIii', 0x18, 1, rate, 1, interleave, -1, -1) +
              b'SSbd' + struct.pack('<I', len(pcm)))
    audio = header + pcm

    out = bytearray()
    vpos = apos = 0
    while vpos < len(video) or apos < len(audio):
        out += pack_header()
        if vpos < len(video):
            chunk = video[vpos:vpos + 2000]
            vpos += len(chunk)
            out += pes(0xE0, chunk)
        if apos < len(audio):
            chunk = audio[apos:apos + 1000]
            apos += len(chunk)
            # Sub-stream header of the PS2 sound packets (skipped by the player).
            out += pes(0xBD, bytes([0, 0, 0, 0]) + chunk)
    out += bytes([0, 0, 1, 0xB9])
    with open(path, 'wb') as f:
        f.write(out)


if __name__ == '__main__':
    main(sys.argv[1])
