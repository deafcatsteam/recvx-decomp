#include "../../../ps2/veronica/prog/binfunc.h"
#ifdef RECVX_PC_PORT
#include <stdint.h>  /* uintptr_t — see RX_PTRADD note below */
#include <stdlib.h>  /* calloc, free — used by port bhMlbBinRealize */
#include <string.h>  /* memcpy — used by port ps2_u32/etc. */
/* Explicit prototypes in case the PS2-SDK include chain shadows
 * <stdlib.h>/<string.h>. Without these MSVC defaults the return type
 * to `int`, which makes `(NJS_CNK_OBJECT*)calloc(...)` truncate the
 * upper 32 bits of the returned 64-bit pointer — the exact bug
 * pattern we're fixing in this file. */
extern void* calloc(size_t num, size_t size);
extern void  free(void* p);
extern void* memcpy(void* dst, const void* src, size_t n);
/* RX_PTRADD: 64-bit-safe pointer + offset for the x64 PC port.
 *
 * The upstream decomp uses `(int)ptr + offset` to apply blob-internal
 * relocation offsets after a model file is loaded. On PS2 (32-bit
 * addresses) that's correct. On x64 the cast to `int` truncates the
 * upper 32 bits of the base pointer, so every "fixed-up" pointer in
 * the loaded blob ends up in a wild low-2GiB address. The walker then
 * AVs the first time it dereferences a child/sibling/model/vlist
 * field. Crash is silent (KiUserExceptionDispatcher hits before
 * SetUnhandledExceptionFilter) because the AV often targets
 * userland-shaped memory rather than NULL.
 *
 * Fix: route all `(int)ptr + offset` arithmetic through uintptr_t so
 * the addition happens in the full pointer width. Kept under #ifdef
 * so the upstream "100% matching" verification still passes on PS2. */
#define RX_PTRADD(p, off)  ((void*)((uintptr_t)(p) + (uintptr_t)(off)))
#define RX_PTRINT          uintptr_t
#else
#define RX_PTRADD(p, off)  ((void*)((int)(p) + (int)(off)))
#define RX_PTRINT          unsigned int
#endif

#ifdef RECVX_PC_PORT
/* Port-side bhMlbBinRealize.
 *
 * The PS2 path does in-place pointer fix-up: it reads/writes the loaded
 * blob using NJS_CNK_OBJECT struct accesses, treating each 48-byte
 * serialized entry as a 48-byte runtime entry. That works on PS2
 * because pointers are 4 bytes; on x64 they're 8, so the runtime
 * NJS_CNK_OBJECT is 64 bytes with completely shifted field offsets
 * (model at runtime offset 8 vs. serialized offset 4, etc.). In-place
 * fix-up is impossible — the realize would read pos[0] as a model
 * pointer and produce non-canonical addresses.
 *
 * Port strategy: parse PS2 layout manually from raw bytes, allocate
 * fresh x64-native NJS_CNK_OBJECT / NJS_CNK_MODEL / NJS_TEXLIST
 * structs, and populate them with proper 64-bit pointers. The blob
 * itself stays untouched (we still hand vlist/plist raw blob offsets
 * to downstream code, which reads them as byte streams). Allocation
 * uses calloc — small leak per examine session, acceptable.
 *
 * PS2 layouts (must match exactly).
 *
 * Critical typedef: KATANA `Angle` = `Sint32` (32-bit), NOT short.
 * That makes ang[3] 12 bytes not 6, and shifts every later field.
 *
 *   NJS_CNK_OBJECT (52 bytes):
 *     0   evalflags  uint32
 *     4   model      uint32 (blob offset, -1 = none)
 *     8   pos[0..2]  3 floats     (12 bytes)
 *    20   ang[0..2]  3 Sint32     (12 bytes)
 *    32   scl[0..2]  3 floats     (12 bytes)
 *    44   child      uint32 (offset, -1 = none)
 *    48   sibling    uint32 (offset, -1 = none)
 *
 *   NJS_CNK_MODEL  (24 bytes):
 *     0   vlist      uint32 (offset, -1 = none)
 *     4   plist      uint32 (offset, -1 = none)
 *     8   center     3 floats
 *    20   r          float
 *
 *   NJS_TEXLIST     (8 bytes):
 *     0   textures   uint32 (offset)
 *     4   nbTexture  uint32
 *
 *   NJS_TEXNAME    (12 bytes):
 *     0   filename   uint32 (offset)
 *     4   attr       uint32
 *     8   texaddr    uint32
 */
#define PS2_CNK_OBJ_SIZE   52  /* Angle = Sint32, NOT short — ang[3]=12 bytes */
#define PS2_CNK_MDL_SIZE   24
#define PS2_TEXLIST_SIZE    8
#define PS2_TEXNAME_SIZE   12

