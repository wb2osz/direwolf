/* rg_xorshift.h - Xorshift32 PRNG for Rattlegram payload scrambling */

#ifndef RG_XORSHIFT_H
#define RG_XORSHIFT_H

#include <stdint.h>

#define RG_XORSHIFT_SEED 2463534242U

typedef struct {
    uint32_t y;
} rg_xorshift32_t;

void rg_xorshift32_init(rg_xorshift32_t *xs);
uint32_t rg_xorshift32_next(rg_xorshift32_t *xs);

#endif /* RG_XORSHIFT_H */
