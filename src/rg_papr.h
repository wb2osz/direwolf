/* rg_papr.h - PAPR reduction for Rattlegram encoder */

#ifndef RG_PAPR_H
#define RG_PAPR_H

#include "rg_dsp.h"
#include "rg_fft.h"

/* PAPR reduction state */
typedef struct {
    int size;          /* OFDM symbol length */
    int fact;          /* Oversampling factor */
    rg_fft_t *fwd_fft; /* FFT for oversized symbols */
    rg_fft_t *bwd_fft; /* IFFT for oversized symbols */
    rg_cplx_t *temp;   /* Scratch: fact * size */
    rg_cplx_t *over;   /* Scratch: fact * size */
    int *used;         /* Track which carriers are active: size */
} rg_papr_t;

rg_papr_t *rg_papr_create(int size, int fact);
void rg_papr_free(rg_papr_t *papr);

/* Apply PAPR reduction to frequency-domain symbol */
void rg_papr_process(rg_papr_t *papr, rg_cplx_t *freq);

#endif /* RG_PAPR_H */
