/* PC port shim for the PS2 SDK <libmpeg.h> (MPEG-2 movie playback). */
#ifndef _libmpeg_h_
#define _libmpeg_h_

#include <eetypes.h>
#include <libipu.h>

typedef enum {
    sceMpegCbError,
    sceMpegCbNodata,
    sceMpegCbStopDMA,
    sceMpegCbRestartDMA,
    sceMpegCbBackground,
    sceMpegCbTimeStamp,
    sceMpegCbStr
} sceMpegCbType;

typedef enum {
    sceMpegStrM2V = 0,
    sceMpegStrIPU = 1,
    sceMpegStrPCM = 2,
    sceMpegStrADPCM = 3,
    sceMpegStrDATA = 4
} sceMpegStrType;

typedef struct {
    int width;
    int height;
    int frameCount;
    long long pts;
    long long dts;
    u_long flags;
    long long pts2nd;
    long long dts2nd;
    u_long flags2nd;
    void *sys;
} sceMpeg;

typedef struct {
    sceMpegCbType type;
} sceMpegCbData;

typedef struct {
    sceMpegCbType type;
    char *errMessage;
} sceMpegCbDataError;

typedef struct {
    sceMpegCbType type;
    long long pts;
    long long dts;
} sceMpegCbDataTimeStamp;

typedef struct {
    sceMpegCbType type;
    u_char *header;
    u_char *data;
    u_int len;
    long long pts;
    long long dts;
} sceMpegCbDataStr;

typedef int (*sceMpegCallback)(sceMpeg *mp, sceMpegCbData *cbdata, void *anyData);

int sceMpegInit(void);
int sceMpegCreate(sceMpeg *mp, u_char *work_area, int work_area_size);
int sceMpegDelete(sceMpeg *mp);
int sceMpegReset(sceMpeg *mp);
int sceMpegIsEnd(sceMpeg *mp);
int sceMpegGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int mbcount);
sceMpegCallback sceMpegAddCallback(sceMpeg *mp, sceMpegCbType type,
                                   sceMpegCallback callback, void *anyData);
sceMpegCallback sceMpegAddStrCallback(sceMpeg *mp, sceMpegStrType strType,
                                      int ch, sceMpegCallback callback, void *anyData);
int sceMpegDemuxPssRing(sceMpeg *mp, u_char *start, int size,
                        u_char *ring_start, int ring_size);

#endif
