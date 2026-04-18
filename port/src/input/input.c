/*
 * Input plumbing: SDL keyboard/gamepad -> Sega Ninja PDS_PERIPHERAL.
 *
 * The game calls njGetPeripheral(port) which we resolve to the address of
 * the static peripheral we maintain here. Bit layout matches sg_pad.h:
 *
 *   KU=4, KD=5, KL=6, KR=7      digital direction A (d-pad)
 *   ST=3                        Start
 *   TA=2 (Cross), TB=1 (Circle), TC=0 (Square on PS2 DS2 via ninja adapter)
 *   TX=10, TY=9, TZ=8, TD=11    shoulder / extra face buttons
 *   TR=16, TL=17                L/R emulation bits
 *
 * Note: there is no "Select" bit in the Sega Ninja peripheral layout. The
 * pads used by the decomp route Select through its scePad code path; for
 * now we just drop it — menu/title tasks don't require Select.
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

/* Digital button bit constants (must match sg_pad.h PDD_DGT_*) */
#define RX_DGT_TC  (1u << 0)   /* Square (PS2) */
#define RX_DGT_TB  (1u << 1)   /* Circle */
#define RX_DGT_TA  (1u << 2)   /* Cross  */
#define RX_DGT_ST  (1u << 3)   /* Start  */
#define RX_DGT_KU  (1u << 4)
#define RX_DGT_KD  (1u << 5)
#define RX_DGT_KL  (1u << 6)
#define RX_DGT_KR  (1u << 7)
#define RX_DGT_TZ  (1u << 8)
#define RX_DGT_TY  (1u << 9)   /* Triangle (PS2) */
#define RX_DGT_TX  (1u << 10)
#define RX_DGT_TD  (1u << 11)
#define RX_DGT_TR  (1u << 16)  /* R1 */
#define RX_DGT_TL  (1u << 17)  /* L1 */

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

static const uint32_t key_to_bit[RX_KEY__COUNT] = {
    [RX_KEY_UP]     = RX_DGT_KU,
    [RX_KEY_DOWN]   = RX_DGT_KD,
    [RX_KEY_LEFT]   = RX_DGT_KL,
    [RX_KEY_RIGHT]  = RX_DGT_KR,
    [RX_KEY_ACTION] = RX_DGT_TA,
    [RX_KEY_CANCEL] = RX_DGT_TB,
    [RX_KEY_AIM]    = RX_DGT_TC,
    [RX_KEY_MENU]   = RX_DGT_TY,
    [RX_KEY_L1]     = RX_DGT_TL,
    [RX_KEY_R1]     = RX_DGT_TR,
    [RX_KEY_L2]     = RX_DGT_TZ,
    [RX_KEY_R2]     = RX_DGT_TX,
    [RX_KEY_START]  = RX_DGT_ST,
    [RX_KEY_SELECT] = 0,  /* no bit — see file header */
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
    g_per.press   = on & ~prev;
    g_per.release = prev & ~on;
    g_per.off     = ~on;
    g_prev_on = on;
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
