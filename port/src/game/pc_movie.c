/*
 * PC stand-in for ps2_MovieFunc.c, the MPEG movie player that decodes with the
 * PS2's IPU and streams sound to the IOP. Until movies are decoded on PC
 * (TODO: FFmpeg), every movie reports itself finished right away, so the game
 * skips it.
 */
#include "ps2_MovieFunc.h"

RMI_WORK rmi;
MDSIZE_WORK mdSize;
int movie_draw;
StrFile infile;
u_long128 test_tag[1400];
VoBuf voBuf;

void initAll()
{
    memset(&rmi, 0, sizeof(rmi));
    memset(&voBuf, 0, sizeof(voBuf));
}

void termAll()
{
}

void readMpeg()
{
    rmi.iMovieState = 3; /* finished */
}

void setImageTag(unsigned int* tags, void* image)
{
}

void vbrank_draw()
{
}

int sendToIOP(int dst, u_char* src, int size)
{
    return size;
}

void changeInputVolume(u_int val)
{
}
