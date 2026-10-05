/* PC port shim for the PS2 SDK <libcdvd.h> (disc access). */
#ifndef _libcdvd_h_
#define _libcdvd_h_

#include <eetypes.h>

typedef struct {
    u_int lsn;
    u_int size;
    char name[16];
    u_char date[8];
} sceCdlFILE;

typedef struct {
    u_char trycount;
    u_char spindlctrl;
    u_char datapattern;
    u_char pad;
} sceCdRMode;

typedef struct {
    u_char stat;
    u_char second;
    u_char minute;
    u_char hour;
    u_char pad;
    u_char day;
    u_char month;
    u_char year;
} sceCdCLOCK;

#define SCECdINIT       0x00
#define SCECdINoD       0x01
#define SCECdEXIT       0x05
#define SCECdCD         1
#define SCECdDVD        2
#define SCECdSpinMax    0
#define SCECdSpinNom    1
#define SCECdSpinStm    0
#define SCECdSecS2048   0
#define SCECdComplete   0x02
#define SCECdNotReady   0x06

int sceCdInit(int init_mode);
int sceCdMmode(int media);
int sceCdDiskReady(int mode);
int sceCdGetError(void);
int sceCdSearchFile(sceCdlFILE *fp, const char *name);
int sceCdRead(u_int lsn, u_int sectors, void *buf, sceCdRMode *mode);
int sceCdSync(int mode);
int sceCdPause(void);
int sceCdReadClock(sceCdCLOCK *rtc);
int sceCdStInit(u_int bufmax, u_int bankmax, u_int iop_bufaddr);
int sceCdStStart(u_int lsn, sceCdRMode *mode);
int sceCdStRead(u_int sectors, u_int *buf, u_int mode, u_int *err);
int sceCdStStop(void);

#endif
