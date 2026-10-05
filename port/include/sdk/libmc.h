/* PC port shim for the PS2 SDK <libmc.h> (memory cards). */
#ifndef _libmc_h_
#define _libmc_h_

#include <eetypes.h>

typedef struct {
    u_char Resv2, Sec, Min, Hour;
    u_char Day, Month;
    u_short Year;
} sceMcStDateTime;

typedef struct {
    sceMcStDateTime _Create;
    sceMcStDateTime _Modify;
    u_int FileSizeByte;
    u_short AttrFile;
    u_short Reserve1;
    u_int Reserve2;
    u_int PdaAplNo;
    u_char EntryName[32];
} sceMcTblGetDir;

#define SCE_RDONLY  0x0001
#define SCE_WRONLY  0x0002
#define SCE_RDWR    0x0003
#define SCE_CREAT   0x0200

#define sceMcFuncNoCardInfo 1
#define sceMcFuncNoOpen     2
#define sceMcFuncNoClose    3
#define sceMcFuncNoSeek     4
#define sceMcFuncNoRead     5
#define sceMcFuncNoWrite    6
#define sceMcFuncNoFlush    10
#define sceMcFuncNoMkdir    11
#define sceMcFuncNoChDir    12
#define sceMcFuncNoGetDir   13
#define sceMcFuncNoFileInfo 14
#define sceMcFuncNoDelete   15
#define sceMcFuncNoFormat   16
#define sceMcFuncNoUnformat 17
#define sceMcFuncNoEntSpace 18

#define sceMcExecRun     0
#define sceMcExecIdle   -1
#define sceMcExecFinish  1

#define sceMcResSucceed      0
#define sceMcResChangedCard -1
#define sceMcResNoFormat    -2
#define sceMcResFullDevice  -3
#define sceMcResNoEntry     -4
#define sceMcResDeniedPermit -5
#define sceMcResNotEmpty    -6
#define sceMcResUpLimitHandle -7
#define sceMcResFailReplace -8

int sceMcInit(void);
int sceMcGetInfo(int port, int slot, int *type, int *free, int *format);
int sceMcOpen(int port, int slot, const char *name, int mode);
int sceMcClose(int fd);
int sceMcRead(int fd, void *buff, int size);
int sceMcWrite(int fd, const void *buff, int size);
int sceMcMkdir(int port, int slot, const char *name);
int sceMcChdir(int port, int slot, const char *newDir, char *oldDir);
int sceMcGetDir(int port, int slot, const char *name, unsigned mode,
                int maxent, sceMcTblGetDir *table);
int sceMcFormat(int port, int slot);
int sceMcDelete(int port, int slot, const char *name);
int sceMcSync(int mode, int *cmd, int *result);

#endif
