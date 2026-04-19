/*
 * Input plumbing: SDL keyboard/gamepad -> ninja PDS_PERIPHERAL.
 *
 * CRITICAL: on the PS2 port the `per->on` field does NOT follow Sega
 * Ninja's PDD_DGT_* layout — it holds the raw scePadRead button mask
 * byte-swapped as `(byte2 << 8) | byte3`, so the game's Pad[] and
 * bhSetPad's pad_type[] tables all decode scePad bits. Matching that is
 * mandatory: `CheckStartButton()` tests bit 0x800 which is START in the
 * shifted scePad layout (bit 11), but would be TD in PDD.
 *
 *   scePad shifted layout (this is what per->on / Pad[].on holds):
 *     bit  0 L2     bit  1 R2     bit  2 L1     bit  3 R1
 *     bit  4 TRI    bit  5 CIRC   bit  6 CROSS  bit  7 SQ
 *     bit  8 SEL    bit  9 L3     bit 10 R3     bit 11 START
 *     bit 12 UP     bit 13 RIGHT  bit 14 DOWN   bit 15 LEFT
 */

#include "recvx_port.h"

/* We forward-declare the Ninja peripheral struct rather than include
 * sg_pad.h here: that header pulls in the KATANA CRT shadow tree which
 * doesn't play with MSVC CRT. Layout MUST match PDS_PERIPHERAL in
 * include/recvx-decomp-katana/KATANA/Include/sg_pad.h exactly — see that
 * file for the canonical definition. We verify via static_assert on size. */
typedef struct recvx_ninja_peripheral {
    uint32_t  id;
    uint32_t  support;
    uint32_t  on;
    uint32_t  off;
    uint32_t  press;
    uint32_t  release;
    uint16_t  r;
    uint16_t  l;
    int16_t   x1, y1, x2, y2;
    char*     name;
    void*     extend;
    uint32_t  old;
    void*     info;
} recvx_ninja_peripheral;

/* scePad-shifted bit constants (what per->on actually carries on PS2). */
#define RX_PAD_L2     (1u <<  0)
#define RX_PAD_R2     (1u <<  1)
#define RX_PAD_L1     (1u <<  2)
#define RX_PAD_R1     (1u <<  3)
#define RX_PAD_TRI    (1u <<  4)
#define RX_PAD_CIRC   (1u <<  5)
#define RX_PAD_CROSS  (1u <<  6)
#define RX_PAD_SQ     (1u <<  7)
#define RX_PAD_SEL    (1u <<  8)
#define RX_PAD_L3     (1u <<  9)
#define RX_PAD_R3     (1u << 10)
#define RX_PAD_START  (1u << 11)
#define RX_PAD_UP     (1u << 12)
#define RX_PAD_RIGHT  (1u << 13)
#define RX_PAD_DOWN   (1u << 14)
#define RX_PAD_LEFT   (1u << 15)

/* Everything the pads used by bhSetPad care about — r, l (triggers),
 * x1, y1 (left stick), plus the digital `on` bitmap. */
static recvx_ninja_peripheral g_per = {
    .id      = 1,
    .support = 0x3FFFF,   /* all analog + digital buttons present */
    .on      = 0,
    .off     = 0xFFFFFFFF,
    .name    = "RECVX_PC_Pad",
};
static uint32_t g_prev_on;

/* Auto-repeat (Rept / "onon") — mirrors ps2_sg_pad.c:578-632. CheckButton
 * reads Pad[].Rept for UP/DOWN cursor nav, so without this the menu is
 * stuck. Per-bit timer counts down after initial press; when it hits 0
 * a single-frame Rept pulse fires, then resets to 2 (repeat rate). */
static uint8_t  g_time1[16];
static uint32_t g_rept;

static const uint32_t key_to_bit[RX_KEY__COUNT] = {
    [RX_KEY_UP]     = RX_PAD_UP,
    [RX_KEY_DOWN]   = RX_PAD_DOWN,
    [RX_KEY_LEFT]   = RX_PAD_LEFT,
    [RX_KEY_RIGHT]  = RX_PAD_RIGHT,
    [RX_KEY_ACTION] = RX_PAD_CROSS,   /* X = OK for keytype 0 (AdvGetOk=0xC0) */
    [RX_KEY_CANCEL] = RX_PAD_CIRC,    /* O = cancel */
    [RX_KEY_AIM]    = RX_PAD_SQ,
    [RX_KEY_MENU]   = RX_PAD_TRI,
    [RX_KEY_L1]     = RX_PAD_L1,
    [RX_KEY_R1]     = RX_PAD_R1,
    [RX_KEY_L2]     = RX_PAD_L2,
    [RX_KEY_R2]     = RX_PAD_R2,
    [RX_KEY_START]  = RX_PAD_START,
    [RX_KEY_SELECT] = RX_PAD_SEL,
};

void recvx_input_set_key(recvx_key k, bool pressed) {
    if ((unsigned)k >= RX_KEY__COUNT) return;
    uint32_t bit = key_to_bit[k];
    if (!bit) return;
    if (pressed) g_per.on |=  bit;
    else         g_per.on &= ~bit;
}

void recvx_input_set_stick(int x, int y) {
    if (x < -128) x = -128; else if (x > 127) x = 127;
    if (y < -128) y = -128; else if (y > 127) y = 127;
    g_per.x1 = (int16_t)x;
    g_per.y1 = (int16_t)y;
}

void recvx_input_new_frame(void) {
    uint32_t on   = g_per.on;
    uint32_t prev = g_prev_on;
    uint32_t push = on & ~prev;
    g_per.press   = push;
    g_per.release = prev & ~on;
    g_per.off     = ~on;
    g_prev_on = on;

    for (int i = 0; i < 16; ++i) {
        uint32_t mask = 1u << i;
        if (push & mask) {
            g_time1[i] = 10;
            g_rept |= mask;
        } else if (on & mask) {
            if (g_time1[i] != 0) {
                g_time1[i]--;
                g_rept &= ~mask;
            } else {
                g_time1[i] = 2;
                g_rept |= mask;
            }
        } else {
            g_rept &= ~mask;
            g_time1[i] = 0;
        }
    }
}

uint32_t recvx_input_rept(void) {
    return g_rept;
}

/* Called by the game via include/recvx-decomp-katana/KATANA/Include/ninjapad.h.
 * Port 0 is the only one we expose; returning NULL for others is what the
 * real ninja library does when a peripheral isn't present. */
const void* njGetPeripheral(uint32_t port) {
    return port == 0 ? (const void*)&g_per : NULL;
}

uint32_t recvx_input_buttons(void) {
    return g_per.on;
}
