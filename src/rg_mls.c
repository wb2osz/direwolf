/* rg_mls.c - Maximum Length Sequence (MLS) LFSR generator */

#include "rg_mls.h"

static unsigned int rg_hibit(unsigned int n)
{
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n ^ (n >> 1);
}

void rg_mls_init(rg_mls_t *mls, unsigned int poly, unsigned int reg)
{
    mls->poly = poly;
    mls->test = rg_hibit(poly) >> 1;
    mls->reg = reg;
}

int rg_mls_next(rg_mls_t *mls)
{
    int fb = (mls->reg & mls->test) ? 1 : 0;
    mls->reg <<= 1;
    mls->reg ^= fb * mls->poly;
    return fb;
}
