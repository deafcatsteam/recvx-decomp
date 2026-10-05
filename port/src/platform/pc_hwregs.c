/* Memory standing in for the PS2 hardware registers (see sdk/eeregs.h). */
unsigned char port_hwregs[0x20000];

/* The EE's 16 KB scratchpad RAM (0x70000000 on the PS2). */
unsigned char port_scratchpad[0x4000] __attribute__((aligned(64)));
