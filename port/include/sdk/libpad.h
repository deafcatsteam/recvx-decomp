/* PC port shim for the PS2 SDK <libpad.h> (controllers). */
#ifndef _libpad_h_
#define _libpad_h_

#include <eetypes.h>

#define scePadDmaBufferMax 16

#define scePadStateDisconn   0
#define scePadStateFindPad   1
#define scePadStateFindCTP1  2
#define scePadStateExecCmd   5
#define scePadStateStable    6
#define scePadStateError     7

#define scePadReqStateComplete 0
#define scePadReqStateFaild    1
#define scePadReqStateBusy     2

#define InfoModeCurID     1
#define InfoModeCurExID   2
#define InfoModeCurExOffs 3
#define InfoModeIdTable   4

int scePadInit(int mode);
int scePadPortOpen(int port, int slot, u_long128 *addr);
int scePadPortClose(int port, int slot);
int scePadRead(int port, int slot, u_char *rdata);
int scePadGetState(int port, int slot);
int scePadGetReqState(int port, int slot);
int scePadInfoMode(int port, int slot, int term, int offs);
int scePadSetMainMode(int port, int slot, int offs, int lock);
int scePadInfoPressMode(int port, int slot);
int scePadEnterPressMode(int port, int slot);
int scePadSetActDirect(int port, int slot, const u_char *data);
int scePadSetActAlign(int port, int slot, const u_char *data);

#endif
