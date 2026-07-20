/* rg_fft.h - FFT for Rattlegram using FFTW3 */

#ifndef RG_FFT_H
#define RG_FFT_H

#include "rg_dsp.h"
#include <fftw3.h>

typedef struct {
    int size;               /* DFT size */
    double *d_in;           /* FFTW3 input buffer (2*size doubles) */
    double *d_out;          /* FFTW3 output buffer (2*size doubles) */
    fftw_plan fwd_plan;     /* forward DFT plan */
    fftw_plan inv_plan;     /* inverse DFT plan */
} rg_fft_t;

rg_fft_t *rg_fft_create(int size);
void rg_fft_forward(rg_fft_t *fft, rg_cplx_t *dst, const rg_cplx_t *src);
void rg_fft_inverse(rg_fft_t *fft, rg_cplx_t *dst, const rg_cplx_t *src);
void rg_fft_free(rg_fft_t *fft);

#endif /* RG_FFT_H */
