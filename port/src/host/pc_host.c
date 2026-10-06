/* Host services for the platform layer (see pc_host.h). */
#include "pc_host.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

int64_t pc_host_time_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;

    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (int64_t)((double)t.QuadPart * 1e9 / (double)freq.QuadPart);
#else
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
#endif
}

void pc_host_sleep_ns(int64_t ns)
{
#ifdef _WIN32
    Sleep((DWORD)(ns / 1000000));
#else
    struct timespec wait = { (time_t)(ns / 1000000000), (long)(ns % 1000000000) };

    nanosleep(&wait, NULL);
#endif
}

PcOverlay pc_overlay;
volatile int pc_wide_shown;
