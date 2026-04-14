#ifndef RECVX_PORT_H
#define RECVX_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Logging
 * -------------------------------------------------------------------------- */
void recvx_log(const char* tag, const char* fmt, ...);
#define RX_LOG(tag, ...)  recvx_log(tag, __VA_ARGS__)

/* --------------------------------------------------------------------------
 * Backend interface
 *
 * Every renderer (GL, Vulkan, etc.) implements the same vtable so we can
 * swap them at startup. Phase 0 ships GL + null backends only.
 * -------------------------------------------------------------------------- */
typedef struct recvx_backend_config {
    int  width;
    int  height;
    bool fullscreen;
    bool vsync;
    const char* window_title;
} recvx_backend_config;

typedef struct recvx_backend {
    const char* name;
    int  (*init)(const recvx_backend_config* cfg);
    void (*shutdown)(void);
    void (*begin_frame)(void);
    void (*end_frame)(void);
    bool (*pump_events)(void); /* false → quit requested */
} recvx_backend;

const recvx_backend* recvx_backend_gl(void);
const recvx_backend* recvx_backend_null(void);

/* --------------------------------------------------------------------------
 * ISO reader
 * -------------------------------------------------------------------------- */
typedef struct recvx_iso recvx_iso_t;

recvx_iso_t* recvx_iso_open(const char* path);
void         recvx_iso_close(recvx_iso_t* iso);
int          recvx_iso_find(recvx_iso_t* iso, const char* path,
                            uint32_t* out_lba, uint32_t* out_size);
int          recvx_iso_read_sectors(recvx_iso_t* iso, uint32_t lba,
                                    uint32_t count, void* buf);

/* Selected as the global ISO once the port has parsed its CLI / config */
void         recvx_iso_set_global(recvx_iso_t* iso);
recvx_iso_t* recvx_iso_global(void);

/* --------------------------------------------------------------------------
 * FMV (Sofdec replacement)
 * -------------------------------------------------------------------------- */
typedef struct recvx_fmv recvx_fmv_t;

recvx_fmv_t* recvx_fmv_open(const char* iso_path);
void         recvx_fmv_close(recvx_fmv_t* fmv);
bool         recvx_fmv_advance(recvx_fmv_t* fmv); /* false when finished */

#ifdef __cplusplus
}
#endif

#endif /* RECVX_PORT_H */
