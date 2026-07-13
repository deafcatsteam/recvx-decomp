/*
 * Ninja-plus capsule/sphere collision math — extracted from
 * src/ps2/veronica/prog/njplus.c, which as a whole is saturated with
 * VU0 inline asm (12 asm blocks starting at line 754, mostly skinning/
 * matrix-stack helpers unrelated to these functions). The functions
 * below are all "100% matching!" pure C in the original file and only
 * call already-real code: njCollisionCheckSS/njDistanceL2L/
 * njDistanceP2L (ps2_NaColi.c) and njInnerProduct/njScalor/
 * njUnitVector (ninja_3d.c).
 *
 * npCollisionCheckCC/CCEx/SC and npDistanceP2C: verbatim copy of
 * njplus.c lines 33-631 (no asm before line 754). Needed by weapon.c's
 * bhCheckGunAtari/bhCheckKnifeAtari/bhCheckFlyAtari/bhCheckBombAtari/
 * bhCheckCapCol2Capsule.
 *
 * npSetAllMatColor: verbatim copy of njplus.c lines 1375-1469, needed
 * by en01.c.
 *
 * npCopyVlist: verbatim copy of njplus.c lines 1722-1738, needed by
 * effsub1b.c (Task 4.2). Source comments/formatting preserved from the
 * decomp.
 */

#include "njplus.h"
#include "ps2_NaColi.h"

// 100% matching!
int npCollisionCheckCC(NJS_CAPSULE* cpa, NJS_CAPSULE* cpb)
{
    NJS_LINE lna;
    NJS_LINE lnb;
    NJS_LINE lnc;
    NJS_CAPSULE* capa;
    NJS_CAPSULE* capb;
    NJS_SPHERE sa;
    NJS_SPHERE sb;
    NJS_VECTOR vec;
    float inn;
    float lena;
    float lenb;
    float sca;
    float temp; // not from DWARF

    lna.px = cpa->c1.x;
    lna.py = cpa->c1.y;
    lna.pz = cpa->c1.z;

    capb = cpa;

    lna.vx = cpa->c2.x - lna.px;
    lna.vy = cpa->c2.y - lna.py;
    lna.vz = cpa->c2.z - lna.pz;

    lnb.px = cpb->c1.x;
    lnb.py = cpb->c1.y;
    lnb.pz = cpb->c1.z;

    lnb.vx = cpb->c2.x - lnb.px;
    lnb.vy = cpb->c2.y - lnb.py;
    lnb.vz = cpb->c2.z - lnb.pz;

    lena = njScalor((NJS_VECTOR*)&lna.vx);
    lenb = njScalor((NJS_VECTOR*)&lnb.vx);

    if (lena > lenb)
    {
        capa = cpb;
        cpb = cpa;
        capb = cpb;

        lnc = lna;
        lna = lnb;
        lnb = lnc;

        temp = lena;
        lena = lenb;
        lenb = temp;
    }
    else
    {
        capa = cpa;
        capb = cpb;
    }

    njDistanceL2L(&lna, &lnb, (NJS_POINT3*)&sa.c, (NJS_POINT3*)&sb.c);

    sa.r = capa->r;
    sb.r = capb->r;

    vec.x = sa.c.x - lna.px;
    vec.y = sa.c.y - lna.py;
    vec.z = sa.c.z - lna.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);
    njUnitVector((NJS_VECTOR*)&lna.vx);

    inn = njInnerProduct(&vec, (NJS_VECTOR*)&lna.vx);

    if (inn > 0)
    {
        if (sca > lena)
        {
            sa.c.x = capa->c2.x;
            sa.c.y = capa->c2.y;
            sa.c.z = capa->c2.z;
        }
    }
    else
    {
        sa.c.x = capa->c1.x;
        sa.c.y = capa->c1.y;
        sa.c.z = capa->c1.z;
    }

    vec.x = sb.c.x - lnb.px;
    vec.y = sb.c.y - lnb.py;
    vec.z = sb.c.z - lnb.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);
    njUnitVector((NJS_VECTOR*)&lnb.vx);

    inn = njInnerProduct((NJS_VECTOR*)&vec,(NJS_VECTOR*)&lnb.vx);

    if (inn > 0)
    {
        if (sca > lenb)
        {
            sb.c.x = capb->c2.x;
            sb.c.y = capb->c2.y;
            sb.c.z = capb->c2.z;
        }
    }
    else
    {
        sb.c.x = capb->c1.x;
        sb.c.y = capb->c1.y;
        sb.c.z = capb->c1.z;
    }

    if (njCollisionCheckSS((NJS_SPHERE*)&sa, (NJS_SPHERE*)&sb) != 0)
    {
        return 1;
    }

    sa.c.x = capa->c1.x;
    sa.c.y = capa->c1.y;
    sa.c.z = capa->c1.z;

    njDistanceP2L(&sa.c, &lnb, (NJS_POINT3*)&sb.c);

    vec.x = sb.c.x - lnb.px;
    vec.y = sb.c.y - lnb.py;
    vec.z = sb.c.z - lnb.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);

    inn = njInnerProduct(&vec, (NJS_VECTOR*)&lnb.vx);

    if (inn > 0)
    {
        if (sca > lenb)
        {
            sb.c.x = capb->c2.x;
            sb.c.y = capb->c2.y;
            sb.c.z = capb->c2.z;
        }
    }
    else
    {
        sb.c.x = capb->c1.x;
        sb.c.y = capb->c1.y;
        sb.c.z = capb->c1.z;
    }

    if (njCollisionCheckSS(&sa, &sb) != 0)
    {
        return 1;
    }

    sa.c.x = capa->c2.x;
    sa.c.y = capa->c2.y;
    sa.c.z = capa->c2.z;

    njDistanceP2L(&sa.c, &lnb, (NJS_POINT3*)&sb.c);

    vec.x = sb.c.x - lnb.px;
    vec.y = sb.c.y - lnb.py;
    vec.z = sb.c.z - lnb.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);

    inn = njInnerProduct(&vec, (NJS_VECTOR*)&lnb.vx);

    if (inn > 0)
    {
        if (sca > lenb)
        {
            sb.c.x = capb->c2.x;
            sb.c.y = capb->c2.y;
            sb.c.z = capb->c2.z;
        }
    }
    else
    {
        sb.c.x = capb->c1.x;
        sb.c.y = capb->c1.y;
        sb.c.z = capb->c1.z;
    }

    if (njCollisionCheckSS(&sa, &sb) != 0)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

