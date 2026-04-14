/*
 * sg_* shim — Sega Ginsu helpers used by the game's higher-level wrappers.
 * Phase 0 no-ops; filled in as phases touch each subsystem.
 */

#include "recvx_port.h"

/* pad */
void pdSetMode(int m)                                 { (void)m; }

/* print/font */
void npSetMemory(void* p, unsigned int sz, int flag)  { (void)p; (void)sz; (void)flag; }
void npPlusInit(void)                                 {}

/* backup ram */
void BupInit(void)                                    {}
void BupExit(void)                                    {}

/* sound */
void InitSoundProgram(int mode)                       { (void)mode; }
void ExitSoundProgram(void)                           {}

/* expand (decompression) */
void Init_Expand(void)                                {}
