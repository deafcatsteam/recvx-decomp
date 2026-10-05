/*
 * Console log for testing the port.
 *
 * Everything the game and the port print (stdout and stderr) is also written
 * to cvx_log.txt next to where the game is started, so a run can be sent
 * as a file instead of copied from the console. When the program ends
 * unexpectedly (an error message, a crash) and its console window would close
 * with it, it waits for Enter first so the messages stay readable.
 */
#include "pc_host.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#define dup _dup
#define dup2 _dup2
#define read _read
#define write _write
#define close _close
#else
#include <pthread.h>
#include <unistd.h>
#endif

volatile int pc_quit_requested;
volatile int pc_turbo;

static int console_fd = -1, pipe_read = -1;
static FILE *log_file;

#ifdef _WIN32
static HANDLE log_thread;
#else
static pthread_t log_thread;
#endif

static void copy_loop(void)
{
    char buf[4096];
    int n;

    while ((n = read(pipe_read, buf, sizeof(buf))) > 0) {
        write(console_fd, buf, n);
        if (log_file != NULL) {
            fwrite(buf, 1, n, log_file);
            fflush(log_file);
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI copy_thread(LPVOID arg)
{
    (void)arg;
    copy_loop();
    return 0;
}
#else
static void *copy_thread(void *arg)
{
    (void)arg;
    copy_loop();
    return NULL;
}
#endif

/* Stops copying into the log: output goes straight to the console again,
 * after everything already printed has reached the log. */
static void log_finish(void)
{
    if (console_fd < 0)
        return;
    fflush(stdout);
    fflush(stderr);
    /* Replacing fds 1 and 2 closes the last write ends of the pipe. */
    dup2(console_fd, 1);
    dup2(console_fd, 2);
#ifdef _WIN32
    WaitForSingleObject(log_thread, 2000);
#else
    pthread_join(log_thread, NULL);
#endif
    console_fd = -1;
    if (log_file != NULL)
        fclose(log_file);
    log_file = NULL;
}

/* Keeps the console open when it was opened just for this program (started
 * from the Explorer rather than from a command prompt). */
static void wait_before_closing(void)
{
#ifdef _WIN32
    DWORD procs[4];
    if (GetConsoleProcessList(procs, 4) != 1)
        return;
    fprintf(stderr, "\nLe jeu s'est arrete. Le texte ci-dessus est aussi dans cvx_log.txt.\n"
                    "Appuie sur Entree pour fermer.\n");
    getchar();
#endif
}

static void at_exit(void)
{
    log_finish();
    if (!pc_quit_requested)
        wait_before_closing();
}

void pc_log_start(void)
{
    int fds[2];

    atexit(at_exit);
    if (getenv("CVX_NO_LOG") != NULL)
        return;
    log_file = fopen("cvx_log.txt", "w");
    if (log_file == NULL || pipe(fds) != 0)
        return;
    fflush(stdout);
    fflush(stderr);
    console_fd = dup(1);
    if (console_fd < 0) {
        close(fds[0]);
        close(fds[1]);
        return;
    }
    pipe_read = fds[0];
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    close(fds[1]);
#ifdef _WIN32
    log_thread = CreateThread(NULL, 0, copy_thread, NULL, 0, NULL);
#else
    pthread_create(&log_thread, NULL, copy_thread, NULL);
#endif
}

void pc_log_crash_done(void)
{
    log_finish();
    wait_before_closing();
}
