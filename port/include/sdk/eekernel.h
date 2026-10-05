/* PC port shim for the PS2 SDK <eekernel.h> (threads, semaphores, caches). */
#ifndef _eekernel_h_
#define _eekernel_h_

#include <eetypes.h>

struct ThreadParam {
    int status;
    void *entry;
    void *stack;
    int stackSize;
    void *gpReg;
    int initPriority;
    int currentPriority;
    u_int attr;
    u_int option;
    int waitType;
    int waitId;
    int wakeupCount;
};

struct SemaParam {
    int currentCount;
    int maxCount;
    int initCount;
    int numWaitThreads;
    u_int attr;
    u_int option;
};

#define WRITEBACK_DCACHE 0
#define INVALIDATE_DCACHE 1
#define INVALIDATE_ICACHE 2
#define INVALIDATE_CACHE 3

int CreateThread(struct ThreadParam *param);
int DeleteThread(int tid);
int StartThread(int tid, void *arg);
int TerminateThread(int tid);
int GetThreadId(void);
int ChangeThreadPriority(int tid, int prio);
int RotateThreadReadyQueue(int prio);
int SleepThread(void);
int WakeupThread(int tid);
int iWakeupThread(int tid);
int CreateSema(struct SemaParam *param);
int DeleteSema(int sid);
int SignalSema(int sid);
int iSignalSema(int sid);
int WaitSema(int sid);
int PollSema(int sid);
int AddIntcHandler(int cause, int (*handler)(int), int next);
int RemoveIntcHandler(int cause, int hid);
int AddDmacHandler(int channel, int (*handler)(int), int next);
int RemoveDmacHandler(int channel, int hid);
int EnableIntc(int cause);
int DisableIntc(int cause);
int EnableDmac(int channel);
int DisableDmac(int channel);
void FlushCache(int operation);
void SyncDCache(void *start, void *end);
void InvalidDCache(void *start, void *end);
void iSyncDCache(void *start, void *end);
void iInvalidDCache(void *start, void *end);
void ExitHandler(void);
int DIntr(void);
int EIntr(void);
void LoadExecPS2(const char *filename, int argc, char **argv);

#define INTC_GS     0
#define INTC_VBLANK_S 2
#define INTC_VBLANK_E 3

#endif
