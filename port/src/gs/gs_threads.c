/*
 * Worker threads for the software GS (see gs_threads.h).
 */
#include "gs_threads.h"

#include <stdint.h>
#include <stdlib.h>

#define MAX_THREADS 8

#ifdef _WIN32
#include <windows.h>
typedef SRWLOCK Lock;
typedef CONDITION_VARIABLE Cond;
#define lock_init(l) InitializeSRWLock(l)
#define lock(l) AcquireSRWLockExclusive(l)
#define unlock(l) ReleaseSRWLockExclusive(l)
#define cond_init(c) InitializeConditionVariable(c)
#define cond_wait(c, l) SleepConditionVariableSRW(c, l, INFINITE, 0)
#define cond_broadcast(c) WakeAllConditionVariable(c)
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_mutex_t Lock;
typedef pthread_cond_t Cond;
#define lock_init(l) pthread_mutex_init(l, NULL)
#define lock(l) pthread_mutex_lock(l)
#define unlock(l) pthread_mutex_unlock(l)
#define cond_init(c) pthread_cond_init(c, NULL)
#define cond_wait(c, l) pthread_cond_wait(c, l)
#define cond_broadcast(c) pthread_cond_broadcast(c)
#endif

static int nthreads = -1; /* including the calling thread */
static Lock mutex;
static Cond work_cond, done_cond;

/* The job being run: bands [0, nbands) of [begin, end). */
static GsBandFn job_fn;
static void *job_ctx;
static int job_begin, job_end, job_bands, next_band, bands_left;
static unsigned job_id;

static void band_range(int band, int *b0, int *b1)
{
    int n = job_end - job_begin;
    *b0 = job_begin + (int)((int64_t)n * band / job_bands);
    *b1 = job_begin + (int)((int64_t)n * (band + 1) / job_bands);
}

/* Runs bands of the current job until none is left. Called with the lock. */
static void run_bands(void)
{
    while (next_band < job_bands) {
        int band = next_band++, b0, b1;
        GsBandFn fn = job_fn;
        void *ctx = job_ctx;
        band_range(band, &b0, &b1);
        unlock(&mutex);
        fn(ctx, b0, b1);
        lock(&mutex);
        if (--bands_left == 0)
            cond_broadcast(&done_cond);
    }
}

static void worker(void)
{
    unsigned seen = 0;

    lock(&mutex);
    for (;;) {
        while (job_id == seen)
            cond_wait(&work_cond, &mutex);
        seen = job_id;
        run_bands();
    }
}

#ifdef _WIN32
static DWORD WINAPI worker_thread(LPVOID arg)
{
    (void)arg;
    worker();
    return 0;
}
#else
static void *worker_thread(void *arg)
{
    (void)arg;
    worker();
    return NULL;
}
#endif

static void start_threads(void)
{
    const char *env = getenv("CVX_GS_THREADS");
    int cores;

#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    cores = (int)si.dwNumberOfProcessors;
#else
    cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
    nthreads = env != NULL ? atoi(env) : cores;
    if (nthreads < 1)
        nthreads = 1;
    if (nthreads > MAX_THREADS)
        nthreads = MAX_THREADS;
    if (nthreads == 1)
        return;

    lock_init(&mutex);
    cond_init(&work_cond);
    cond_init(&done_cond);
    for (int i = 1; i < nthreads; i++) {
#ifdef _WIN32
        CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);
#else
        pthread_t t;
        pthread_create(&t, NULL, worker_thread, NULL);
#endif
    }
}

void gs_parallel(GsBandFn fn, void *ctx, int begin, int end, int work)
{
    int bands;

    if (nthreads < 0)
        start_threads();
    if (end <= begin)
        return;
    bands = nthreads;
    if (bands > end - begin)
        bands = end - begin;
    /* Small jobs are not worth waking the workers. */
    if (bands <= 1 || work < 8192) {
        fn(ctx, begin, end);
        return;
    }

    lock(&mutex);
    job_fn = fn;
    job_ctx = ctx;
    job_begin = begin;
    job_end = end;
    job_bands = bands * 2; /* smaller bands even out the load */
    if (job_bands > end - begin)
        job_bands = end - begin;
    next_band = 0;
    bands_left = job_bands;
    job_id++;
    cond_broadcast(&work_cond);
    run_bands();
    while (bands_left > 0)
        cond_wait(&done_cond, &mutex);
    unlock(&mutex);
}
