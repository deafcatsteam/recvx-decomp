/*
 * tex_pool_alloc.c — low-4GiB raw allocator for the NJS_TEXMEMLIST pool.
 *
 * Lives in recvx_port_stubs (not recvx_game) so it can freely include
 * <windows.h> without colliding with types.h's POINT/RECT/etc. structs.
 * game_texture_stubs.c calls recvx_alloc_low4g() via an extern decl.
 *
 * Why low-4GiB: NJS_TEXNAME.texaddr is a Uint32 (4 bytes). On PS2 that
 * holds a real VRAM pointer; on x64 it can't hold an 8-byte heap ptr.
 * We pin the pool below 4 GiB so truncation to Uint32 is lossless.
 */

#include <stddef.h>
#include <stdint.h>

extern void recvx_log(const char* tag, const char* fmt, ...);
#define RX_LOG(tag, ...) recvx_log(tag, __VA_ARGS__)

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

/* VirtualAlloc2 lives in kernelbase.dll but its import symbol is in
 * mincore.lib / onecore.lib — NOT kernel32.lib. Resolve at runtime via
 * GetProcAddress so we stay linked only against kernel32 + CRT. */
typedef PVOID (WINAPI *PFN_VirtualAlloc2)(
    HANDLE Process, PVOID BaseAddress, SIZE_T Size,
    ULONG AllocationType, ULONG PageProtection,
    MEM_EXTENDED_PARAMETER* ExtendedParameters, ULONG ParameterCount);

void* recvx_alloc_low4g(size_t bytes) {
    HMODULE k32 = GetModuleHandleA("kernelbase.dll");
    if (!k32) k32 = GetModuleHandleA("kernel32.dll");
    PFN_VirtualAlloc2 pVirtualAlloc2 = k32
        ? (PFN_VirtualAlloc2)GetProcAddress(k32, "VirtualAlloc2")
        : NULL;

    void* p = NULL;
    if (pVirtualAlloc2) {
        MEM_ADDRESS_REQUIREMENTS req = {0};
        req.LowestStartingAddress = (PVOID)(uintptr_t)0x10000;
        req.HighestEndingAddress  = (PVOID)(uintptr_t)0xFFFFFFFEULL;
        req.Alignment             = 0;
        MEM_EXTENDED_PARAMETER param = {0};
        param.Type    = MemExtendedParameterAddressRequirements;
        param.Pointer = &req;
        p = pVirtualAlloc2(GetCurrentProcess(), NULL, bytes,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE,
                           &param, 1);
        if (!p) {
            RX_LOG("tex", "VirtualAlloc2 low-4g failed (err=%lu)",
                   GetLastError());
        }
    }

    /* Fallback: scan standard low-memory base addresses. On x64 ASLR the
     * image base + heap usually start above 4 GiB, so the first few
     * 256 MiB slots below 2 GiB are almost always free. */
    if (!p) {
        static const uintptr_t candidates[] = {
            0x10000000, 0x20000000, 0x30000000, 0x40000000,
            0x50000000, 0x60000000, 0x70000000,
        };
        for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); ++i) {
            p = VirtualAlloc((void*)candidates[i], bytes,
                             MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (p) { RX_LOG("tex", "fallback base=0x%p", p); break; }
        }
    }
    return p;
}

#else
#include <sys/mman.h>

void* recvx_alloc_low4g(size_t bytes) {
#ifdef MAP_32BIT
    /* Linux-only flag: restricts the mapping to the first 2 GiB of the
     * address space, comfortably under the 4 GiB Uint32 texaddr limit. */
    void* p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p != MAP_FAILED) {
        RX_LOG("tex", "mmap MAP_32BIT base=%p", p);
        return p;
    }
    RX_LOG("tex", "mmap MAP_32BIT failed, trying fixed low addresses");
#endif
    /* Fallback: hint a fixed low address. MAP_FIXED_NOREPLACE (Linux 4.17+)
     * fails cleanly instead of silently mapping elsewhere if the address
     * is already used, mirroring the Windows VirtualAlloc-at-candidate loop. */
    static const uintptr_t candidates[] = {
        0x10000000, 0x20000000, 0x30000000, 0x40000000,
        0x50000000, 0x60000000, 0x70000000,
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        void* hint = (void*)candidates[i];
        void* p = mmap(hint, bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                        -1, 0);
        if (p != MAP_FAILED) {
            RX_LOG("tex", "mmap fixed base=%p", p);
            return p;
        }
    }
    RX_LOG("tex", "all low-4g mmap attempts failed");
    return NULL;
}
#endif
