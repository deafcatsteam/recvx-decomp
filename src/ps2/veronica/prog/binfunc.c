#include "../../../ps2/veronica/prog/binfunc.h"
#ifdef RECVX_PC_PORT
#include <stdint.h>  /* uintptr_t — see RX_PTRADD note below */
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

        objP = (NJS_CNK_OBJECT*)RX_PTRADD(bin_datP, obj_off);

        mlwP->obj_num = obj_num;
        mlwP->objP = objP;

        for ( ; obj_num != 0; obj_num--, objP++)
        {
            if (objP->child != (void*)-1)
            {
                objP->child = (struct cnkobj*)RX_PTRADD(objP->child, bin_datP);
            }
            else
            {
                objP->child = NULL;
            }

            if (objP->sibling != (void*)-1)
            {
                objP->sibling = (struct cnkobj*)RX_PTRADD(objP->sibling, bin_datP);
            }
            else
            {
                objP->sibling = NULL;
            }

            if (objP->model != (void*)-1)
            {
                objP->model = (NJS_CNK_MODEL*)RX_PTRADD(objP->model, bin_datP);

                if (!(status & 0x80))
                {
                    bhBscBinRealize((NJS_MODEL*)objP->model, (RX_PTRINT)bin_datP);
                }
                else
                {
                    bhCnkBinRealize(objP->model, (RX_PTRINT)bin_datP);
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
        mlwP->texP = (NJS_TEXLIST*)RX_PTRADD(bin_datP, tex_off);

        mlwP->texP->textures = (NJS_TEXNAME*)RX_PTRADD(mlwP->texP->textures, bin_datP);

        namP = mlwP->texP->textures;

        for (tex_num = mlwP->texP->nbTexture; tex_num != 0; tex_num--, namP++)
        {
            namP->filename = (char*)RX_PTRADD(namP->filename, bin_datP);
        }
    }

    return 1;
}

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