/* Read a uint32 from a PS2-layout struct at given byte offset. */
static unsigned int ps2_u32(const unsigned char* p, unsigned o)
{
    unsigned int v;
    memcpy(&v, p + o, 4);
    return v;
}
static float ps2_f32(const unsigned char* p, unsigned o)
{
    float v;
    memcpy(&v, p + o, 4);
    return v;
}
static int ps2_s32(const unsigned char* p, unsigned o)
{
    int v;
    memcpy(&v, p + o, 4);
    return v;
}

/* Parse one PS2 NJS_CNK_MODEL block at `pm` (bytes), produce an x64
 * NJS_CNK_MODEL using `dat_top` as base for vlist/plist relocation. */
static NJS_CNK_MODEL* port_parse_cnk_model(const unsigned char* pm,
                                           unsigned char* dat_top)
{
    NJS_CNK_MODEL* m = (NJS_CNK_MODEL*)calloc(1, sizeof(NJS_CNK_MODEL));
    if (!m) return NULL;

    unsigned int vlist_off = ps2_u32(pm,  0);
    unsigned int plist_off = ps2_u32(pm,  4);
    m->center.x            = ps2_f32(pm,  8);
    m->center.y            = ps2_f32(pm, 12);
    m->center.z            = ps2_f32(pm, 16);
    m->r                   = ps2_f32(pm, 20);

    m->vlist = (vlist_off == 0xFFFFFFFFu) ? NULL
                                          : (Sint32*)(dat_top + vlist_off);
    m->plist = (plist_off == 0xFFFFFFFFu) ? NULL
                                          : (Sint16*)(dat_top + plist_off);
    return m;
}

