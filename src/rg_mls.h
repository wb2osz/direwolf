/* rg_mls.h - Maximum Length Sequence (MLS) LFSR generator */

#ifndef RG_MLS_H
#define RG_MLS_H

typedef struct {
    unsigned int poly;
    unsigned int test;
    unsigned int reg;
} rg_mls_t;

void rg_mls_init(rg_mls_t *mls, unsigned int poly, unsigned int reg);
int rg_mls_next(rg_mls_t *mls);

/* NRZ mapping: true->+1, false->-1 (same as 1-2*bit) */
static inline int rg_mls_nrz(rg_mls_t *mls)
{
    return 1 - 2 * rg_mls_next(mls);
}

#endif /* RG_MLS_H */
