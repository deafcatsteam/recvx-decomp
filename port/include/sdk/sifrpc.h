/* PC port shim for the PS2 SDK <sifrpc.h> (remote calls to the IOP). */
#ifndef _sifrpc_h_
#define _sifrpc_h_

#include <eetypes.h>

typedef void (*sceSifEndFunc)(void *data);

typedef struct {
    u_int psize:16;
    u_int dsize:16;
    u_int daddr;
    u_int rec_id;
    u_int pkt_addr;
    u_int rpc_id;
} sceSifCmdHdr;

typedef struct {
    sceSifCmdHdr hdr;
    void *paddr;
    u_int pid;
    int tid;
    u_int mode;
} sceSifRpcData;

typedef struct _sif_serve_data sceSifServeData;

typedef struct {
    sceSifRpcData rpcd;
    u_int command;
    void *buff;
    void *cbuff;
    sceSifEndFunc func;
    void *para;
    sceSifServeData *serve;
} sceSifClientData;

typedef struct {
    sceSifRpcData rpcd;
    u_int src;
    u_int dest;
    int size;
} sceSifReceiveData;

#define SIF_RPCM_NOWAIT 0x01
#define SIF_RPCM_NOWBDC 0x02

void sceSifInitRpc(u_int mode);
int sceSifBindRpc(sceSifClientData *bd, u_int request, u_int mode);
int sceSifCallRpc(sceSifClientData *bd, u_int fno, u_int mode, void *send,
                  int ssize, void *receive, int rsize, sceSifEndFunc end_function,
                  void *end_param);
int sceSifCheckStatRpc(sceSifRpcData *cd);
int sceSifGetOtherData(sceSifReceiveData *rd, void *src, void *dest, int size,
                       u_int mode);

#endif
