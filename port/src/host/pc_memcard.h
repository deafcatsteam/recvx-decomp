/*
 * The memory card in slot 1, kept as a folder on the PC (see pc_memcard.c).
 *
 * Paths are the card's own ("/BASLUS-20184/SAVEDATA-00"), absolute or
 * relative to the card's current directory. Errors are the PS2 library's
 * result codes, so the SDK layer can hand them to the game as they are.
 */
#ifndef PC_MEMCARD_H
#define PC_MEMCARD_H

#include <stdint.h>

#define PC_MC_FAILED   (-1)  /* the PC could not read or write the file */
#define PC_MC_NOTFOUND (-4)  /* no such file or folder (or it already exists) */
#define PC_MC_DENIED   (-5)  /* folder not empty, file is a folder, ... */
#define PC_MC_NOCARD   (-10) /* no card in this slot */

#define PC_MC_CREATE 0x200   /* open mode: create the file if missing */

typedef struct {
    char name[32];
    uint32_t size;
    int is_dir;
    int64_t mtime; /* seconds since 1970, 0 if unknown */
} PcMcEntry;

/* Folder that holds the card (CVX_SAVES, by default "saves"). */
const char *pc_mc_root(void);

int pc_mc_card(int port);     /* 1 when a card is in the slot */
int pc_mc_free_kb(int port);  /* free space, in kilobytes (an 8 MB card) */

/* Files: one at a time, like the game uses them. Open returns the file
 * number (0) or an error; writes reach the PC file when it is closed, in a
 * single safe replace, so a crash never leaves half a save. */
int pc_mc_open(int port, const char *path, int mode);
int pc_mc_read(int fd, void *buf, int size);
int pc_mc_write(int fd, const void *buf, int size);
int pc_mc_close(int fd);

int pc_mc_mkdir(int port, const char *path);
int pc_mc_chdir(int port, const char *path, char *old /* 1024 bytes, or NULL */);
int pc_mc_delete(int port, const char *path);

/* Lists a folder ("/DIR/*": ".", ".." then the files, by name) or one entry
 * ("/DIR/FILE"). Returns how many entries were written (at most max). */
int pc_mc_getdir(int port, const char *path, int max, PcMcEntry *out);

#endif
