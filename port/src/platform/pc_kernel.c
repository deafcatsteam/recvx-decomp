/*
 * PC replacement for the PS2 EE kernel calls the game uses.
 *
 * The port runs the game on a single thread, so threads and semaphores reduce
 * to bookkeeping: semaphores are plain counters, and work the PS2 would hand
 * to another processor (IOP RPCs) completes immediately. Interrupt handlers
 * are kept and the V-blank ones are run by pc_vblank(), once per frame.
 */
#include <eekernel.h>
#include <stdio.h>
#include <stdlib.h>

#include "../host/pc_host.h"
#include "pc_platform.h"

#define PC_INTC_MAX 16
#define PC_SEMA_MAX 64

static int (*intc_handler[PC_INTC_MAX])(int);
static int intc_enabled[PC_INTC_MAX];

static struct {
    int used;
    int count;
    int max;
} sema[PC_SEMA_MAX];

int AddIntcHandler(int cause, int (*handler)(int), int next)
{
    if (cause < 0 || cause >= PC_INTC_MAX)
        return -1;
    intc_handler[cause] = handler;
    return cause + 1;
}

int RemoveIntcHandler(int cause, int hid)
{
    if (cause >= 0 && cause < PC_INTC_MAX)
        intc_handler[cause] = NULL;
    return 0;
}

int EnableIntc(int cause)
{
    if (cause >= 0 && cause < PC_INTC_MAX)
        intc_enabled[cause] = 1;
    return 1;
}

int DisableIntc(int cause)
{
    if (cause >= 0 && cause < PC_INTC_MAX)
        intc_enabled[cause] = 0;
    return 1;
}

void pc_vblank(void)
{
    /* INTC_VBLANK_S is cause 2, INTC_VBLANK_E cause 3. */
    for (int cause = 2; cause <= 3; cause++) {
        if (intc_enabled[cause] && intc_handler[cause] != NULL)
            intc_handler[cause](cause);
    }
}

void (*pc_frame_hook)(void);

void pc_wait_vblank(void)
{
    /* NTSC field rate: 60000/1001 Hz. */
    static const int64_t period_ns = 1000000000LL * 1001 / 60000;
    static int64_t next_ns;
    static int no_vsync = -1;

    if (no_vsync < 0)
        no_vsync = getenv("CVX_NO_VSYNC") != NULL;

    if (!no_vsync && !pc_turbo) {
        int64_t now = pc_host_time_ns();
        if (next_ns == 0 || now - next_ns > period_ns * 4)
            next_ns = now; /* first frame, or fell far behind: resync */
        next_ns += period_ns;
        if (next_ns > now)
            pc_host_sleep_ns(next_ns - now);
    }

    pc_diag_vblanks++;
    if (pc_frame_hook != NULL)
        pc_frame_hook();
    pc_vblank();
}

void ExitHandler(void) {}
void FlushCache(int operation) {}
void SyncDCache(void *start, void *end) {}
void InvalidDCache(void *start, void *end) {}
void iSyncDCache(void *start, void *end) {}
void iInvalidDCache(void *start, void *end) {}

int GetThreadId(void) { return 1; }
int SleepThread(void) { return 0; }
int WakeupThread(int tid) { return tid; }
int iWakeupThread(int tid) { return tid; }

int SetAlarm(u_short time, void (*handler)(int, u_short, void *), void *arg)
{
    /* Only used to delay a thread by a few scanlines: fire right away. */
    handler(1, time, arg);
    return 1;
}

int CreateSema(struct SemaParam *param)
{
    for (int i = 1; i < PC_SEMA_MAX; i++) {
        if (!sema[i].used) {
            sema[i].used = 1;
            sema[i].count = param->initCount;
            sema[i].max = param->maxCount;
            return i;
        }
    }
    return -1;
}

int DeleteSema(int sid)
{
    if (sid > 0 && sid < PC_SEMA_MAX)
        sema[sid].used = 0;
    return sid;
}

int SignalSema(int sid)
{
    if (sid <= 0 || sid >= PC_SEMA_MAX || !sema[sid].used)
        return -1;
    if (sema[sid].count < sema[sid].max)
        sema[sid].count++;
    return sid;
}

int iSignalSema(int sid)
{
    return SignalSema(sid);
}

int PollSema(int sid)
{
    if (sid <= 0 || sid >= PC_SEMA_MAX || !sema[sid].used || sema[sid].count == 0)
        return -1;
    sema[sid].count--;
    return sid;
}

int WaitSema(int sid)
{
    if (PollSema(sid) < 0) {
        /* Nothing else runs that could signal it: waiting would hang. */
        if (sid > 0 && sid < PC_SEMA_MAX && sema[sid].used)
            fprintf(stderr, "kernel: WaitSema(%d) would block forever, continuing\n", sid);
        return -1;
    }
    return sid;
}
