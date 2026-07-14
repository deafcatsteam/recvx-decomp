/*
 * sg_* shim — Sega Ginsu helpers used by the game's higher-level wrappers.
 * Phase 0 no-ops; filled in as phases touch each subsystem.
 */

#include "recvx_port.h"

/* pad */
void pdSetMode(int m)                                 { (void)m; }

/* print/font */
/* npSetMemory moved to game_room_stubs.c — it's a real byte-fill op,
 * not a phase-0 no-op (see comment there). */
void npPlusInit(void)                                 {}

/* backup ram */
void BupInit(void)                                    {}
void BupExit(void)                                    {}

/* sound */
void InitSoundProgram(int mode)                       { (void)mode; }
void ExitSoundProgram(void)                           {}

/* expand (decompression) */
void Init_Expand(void)                                {}