// 100% matching!
int npCollisionCheckCCEx(NJS_CAPSULE* cpa, NJS_CAPSULE* cpb, NJS_POINT3* pos)
{
    NJS_LINE lna;
    NJS_LINE lnb;
    NJS_LINE lnc;
    NJS_CAPSULE* capa;
    NJS_CAPSULE* capb;
    NJS_SPHERE sa;
    NJS_SPHERE sb;
    NJS_VECTOR vec;
    float inn;
    float lena;
    float lenb;
    float sca;
    float temp; // not from DWARF

    lna.px = cpa->c1.x;
    lna.py = cpa->c1.y;
    lna.pz = cpa->c1.z;

    capb = cpa;

    lna.vx = cpa->c2.x - lna.px;
    lna.vy = cpa->c2.y - lna.py;
    lna.vz = cpa->c2.z - lna.pz;

    lnb.px = cpb->c1.x;
    lnb.py = cpb->c1.y;
    lnb.pz = cpb->c1.z;

    lnb.vx = cpb->c2.x - lnb.px;
    lnb.vy = cpb->c2.y - lnb.py;
    lnb.vz = cpb->c2.z - lnb.pz;

    lena = njScalor((NJS_VECTOR*)&lna.vx);
    lenb = njScalor((NJS_VECTOR*)&lnb.vx);

    if (lena > lenb)
    {
        capa = cpb;
        cpb = cpa;
        capb = cpb;

        lnc = lna;
        lna = lnb;
        lnb = lnc;

        temp = lena;
        lena = lenb;
        lenb = temp;
    }
    else
    {
        capa = cpa;
        capb = cpb;
    }

    njDistanceL2L(&lna, &lnb, (NJS_POINT3*)&sa.c, (NJS_POINT3*)&sb.c);

    sa.r = capa->r;
    sb.r = capb->r;

    vec.x = sa.c.x - lna.px;
    vec.y = sa.c.y - lna.py;
    vec.z = sa.c.z - lna.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);
    njUnitVector((NJS_VECTOR*)&lna.vx);

    inn = njInnerProduct(&vec, (NJS_VECTOR*)&lna.vx);

    if (inn > 0)
    {
        if (sca > lena)
        {
            sa.c.x = capa->c2.x;
            sa.c.y = capa->c2.y;
            sa.c.z = capa->c2.z;
        }
    }
    else
    {
        sa.c.x = capa->c1.x;
        sa.c.y = capa->c1.y;
        sa.c.z = capa->c1.z;
    }

    vec.x = sb.c.x - lnb.px;
    vec.y = sb.c.y - lnb.py;
    vec.z = sb.c.z - lnb.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);
    njUnitVector((NJS_VECTOR*)&lnb.vx);

    inn = njInnerProduct((NJS_VECTOR*)&vec,(NJS_VECTOR*)&lnb.vx);

    if (inn > 0)
    {
        if (sca > lenb)
        {
            sb.c.x = capb->c2.x;
            sb.c.y = capb->c2.y;
            sb.c.z = capb->c2.z;
        }
    }
    else
    {
        sb.c.x = capb->c1.x;
        sb.c.y = capb->c1.y;
        sb.c.z = capb->c1.z;
    }

    if (njCollisionCheckSS((NJS_SPHERE*)&sa, (NJS_SPHERE*)&sb) != 0)
    {
        NJS_POINT3 scl;

        scl.x = sb.c.x - sa.c.x;
        scl.y = sb.c.y - sa.c.y;
        scl.z = sb.c.z - sa.c.z;

        if (njScalor(&scl) > sa.r)
        {
            njUnitVector(&scl);

            pos->x = sa.c.x + (scl.x * sa.r);
            pos->y = sa.c.y + (scl.y * sa.r);
            pos->z = sa.c.z + (scl.z * sa.r);
        }
        else
        {
            pos->x = sb.c.x;
            pos->y = sb.c.y;
            pos->z = sb.c.z;
        }

        return 1;
    }

    sa.c.x = capa->c1.x;
    sa.c.y = capa->c1.y;
    sa.c.z = capa->c1.z;

    njDistanceP2L(&sa.c, &lnb, (NJS_POINT3*)&sb.c);

    vec.x = sb.c.x - lnb.px;
    vec.y = sb.c.y - lnb.py;
    vec.z = sb.c.z - lnb.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);

    inn = njInnerProduct(&vec, (NJS_VECTOR*)&lnb.vx);

    if (inn > 0)
    {
        if (sca > lenb)
        {
            sb.c.x = capb->c2.x;
            sb.c.y = capb->c2.y;
            sb.c.z = capb->c2.z;
        }
    }
    else
    {
        sb.c.x = capb->c1.x;
        sb.c.y = capb->c1.y;
        sb.c.z = capb->c1.z;
    }

    if (njCollisionCheckSS(&sa, &sb) != 0)
    {
        NJS_POINT3 scl;

        scl.x = sb.c.x - sa.c.x;
        scl.y = sb.c.y - sa.c.y;
        scl.z = sb.c.z - sa.c.z;

        if (njScalor(&scl) > sa.r)
        {
            njUnitVector(&scl);

            pos->x = sa.c.x + (scl.x * sa.r);
            pos->y = sa.c.y + (scl.y * sa.r);
            pos->z = sa.c.z + (scl.z * sa.r);
        }
        else
        {
            pos->x = sb.c.x;
            pos->y = sb.c.y;
            pos->z = sb.c.z;
        }

        return 1;
    }

    sa.c.x = capa->c2.x;
    sa.c.y = capa->c2.y;
    sa.c.z = capa->c2.z;

    njDistanceP2L(&sa.c, &lnb, (NJS_POINT3*)&sb.c);

    vec.x = sb.c.x - lnb.px;
    vec.y = sb.c.y - lnb.py;
    vec.z = sb.c.z - lnb.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);

    inn = njInnerProduct(&vec, (NJS_VECTOR*)&lnb.vx);

    if (inn > 0)
    {
        if (sca > lenb)
        {
            sb.c.x = capb->c2.x;
            sb.c.y = capb->c2.y;
            sb.c.z = capb->c2.z;
        }
    }
    else
    {
        sb.c.x = capb->c1.x;
        sb.c.y = capb->c1.y;
        sb.c.z = capb->c1.z;
    }

    if (njCollisionCheckSS(&sa, &sb) != 0)
    {
        NJS_POINT3 scl;

        scl.x = sb.c.x - sa.c.x;
        scl.y = sb.c.y - sa.c.y;
        scl.z = sb.c.z - sa.c.z;

        if (njScalor(&scl) > sa.r)
        {
            njUnitVector(&scl);

            pos->x = sa.c.x + (scl.x * sa.r);
            pos->y = sa.c.y + (scl.y * sa.r);
            pos->z = sa.c.z + (scl.z * sa.r);
        }
        else
        {
            pos->x = sb.c.x;
            pos->y = sb.c.y;
            pos->z = sb.c.z;
        }

        return 1;
    }
    else
    {
        return 0;
    }
}

