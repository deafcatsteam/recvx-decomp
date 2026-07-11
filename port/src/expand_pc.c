/*
 * expand_pc.c — C re-implementation of Expand() from
 * src/ps2/veronica/prog/expand.c, which is 100% MIPS EE inline asm and
 * can't be compiled on x86. Called on every room load (system.c room
 * state machine: `sys->rdtsz = Expand(sys->rdtp, sys->memp)`).
 *
 * Format decoded from the asm (LZSS variant, control bits LSB-first from
 * a rolling control byte; the first control byte carries 8 usable bits,
 * refills carry 8):
 *   bit 1            -> literal: copy 1 byte from src
 *   bits 0,0         -> short match: 2 more bits = length field L (0..3),
 *                       then 1 byte B: offset = B - 256 (back-ref within
 *                       last 256 bytes), copy L+2 bytes
 *   bits 0,1         -> long match: 16-bit LE word W (word of 0 = end of
 *                       stream). offset = (W >> 3) - 8192, length field
 *                       L = W & 7. L != 0: copy L+2 bytes. L == 0: one
 *                       extra byte N follows, copy N+1 bytes.
 * Matches may self-overlap (byte-at-a-time forward copy, like the asm).
 * Returns the number of bytes written to dst.
 *
 * Init_Expand()/ExpandCtrlBuf only manage an abort flag used by the PS2
 * async path; the synchronous decode ignores them (Init_Expand is a no-op
 * stub in stub_sg.c).
 */

int Expand(char* src, unsigned char* dst)
{
    const unsigned char* s = (const unsigned char*)src;
    unsigned char* d = dst;
    unsigned char* const out_start = dst;

    unsigned int ctrl = *s++;
    int bits = 9;

/* Pull the next control bit into `bit`, refilling the control byte when
 * exhausted. Mirrors the asm's t7/t6 dance exactly (t7 pre-decremented,
 * starts at 9 so the first byte yields 8 usable bits; on refill the
 * stale extraction is discarded and redone from the fresh byte). */
#define NEXT_BIT(bit)                          \
    do {                                       \
        --bits;                                \
        (bit) = ctrl & 1u;                     \
        ctrl >>= 1;                            \
        if (bits <= 0) {                       \
            ctrl = *s++;                       \
            bits = 8;                          \
            (bit) = ctrl & 1u;                 \
            ctrl >>= 1;                        \
        }                                      \
    } while (0)

    for (;;) {
        unsigned int bit;
        NEXT_BIT(bit);
        if (bit) {
            *d++ = *s++;
            continue;
        }

        NEXT_BIT(bit);
        long offset;
        long len;
        if (!bit) {
            /* Short match: 2-bit length field, 8-bit offset. */
            unsigned int b;
            NEXT_BIT(b);
            len = b;
            NEXT_BIT(b);
            len = (len << 1) | b;
            offset = (long)*s++ - 256;
            len += 2;
        } else {
            /* Long match: 16-bit LE word. */
            unsigned int w = (unsigned int)s[0] | ((unsigned int)s[1] << 8);
            s += 2;
            if (w == 0)
                break; /* end of stream */
            offset = (long)(w >> 3) - 8192;
            len = w & 7;
            if (len != 0)
                len += 2;
            else
                len = (long)*s++ + 1;
        }

        /* Overlapping copies are legal — must stay byte-at-a-time. */
        const unsigned char* p = d + offset;
        while (len-- > 0)
            *d++ = *p++;
    }

#undef NEXT_BIT

    return (int)(d - out_start);
}