int bhMlbBinRealize(void* bin_datP, ML_WORK* mlwP)
{
    unsigned char* base = (unsigned char*)bin_datP;

    /* Header fields (read from very start of blob). */
    unsigned int status        = ((unsigned char*)base)[3];
    unsigned int obj_num       = ((unsigned short*)base)[3];
    unsigned int tex_off       = (unsigned int)((int*)base)[2];
    unsigned int obj_off       = (unsigned int)((int*)base)[3];
    unsigned int dat_start_off = ((unsigned short*)base)[2];

    mlwP->flg     = 0;
    mlwP->obj_num = 0;
    mlwP->datP    = base;
    mlwP->objP    = NULL;
    mlwP->texP    = NULL;
    mlwP->owP     = NULL;

    /* dat_top = base of the data block referenced by all internal
     * offsets (objects, models, textures, vlist/plist payloads). */
    unsigned char* dat_top = base + dat_start_off;

    if (obj_off != 0xFFFFFFFFu)
    {
        mlwP->flg     = status;
        mlwP->obj_num = obj_num;

        /* Allocate a fresh x64-native object array. */
        NJS_CNK_OBJECT* objs = (NJS_CNK_OBJECT*)calloc(obj_num,
                                                       sizeof(NJS_CNK_OBJECT));
        if (!objs) return 0;
        mlwP->objP = objs;

        const unsigned char* ps2_objs = dat_top + obj_off;

        /* First pass: populate non-pointer fields and resolve models.
         * Stash raw child/sibling offsets in a side array for pass two. */
        unsigned int* child_offs   = (unsigned int*)calloc(obj_num, 4);
        unsigned int* sibling_offs = (unsigned int*)calloc(obj_num, 4);
        if (!child_offs || !sibling_offs) return 0;

        for (unsigned int i = 0; i < obj_num; ++i)
        {
            const unsigned char* po = ps2_objs + i * PS2_CNK_OBJ_SIZE;
            NJS_CNK_OBJECT*       o = &objs[i];

            o->evalflags = ps2_u32(po,  0);
            unsigned int model_off = ps2_u32(po,  4);
            o->pos[0]    = ps2_f32(po,  8);
            o->pos[1]    = ps2_f32(po, 12);
            o->pos[2]    = ps2_f32(po, 16);
            /* Angle = Sint32, 4 bytes each — three full ints. */
            o->ang[0]    = ps2_s32(po, 20);
            o->ang[1]    = ps2_s32(po, 24);
            o->ang[2]    = ps2_s32(po, 28);
            o->scl[0]    = ps2_f32(po, 32);
            o->scl[1]    = ps2_f32(po, 36);
            o->scl[2]    = ps2_f32(po, 40);
            child_offs[i]   = ps2_u32(po, 44);
            sibling_offs[i] = ps2_u32(po, 48);

            if (model_off == 0xFFFFFFFFu)
            {
                o->model = NULL;
            }
            else
            {
                o->model = port_parse_cnk_model(dat_top + model_off, dat_top);
                /* status & 0x80 distinguishes chunked vs basic model
                 * in the PS2 path. We only support chunked for now;
                 * basic-model items (NJS_MODEL) would crash here.
                 * Phase 3b will add the basic path. */
            }
        }

        /* Second pass: resolve child/sibling offsets into x64 pointers.
         * An offset Z names the byte position (relative to dat_top) of
         * the target object's serialized record. Target index =
         * (Z - obj_off) / 48. */
        for (unsigned int i = 0; i < obj_num; ++i)
        {
            NJS_CNK_OBJECT* o = &objs[i];

            if (child_offs[i] == 0xFFFFFFFFu) {
                o->child = NULL;
            } else {
                unsigned int idx = (child_offs[i] - obj_off) / PS2_CNK_OBJ_SIZE;
                o->child = (idx < obj_num) ? &objs[idx] : NULL;
            }

            if (sibling_offs[i] == 0xFFFFFFFFu) {
                o->sibling = NULL;
            } else {
                unsigned int idx = (sibling_offs[i] - obj_off) / PS2_CNK_OBJ_SIZE;
                o->sibling = (idx < obj_num) ? &objs[idx] : NULL;
            }
        }

        free(child_offs);
        free(sibling_offs);
    }

    if (tex_off != 0xFFFFFFFFu)
    {
        /* Parse NJS_TEXLIST + its NJS_TEXNAME array. */
        const unsigned char* ptl = dat_top + tex_off;
        unsigned int textures_off = ps2_u32(ptl, 0);
        unsigned int nbTexture    = ps2_u32(ptl, 4);

        NJS_TEXLIST* tl = (NJS_TEXLIST*)calloc(1, sizeof(NJS_TEXLIST));
        if (!tl) return 0;
        tl->nbTexture = nbTexture;

        if (nbTexture > 0)
        {
            tl->textures = (NJS_TEXNAME*)calloc(nbTexture, sizeof(NJS_TEXNAME));
            if (!tl->textures) return 0;

            const unsigned char* ptn = dat_top + textures_off;
            for (unsigned int i = 0; i < nbTexture; ++i)
            {
                const unsigned char* pn = ptn + i * PS2_TEXNAME_SIZE;
                unsigned int filename_off = ps2_u32(pn,  0);
                tl->textures[i].attr      = ps2_u32(pn,  4);
                tl->textures[i].texaddr   = ps2_u32(pn,  8);
                tl->textures[i].filename  = (void*)(dat_top + filename_off);
            }
        }

        mlwP->texP = tl;
    }

    return 1;
}
#else
// 100% matching!
int bhMlbBinRealize(void* bin_datP, ML_WORK* mlwP)
{
    unsigned int obj_num;
    unsigned int tex_off;
    unsigned int obj_off;
    unsigned int status;
    NJS_CNK_OBJECT* objP;
    NJS_TEXNAME* namP;
    unsigned int tex_num;

    status = ((char*)bin_datP)[3];

    obj_num = ((unsigned short*)bin_datP)[3];

    tex_off = ((int*)bin_datP)[2];
    obj_off = ((int*)bin_datP)[3];

    mlwP->flg = 0;

    mlwP->obj_num = 0;

    mlwP->datP = bin_datP;
    mlwP->objP = NULL;
    mlwP->texP = NULL;
    mlwP->owP = NULL;

    bin_datP = (void*)((char*)bin_datP + ((unsigned short*)bin_datP)[2]);

    if (obj_off != -1)
    {
        mlwP->flg = status;

        objP = (NJS_CNK_OBJECT*)((int)bin_datP + obj_off);

        mlwP->obj_num = obj_num;
        mlwP->objP = objP;

        for ( ; obj_num != 0; obj_num--, objP++)
        {
            if (objP->child != (void*)-1)
            {
                objP->child = (void*)((int)objP->child + (int)bin_datP);
            }
            else
            {
                objP->child = NULL;
            }

            if (objP->sibling != (void*)-1)
            {
                objP->sibling = (void*)((int)objP->sibling + (int)bin_datP);
            }
            else
            {
                objP->sibling = NULL;
            }

            if (objP->model != (void*)-1)
            {
                objP->model = (void*)((int)objP->model + (int)bin_datP);

                if (!(status & 0x80))
                {
                    bhBscBinRealize((NJS_MODEL*)objP->model, (unsigned int)bin_datP);
                }
                else
                {
                    bhCnkBinRealize(objP->model, (unsigned int)bin_datP);
                }
            }
            else
            {
                objP->model = NULL;
            }
        }
    }

    if (tex_off != -1)
    {
        mlwP->texP = (void*)((int)bin_datP + tex_off);

        mlwP->texP->textures = (NJS_TEXNAME*)((char*)mlwP->texP->textures + (int)bin_datP);

        namP = mlwP->texP->textures;

        for (tex_num = mlwP->texP->nbTexture; tex_num != 0; tex_num--, namP++)
        {
            namP->filename = (void*)((char*)namP->filename + (int)bin_datP);
        }
    }

    return 1;
}
#endif