// 100% matching!
int npCollisionCheckSC(NJS_SPHERE* sa, NJS_CAPSULE* cpb)
{
    NJS_LINE lnb;
    NJS_SPHERE sb;
    NJS_POINT3 vec;
    float inn;
    float lr;
    float lenb;
    float sca;

    lr = sa->r + cpb->r;

    lnb.px = cpb->c1.x;
    lnb.py = cpb->c1.y;
    lnb.pz = cpb->c1.z;

    lnb.vx = cpb->c2.x - lnb.px;
    lnb.vy = cpb->c2.y - lnb.py;
    lnb.vz = cpb->c2.z - lnb.pz;

    njDistanceP2L(&sa->c, &lnb, &sb.c);

    sb.r = cpb->r;

    lenb = njScalor((NJS_VECTOR*)&lnb.vx);

    vec.x = sb.c.x - lnb.px;
    vec.y = sb.c.y - lnb.py;
    vec.z = sb.c.z - lnb.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);
    njUnitVector((NJS_VECTOR*)&lnb.vx);

    inn = njInnerProduct(&vec, (NJS_VECTOR*)&lnb.vx);

    if (inn > 0)
    {
        if (sca > (lenb + lr))
        {
            return 0;
        }

        if (sca > lenb)
        {
            sb.c.x = cpb->c2.x;
            sb.c.y = cpb->c2.y;
            sb.c.z = cpb->c2.z;
        }
    }
    else
    {
        if (sca > lr)
        {
            return 0;
        }

        sb.c.x = cpb->c1.x;
        sb.c.y = cpb->c1.y;
        sb.c.z = cpb->c1.z;
    }

    return (njCollisionCheckSS(sa, &sb) != 0) ? 1 : 0;
}

