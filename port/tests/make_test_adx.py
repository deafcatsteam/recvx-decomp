#!/usr/bin/env python3
"""A small ADX encoder for the tests: makes ADX streams of sine waves (no
game data involved). Used by make_test_iso.py for /SOUND.AFS.

ADX (CRI): a big-endian header, then for each group of 32 samples one frame
of 18 bytes per channel: a 16-bit scale (stored minus one) and 32 signed
4-bit values, each sample predicted from the two before it.
"""
import math
import struct


def coefficients(cutoff, rate):
    z = math.cos(2.0 * math.pi * cutoff / rate)
    a = math.sqrt(2.0) - z
    b = math.sqrt(2.0) - 1.0
    c = (a - math.sqrt((a + b) * (a - b))) / b
    return int(math.floor(c * 8192.0)), int(math.floor(c * c * -4096.0))


def clamp16(v):
    return max(-32768, min(32767, v))


def encode_frame(x, hist, c1, c2):
    """Encodes 32 samples; hist is [h1, h2] and is updated."""
    h1, h2 = hist
    # Scale from the residual against an ideal prediction.
    p1, p2, worst = h1, h2, 0
    for s in x:
        pred = (c1 * p1 >> 12) + (c2 * p2 >> 12)
        worst = max(worst, abs(s - pred))
        p2, p1 = p1, s
    scale = max(1, min(0x1000, -(-worst // 7)))
    out = bytearray(struct.pack('>H', scale - 1))
    nibs = []
    for s in x:
        pred = (c1 * h1 >> 12) + (c2 * h2 >> 12)
        n = max(-8, min(7, int(round((s - pred) / scale))))
        nibs.append(n & 15)
        d = clamp16(n * scale + pred)
        h2, h1 = h1, d
    for i in range(0, 32, 2):
        out.append(nibs[i] << 4 | nibs[i + 1])
    hist[0], hist[1] = h1, h2
    return bytes(out)


def adx(channels, rate, loop=None, cutoff=500):
    """channels: one list of samples per channel, all the same length.
    loop: (start, end) in samples, start a multiple of 32."""
    n = len(channels[0])
    c1, c2 = coefficients(cutoff, rate)
    hsize = 0x34
    head = bytearray(hsize)
    struct.pack_into('>HHBBBBIIHBB', head, 0, 0x8000, hsize - 4, 3, 18, 4,
                     len(channels), rate, n, cutoff, 3, 0)
    if loop:
        start, end = loop
        group = 18 * len(channels)
        struct.pack_into('>IIIII', head, 0x18, 1, start, hsize + start // 32 * group,
                         end, hsize + (end + 31) // 32 * group)
    head[hsize - 6:hsize] = b'(c)CRI'
    hists = [[0, 0] for _ in channels]
    body = bytearray()
    for f in range((n + 31) // 32):
        for ch, data in enumerate(channels):
            x = data[f * 32:f * 32 + 32]
            x += [0] * (32 - len(x))
            body += encode_frame(x, hists[ch], c1, c2)
    return bytes(head) + bytes(body)


def sine(freq, rate, n, amp=10000):
    return [int(round(amp * math.sin(2 * math.pi * freq * i / rate))) for i in range(n)]


def test_streams():
    """File 0: stereo 44.1 kHz, 440 Hz left and 880 Hz right, 0.5 s, looping
    over its second half. File 1: mono 22.05 kHz, 1000 Hz, 0.3 s."""
    n = 22050
    looped = adx([sine(440, 44100, n), sine(880, 44100, n)], 44100, loop=(11040, n))
    once = adx([sine(1000, 22050, 6615)], 22050)
    return [looped, once]
