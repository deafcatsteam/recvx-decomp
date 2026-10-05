/*
 * Entry point of the PC port. Prepares the process the way the game expects
 * a PS2 to be, then runs the game's own main() (njloop.c, renamed cvx_main
 * by the build).
 */
#ifdef _WIN32
#include <windows.h>
#else
#define _GNU_SOURCE
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <stdint.h>
#include <stdio.h>

#include "pc_window.h"
#include "../host/pc_host.h"

int cvx_main(int argc, char *argv[]);

/*
 * The PS2 has no memory protection and the game writes into data the PC
 * toolchain puts in read-only pages (string literals, for instance the disc
 * file names it upper-cases in place). Make the executable's image writable.
 */
#ifdef _WIN32

static void unprotect_image(void)
{
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);

    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        DWORD old;
        DWORD prot = (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) ? PAGE_EXECUTE_READWRITE
                                                                    : PAGE_READWRITE;
        VirtualProtect(base + sec->VirtualAddress, sec->Misc.VirtualSize, prot, &old);
    }
}

#else

static int unprotect_segment(struct dl_phdr_info *info, size_t size, void *data)
{
    long page = sysconf(_SC_PAGESIZE);

    (void)size;
    (void)data;
    if (info->dlpi_name != NULL && info->dlpi_name[0] != '\0')
        return 1; /* only the first entry is the executable itself */

    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD)
            continue;
        uintptr_t start = (info->dlpi_addr + ph->p_vaddr) & ~(uintptr_t)(page - 1);
        uintptr_t end = info->dlpi_addr + ph->p_vaddr + ph->p_memsz;
        int prot = PROT_READ | PROT_WRITE | ((ph->p_flags & PF_X) ? PROT_EXEC : 0);
        if (mprotect((void *)start, end - start, prot) != 0)
            perror("mprotect");
    }
    return 1;
}

static void unprotect_image(void)
{
    dl_iterate_phdr(unprotect_segment, NULL);
}

#endif

int main(int argc, char *argv[])
{
    /* Keep the game's log lines in order with ours, even when redirected. */
    setvbuf(stdout, NULL, _IONBF, 0);
    pc_log_start();

    unprotect_image();
    pc_window_open();
    pc_diag_start();

    return cvx_main(argc, argv);
}
