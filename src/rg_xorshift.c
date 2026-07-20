/* rg_xorshift.c - Xorshift32 PRNG */

#include "rg_xorshift.h"

void rg_xorshift32_init(rg_xorshift32_t *xs)
{
    xs->y = RG_XORSHIFT_SEED;
}

uint32_t rg_xorshift32_next(rg_xorshift32_t *xs)
{
    xs->y ^= xs->y << 13;
    xs->y ^= xs->y >> 17;
    xs->y ^= xs->y << 5;
    return xs->y;
}
