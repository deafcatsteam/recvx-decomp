/* PC port shim for the PS2 SDK <libgraph.h> (GS setup and double buffering). */
#ifndef _libgraph_h_
#define _libgraph_h_

#include <eestruct.h>

typedef struct {
    sceGsPmode pmode;
    sceGsSmode2 smode2;
    sceGsDispfb dispfb;
    sceGsDisplay display;
    sceGsBgcolor bgcolor;
} sceGsDispEnv;

typedef struct {
    sceGsFrame frame1;          u_long frame1addr;
    sceGsZbuf zbuf1;            u_long zbuf1addr;
    sceGsXyoffset xyoffset1;    u_long xyoffset1addr;
    sceGsScissor scissor1;      u_long scissor1addr;
    sceGsPrmodecont prmodecont; u_long prmodecontaddr;
    sceGsColclamp colclamp;     u_long colclampaddr;
    sceGsDthe dthe;             u_long dtheaddr;
    sceGsTest test1;            u_long test1addr;
} __attribute__((aligned(16))) sceGsDrawEnv1;

typedef struct {
    sceGsFrame frame2;          u_long frame2addr;
    sceGsZbuf zbuf2;            u_long zbuf2addr;
    sceGsXyoffset xyoffset2;    u_long xyoffset2addr;
    sceGsScissor scissor2;      u_long scissor2addr;
    sceGsPrmodecont prmodecont; u_long prmodecontaddr;
    sceGsColclamp colclamp;     u_long colclampaddr;
    sceGsDthe dthe;             u_long dtheaddr;
    sceGsTest test2;            u_long test2addr;
} __attribute__((aligned(16))) sceGsDrawEnv2;

typedef struct {
    sceGsTest testa;  u_long testaaddr;
    sceGsPrim prim;   u_long primaddr;
    sceGsRgbaq rgbaq; u_long rgbaqaddr;
    sceGsXyz xyz2a;   u_long xyz2aaddr;
    sceGsXyz xyz2b;   u_long xyz2baddr;
    sceGsTest testb;  u_long testbaddr;
} __attribute__((aligned(16))) sceGsClear;

typedef struct {
    sceGsDispEnv disp[2];
    sceGifTag giftag0;
    sceGsDrawEnv1 draw01;
    sceGsDrawEnv2 draw02;
    sceGsClear clear0;
    sceGifTag giftag1;
    sceGsDrawEnv1 draw11;
    sceGsDrawEnv2 draw12;
    sceGsClear clear1;
} __attribute__((aligned(16))) sceGsDBuffDc;

typedef struct {
    sceGifTag giftag0;
    sceGsBitbltbuf bitbltbuf; u_long bitbltbufaddr;
    sceGsTrxpos trxpos;       u_long trxposaddr;
    sceGsTrxreg trxreg;       u_long trxregaddr;
    sceGsTrxdir trxdir;       u_long trxdiraddr;
    sceGifTag giftag1;
} __attribute__((aligned(16))) sceGsLoadImage;

typedef struct {
    u_int vifcode[4];
    sceGifTag giftag;
    sceGsBitbltbuf bitbltbuf; u_long bitbltbufaddr;
    sceGsTrxpos trxpos;       u_long trxposaddr;
    sceGsTrxreg trxreg;       u_long trxregaddr;
    sceGsFinish finish;       u_long finishaddr;
    sceGsTrxdir trxdir;       u_long trxdiraddr;
} __attribute__((aligned(16))) sceGsStoreImage;

int sceGsResetGraph(short mode, short inter, short omode, short ffmode);
void sceGsResetPath(void);
int sceGsSyncV(int mode);
int sceGsSyncPath(int mode, u_short timeout);
void sceGsSetDefDBuffDc(sceGsDBuffDc *db, short psm, short w, short h,
                        short ztest, short zpsm, short clear);
int sceGsSwapDBuffDc(sceGsDBuffDc *db, int id);
int sceGsSetDefLoadImage(sceGsLoadImage *lp, short dbp, short dbw, short dpsm,
                         short x, short y, short w, short h);
int sceGsExecLoadImage(sceGsLoadImage *lp, u_long128 *srcaddr);
int sceGsSetDefStoreImage(sceGsStoreImage *sp, short sbp, short sbw, short spsm,
                          short x, short y, short w, short h);
int sceGsExecStoreImage(sceGsStoreImage *sp, u_long128 *dstaddr);

#endif
