/*
 * Ninja-plus capsule/sphere collision math — extracted from
 * src/ps2/veronica/prog/njplus.c, which as a whole is saturated with
 * VU0 inline asm (12 asm blocks starting at line 754, mostly skinning/
 * matrix-stack helpers unrelated to these 4 functions). These 4 are
 * "100% matching!" pure C in the original file (verified: no asm before
 * line 631, where npDistanceP2C ends) and only call njCollisionCheckSS/
 * njDistanceL2L/njDistanceP2L (real, ps2_NaColi.c) and
 * njInnerProduct/njScalor/njUnitVector (real, ninja_3d.c) — both already
 * compiled. Needed by weapon.c's bhCheckGunAtari/bhCheckKnifeAtari/
 * bhCheckFlyAtari/bhCheckBombAtari/bhCheckCapCol2Capsule.
 *
 * Verbatim copy of njplus.c lines 33-631 (source comments/formatting
 * preserved from the decomp).
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
