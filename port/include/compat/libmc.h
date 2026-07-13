#ifndef RECVX_COMPAT_LIBMC_H
#define RECVX_COMPAT_LIBMC_H
#include <eetypes.h>

/* Matches the real KATANA libmc.h layout exactly (sizes/offsets don't
 * matter here since nothing round-trips this struct to real hardware,
 * but ps2_MemoryCard..c declares a `static sceMcTblGetDir CardInfo[21]`
 * and reads .EntryName/.AttrFile/.FileSizeByte, so the fields need to
 * really exist, not just be an opaque pointer target). */
typedef struct sceMcTblGetDir {
    struct { unsigned char Resv2,Sec,Min,Hour; unsigned char Day,Month; unsigned short Year; } _Create;
    struct { unsigned char Resv2,Sec,Min,Hour; unsigned char Day,Month; unsigned short Year; } _Modify;
    unsigned FileSizeByte;
    unsigned short AttrFile;
    unsigned short Reserve1;
    unsigned Reserve2[2];
    unsigned char EntryName[32];
} sceMcTblGetDir;

int sceMcInit(void);
int sceMcGetInfo(int a,int b,int* c,int* d,int* e);
int sceMcSync(int mode, int* cmd, int* result);
int sceMcOpen(int port, int slot, char* name, int mode);
int sceMcClose(int fd);
int sceMcRead(int fd, void* buf, int size);
int sceMcWrite(int fd, void* buf, int size);
int sceMcMkdir(int port, int slot, char* name);
int sceMcChdir(int port, int slot, char* name, char* buf);
int sceMcGetDir(int port, int slot, char* name, unsigned flag, int maxent, sceMcTblGetDir* buf);
int sceMcFormat(int port, int slot);
#endif
