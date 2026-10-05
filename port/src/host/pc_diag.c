/*
 * Diagnostics for testing the port on real data.
 *
 * A background thread prints a status line every second (frames reached and
 * GS activity). If the game stops reaching frames for a few seconds, it
 * prints where the main thread is, as a list of functions, so a hang can be
 * located from a plain console log. CVX_QUIET turns the status lines off.
 */
#include "pc_host.h"

#include "../gs/gs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile uint32_t pc_diag_vblanks;

#define STALL_SECONDS 3
#define MAX_FRAMES 12

#ifdef _WIN32
#include <windows.h>

static HANDLE main_thread;

/* Function symbols from the executable's COFF symbol table. */
typedef struct {
    uint32_t addr;
    char name[48];
} Sym;
static Sym *syms;
static int nsyms;

static int sym_cmp(const void *a, const void *b)
{
    uint32_t x = ((const Sym *)a)->addr, y = ((const Sym *)b)->addr;
    return x < y ? -1 : x > y;
}

static void load_symbols(void)
{
    char path[MAX_PATH];
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    uint32_t symoff = nt->FileHeader.PointerToSymbolTable, count = nt->FileHeader.NumberOfSymbols;
    FILE *f;
    unsigned char *table;
    uint32_t strsize;

    if (syms != NULL || symoff == 0 || GetModuleFileNameA(NULL, path, sizeof(path)) == 0)
        return;
    f = fopen(path, "rb");
    if (f == NULL)
        return;
    fseek(f, symoff + count * 18, SEEK_SET);
    if (fread(&strsize, 4, 1, f) != 1)
        strsize = 4;
    table = malloc(count * 18 + strsize);
    fseek(f, symoff, SEEK_SET);
    if (table == NULL || fread(table, 1, count * 18 + strsize, f) != count * 18 + strsize) {
        fclose(f);
        free(table);
        return;
    }
    fclose(f);

    syms = calloc(count, sizeof(Sym));
    for (uint32_t i = 0; i < count; i++) {
        unsigned char *e = table + i * 18;
        uint32_t value, zero, off;
        int16_t secnum;
        uint16_t type;
        memcpy(&value, e + 8, 4);
        memcpy(&secnum, e + 12, 2);
        memcpy(&type, e + 14, 2);
        if (secnum > 0 && secnum <= nt->FileHeader.NumberOfSections && (type & 0x30) == 0x20 &&
            (e[16] == 2 || e[16] == 3)) {
            Sym *s = &syms[nsyms++];
            memcpy(&zero, e, 4);
            memcpy(&off, e + 4, 4);
            if (zero == 0 && off < strsize)
                snprintf(s->name, sizeof(s->name), "%s", (char *)table + count * 18 + off);
            else
                snprintf(s->name, sizeof(s->name), "%.8s", (char *)e);
            s->addr = (uint32_t)(uintptr_t)base + sec[secnum - 1].VirtualAddress + value;
        }
        i += e[17]; /* auxiliary records */
    }
    free(table);
    qsort(syms, nsyms, sizeof(Sym), sym_cmp);
}

static const char *symbol_for(uint32_t addr, uint32_t *offset)
{
    int lo = 0, hi = nsyms - 1, best = -1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (syms[mid].addr <= addr) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (best < 0)
        return "?";
    *offset = addr - syms[best].addr;
    return syms[best].name;
}

static void print_main_stack(void)
{
    CONTEXT ctx;
    uint32_t pcs[MAX_FRAMES];
    int n = 0;

    load_symbols();
    if (SuspendThread(main_thread) == (DWORD)-1)
        return;
    ctx.ContextFlags = CONTEXT_CONTROL;
    if (GetThreadContext(main_thread, &ctx)) {
        uint32_t ebp = ctx.Ebp;
        pcs[n++] = ctx.Eip;
        /* Walk the frame pointer chain (the game is built with frame pointers). */
        while (n < MAX_FRAMES && ebp != 0 && !IsBadReadPtr((void *)(uintptr_t)ebp, 8)) {
            uint32_t next = ((uint32_t *)(uintptr_t)ebp)[0];
            pcs[n++] = ((uint32_t *)(uintptr_t)ebp)[1];
            if (next <= ebp)
                break;
            ebp = next;
        }
    }
    ResumeThread(main_thread);

    for (int i = 0; i < n; i++) {
        uint32_t off = 0;
        const char *name = symbol_for(pcs[i], &off);
        fprintf(stderr, "    %08x  %s+0x%x\n", pcs[i], name, off);
    }
}

#else /* POSIX */
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

static pthread_t main_thread;

static void dump_stack(int sig)
{
    void *pcs[MAX_FRAMES + 2];
    int n = backtrace(pcs, MAX_FRAMES + 2);

    (void)sig;
    /* Skip this handler and the signal trampoline. */
    backtrace_symbols_fd(pcs + 2, n > 2 ? n - 2 : 0, 2);
}

static void print_main_stack(void)
{
    pthread_kill(main_thread, SIGUSR1);
    pc_host_sleep_ns(200000000);
}

#endif

static void diag_loop(void)
{
    uint32_t last = pc_diag_vblanks;
    int stalled = 0, seconds = 0, quiet = getenv("CVX_QUIET") != NULL;
    char line[256];

    for (;;) {
        pc_host_sleep_ns(1000000000);
        seconds++;
        uint32_t now = pc_diag_vblanks;
        if (!quiet) {
            gs_debug_status(line, sizeof(line));
            fprintf(stderr, "[%3ds] %u frames/s | %s\n", seconds, now - last, line);
        }
        if (now == last) {
            if (++stalled == STALL_SECONDS) {
                fprintf(stderr, "[%3ds] the game has not reached a frame for %d s; it is in:\n",
                        seconds, STALL_SECONDS);
                print_main_stack();
            }
        } else {
            stalled = 0;
        }
        last = now;
    }
}

#ifdef _WIN32
static DWORD WINAPI diag_thread(LPVOID arg)
{
    (void)arg;
    diag_loop();
    return 0;
}
#else
static void *diag_thread(void *arg)
{
    (void)arg;
    diag_loop();
    return NULL;
}
#endif

void pc_diag_start(void)
{
#ifdef _WIN32
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(NULL, 0, diag_thread, NULL, 0, NULL);
#else
    pthread_t t;
    main_thread = pthread_self();
    signal(SIGUSR1, dump_stack);
    pthread_create(&t, NULL, diag_thread, NULL);
#endif
}