// 100% matching!
int bhBscBinRealize(NJS_MODEL* mdlP, BH_DATOFF_T dat_off)
{
    NJS_MESHSET* mshP;
    unsigned int i;

    if ((mdlP->nbPoint & 0x80000000))
    {
        mdlP->nbPoint &= ~0x80000000;

        /* The original sentinel test `(int)ptr != -1` checks for a
         * sign-extended 0xFFFFFFFF. On x64 a fixed-up pointer can never
         * BE that value (it lives in the upper half of the address
         * space), so the comparison must be done in 32 bits even after
         * we widen the addition — keep the (int) cast for the test,
         * use uintptr_t arithmetic for the actual fix-up. */
        if ((int)(intptr_t)mdlP->points   != -1) mdlP->points   = (NJS_POINT3*)RX_PTRADD(mdlP->points, dat_off);
        if ((int)(intptr_t)mdlP->normals  != -1) mdlP->normals  = (NJS_VECTOR*)RX_PTRADD(mdlP->normals, dat_off);
        if ((int)(intptr_t)mdlP->meshsets != -1) mshP = mdlP->meshsets = (NJS_MESHSET*)RX_PTRADD(mdlP->meshsets, dat_off);
        if ((int)(intptr_t)mdlP->mats     != -1) mdlP->mats     = (NJS_MATERIAL*)RX_PTRADD(mdlP->mats, dat_off);

        for (i = 0; i < mdlP->nbMeshset; i++, mshP++)
        {
            if ((int)(intptr_t)mshP->meshes != -1)
                mshP->meshes = (short*)RX_PTRADD(mshP->meshes, dat_off);

            if ((int)(intptr_t)mshP->attrs != -1)
                mshP->attrs = (unsigned int*)RX_PTRADD(mshP->attrs, dat_off);
            else
                mshP->attrs = NULL;

            if ((int)(intptr_t)mshP->normals != -1)
                mshP->normals = (NJS_VECTOR*)RX_PTRADD(mshP->normals, dat_off);
            else
                mshP->normals = NULL;

            if ((int)(intptr_t)mshP->vertcolor != -1)
                mshP->vertcolor = (NJS_COLOR*)RX_PTRADD(mshP->vertcolor, dat_off);
            else
                mshP->vertcolor = NULL;

            if ((int)(intptr_t)mshP->vertuv != -1)
                mshP->vertuv = (NJS_COLOR*)RX_PTRADD(mshP->vertuv, dat_off);
            else
                mshP->vertuv = NULL;
        }
    }

    return 1;
}

// 100% matching!
int bhCnkBinRealize(NJS_CNK_MODEL* mdlP, BH_DATOFF_T dat_off)
{
    if (mdlP->r < 0)
    {
        mdlP->r = -mdlP->r;

        if ((int)(intptr_t)mdlP->vlist != -1)
            mdlP->vlist = (int*)RX_PTRADD(mdlP->vlist, dat_off);

        if ((int)(intptr_t)mdlP->plist != -1)
            mdlP->plist = (short*)RX_PTRADD(mdlP->plist, dat_off);
    }

    return 1;
}

// 100% matching!
int bhMnbBinRealize(void* bin_datP, MN_WORK* mnwP)
{
    NJS_MOTION* mtnP;
    NJS_MDATA2_MOD* md2P;
    void* dat_topP;
    int i;

    dat_topP = (void*)((char*)bin_datP + ((unsigned short*)bin_datP)[2]);
    mtnP = (NJS_MOTION*)RX_PTRADD(dat_topP, ((int*)bin_datP)[2]);
    md2P = (NJS_MDATA2_MOD*)RX_PTRADD(mtnP->mdata, dat_topP);

    mtnP->mdata = md2P;

    mnwP->flg = ((char*)bin_datP)[3];

    mnwP->obj_num = ((unsigned short*)bin_datP)[3];
    mnwP->frm_num = mtnP->nbFrame;

    mnwP->datP = bin_datP;
    mnwP->md2P = (NJS_MDATA2*)md2P;
    mnwP->atrP = NULL;

    if (((int*)bin_datP)[3] != -1)
    {
        mnwP->atrP = (unsigned short*)RX_PTRADD(dat_topP, ((int*)bin_datP)[3]);
    }

    for (i = 0; i < ((unsigned short*)bin_datP)[3]; i++, md2P++)
    {
        if (md2P->p[0] == (void*)-1)
            md2P->p[0] = NULL;
        else
            md2P->p[0] = RX_PTRADD(md2P->p[0], dat_topP);

        if (md2P->p[1] == (void*)-1)
            md2P->p[1] = NULL;
        else
            md2P->p[1] = RX_PTRADD(md2P->p[1], dat_topP);
    }

    return 1;
}