// 100% matching!
void npDistanceP2C(NJS_POINT3* pos, NJS_CAPSULE* cap, NJS_POINT3* htp)
{
    NJS_LINE ln;
    NJS_VECTOR vec;
    float inn;
    float len;
    float sca;

    ln.px = cap->c1.x;
    ln.py = cap->c1.y;
    ln.pz = cap->c1.z;

    ln.vx = cap->c2.x - ln.px;
    ln.vy = cap->c2.y - ln.py;
    ln.vz = cap->c2.z - ln.pz;

    njDistanceP2L(pos, &ln, htp);

    len = njScalor((NJS_VECTOR*)&ln.vx);

    vec.x = htp->x - ln.px;
    vec.y = htp->y - ln.py;
    vec.z = htp->z - ln.pz;

    sca = njScalor(&vec);

    njUnitVector(&vec);
    njUnitVector((NJS_VECTOR*)&ln.vx);

    inn = njInnerProduct(&vec, (NJS_VECTOR*)&ln.vx);

    if (inn > 0)
    {
        if (sca > len)
        {
            htp->x = cap->c2.x;
            htp->y = cap->c2.y;
            htp->z = cap->c2.z;
        }
    }
    else
    {
        htp->x = cap->c1.x;
        htp->y = cap->c1.y;
        htp->z = cap->c1.z;
    }

    vec.x = pos->x - htp->x;
    vec.y = pos->y - htp->y;
    vec.z = pos->z - htp->z;

    if (cap->r < njScalor(&vec))
    {
        njUnitVector(&vec);

        htp->x += vec.x * cap->r;
        htp->y += vec.y * cap->r;
        htp->z += vec.z * cap->r;
    }
    else
    {
        htp->x = pos->x;
        htp->y = pos->y;
        htp->z = pos->z;
    }
}

