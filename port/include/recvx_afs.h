/*
 * recvx_afs.h — minimal Sega AFS container reader.
 *
 * Sega AFS is a "files-in-a-file" archive used for BGM?.AFS / VOICE?.AFS /
 * SYSTEM.AFS / etc. Layout:
 *
 *   0x00  "AFS\0"               magic
 *   0x04  u32  entry_count      little-endian
 *   0x08  { u32 offset, u32 size } x entry_count   — "TOC"
 *   ...   optional filename table (48 bytes per entry), unused here
 *   ...   raw sub-file data, offsets absolute from start of file,
 *         padded up to a sector boundary
 *
 * The real game talks to these through CRI ADXF (async, DVD-backed). Our
 * port re-implements the subset the boot chain actually uses: open, look
 * up entry size, synchronous read into a caller buffer.
 */
#ifndef RECVX_AFS_H
#define RECVX_AFS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct recvx_afs recvx_afs_t;

recvx_afs_t* recvx_afs_open (const char* path);
void         recvx_afs_close(recvx_afs_t* a);

/* Number of entries in the TOC. */
unsigned     recvx_afs_count(const recvx_afs_t* a);

/* Size in bytes of entry `idx`, or 0 if idx is out of range / entry empty. */
uint32_t     recvx_afs_entry_size(const recvx_afs_t* a, unsigned idx);

/* Read entry `idx` in full into `dst` (assumed sized by prior call to
 * recvx_afs_entry_size). Returns bytes actually read, 0 on failure. */
uint32_t     recvx_afs_read(recvx_afs_t* a, unsigned idx, void* dst);

#ifdef __cplusplus
}
#endif

#endif
