/*
 * Game disc access for the PC port.
 *
 * The game reads its data the way it would on a PS2: by file name for the
 * directory lookup, then by 2048-byte sector. The port serves those reads
 * straight from the user's own ISO image of the disc.
 */
#ifndef PC_DISC_H
#define PC_DISC_H

#define PC_DISC_SECTOR 2048

/* Opens the ISO image. The path comes from the CVX_ISO environment variable,
 * or defaults to "cvx.iso" in the working directory. Returns 0 on failure. */
int pc_disc_open(void);

/* Looks up a file such as "\\RDX_LNK.AFS;1" or "\\MOVIE\\OPEN.SFD".
 * The match ignores case and the ";1" version suffix. Returns 0 if absent. */
int pc_disc_find(const char *name, unsigned int *lsn, unsigned int *size);

/* Calls fn for every file of the disc, with its path ("\\DIR\\NAME.EXT"). */
int pc_disc_list(void (*fn)(const char *name, unsigned int lsn, unsigned int size, void *data), void *data);

/* Reads nsct sectors starting at lsn. Returns 0 on failure. */
int pc_disc_read(unsigned int lsn, unsigned int nsct, void *buf);

/* Reads size bytes starting at a byte offset inside the disc. */
int pc_disc_read_bytes(uint64_t offset, unsigned int size, void *buf);

#endif