/* Verbatim copy of njplus.c lines 1722-1738 (npCopyVlist) — also pure C
 * ("100% matching!" in the decomp), needed by effsub1b.c (Task 4.2).
 * Only calls njMemCopy4, already real (stub_game.c). */
// 100% matching!
int npCopyVlist(int* dstp, int* srcp)
{
    HDR_PS* pPs;
    int nb;

    pPs = (HDR_PS*)srcp;

    if (pPs->ucType == 41)
    {
        nb = ((*(int*)&pPs->usIndexOfs >> 16) * 24) + 12;

        njMemCopy4(dstp, srcp, nb / 4);
    }
    else
    {
        nb = (pPs->usIndexMax * 32) + 72;

        njMemCopy4(dstp, srcp, nb / 4);
    }

    return nb;
}

/* Verbatim copy of njplus.c lines 1375-1469 (npSetAllMatColor) — also
 * pure C ("100% matching!" in the decomp), needed by en01.c. Walks a
 * chunk model's material list directly, no asm, no other dependencies. */
// 100% matching!
void npSetAllMatColor(NJS_CNK_OBJECT* objp, int obj_n, unsigned int argb)
{
    int i;
    int offset;
    short head;
    short* plp;
    unsigned char* mat;
    unsigned char a;
    unsigned char r;
    unsigned char g;
    unsigned char b;

    a = ((argb & 0xFF000000) >> 24) & 0xFF;
    r = ((argb & 0xFF0000) >> 16) & 0xFF;
    g = ((argb & 0xFF00) >> 8) & 0xFF;
    b = argb & 0xFF;

    for (i = 0; i < obj_n; i++, objp++)
    {
        if ((objp->model != NULL) && (!(objp->evalflags & 0x8)))
        {
            plp = objp->model->plist;

            while (TRUE)
            {
                head = (unsigned char)*plp++;

                if ((head >= 64) && (head < 67))
                {
                    offset = *plp++;
                    plp += offset;
                }
                else if (head == 8)
                {
                    plp++;
                }
                else if ((head >= 17) && (head < 24))
                {
                    mat = (unsigned char*)plp + 2;

                    switch (head)
                    {
                    case 17:
                    case 21:
                        *mat++ = b;
                        *mat++ = g;
                        *mat++ = r;

                        if (*mat != 0)
                        {
                            *mat = a;
                        }

                        break;
                    case 19:
                    case 23:
                        *mat++ = b;
                        *mat++ = g;
                        *mat++ = r;

                        if (*mat != 0)
                        {
                            *mat = a;
                        }

                        mat++;

                        *mat++ = b;
                        *mat++ = g;
                        *mat++ = r;

                        if (*mat != 0)
                        {
                            *mat = a;
                        }

                        break;
                    }

                    offset = *plp++;
                    plp += offset;
                }
                else if ((head >= 56) && (head < 59))
                {
                    offset = *plp++;
                    plp += offset;
                }
                else if (head == 255)
                {
                    break;
                }
            }
        }
    }
}
