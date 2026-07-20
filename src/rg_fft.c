/*
 * rg_fft.c - FFT for Rattlegram using FFTW3
 *
 * Uses FFTW3 for all FFT sizes (power-of-2 and non-power-of-2).
 * Convention: UNNORMALIZED forward and inverse.
 * Forward: X[k] = sum_n x[n] * exp(-j*2*pi*n*k/N)
 * Inverse: x[n] = sum_k X[k] * exp(+j*2*pi*n*k/N)
 *
 * Each rg_fft_t object has its own plans and buffers, so multiple
 * FFT objects can be used concurrently without corruption.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>
#include "rg_fft.h"

rg_fft_t *rg_fft_create(int size)
{
    rg_fft_t *fft = calloc(1, sizeof(*fft));
    if (!fft) return NULL;
    fft->size = size;

    /* Allocate double-precision buffers for FFTW3 */
    fft->d_in  = (double *)fftw_malloc(sizeof(double) * 2 * size);
    fft->d_out = (double *)fftw_malloc(sizeof(double) * 2 * size);
    if (!fft->d_in || !fft->d_out) {
        fftw_free(fft->d_in);
        fftw_free(fft->d_out);
        free(fft);
        return NULL;
    }

    /* Create forward and inverse plans with dedicated buffers.
     * FFTW_ESTIMATE is fast and doesn't modify the arrays.
     * Each FFT object has its own plans, so concurrent use is safe. */
    fft->fwd_plan = fftw_plan_dft_1d(size,
                                       (fftw_complex *)fft->d_in,
                                       (fftw_complex *)fft->d_out,
                                       FFTW_FORWARD, FFTW_ESTIMATE);
    fft->inv_plan = fftw_plan_dft_1d(size,
                                       (fftw_complex *)fft->d_in,
                                       (fftw_complex *)fft->d_out,
                                       FFTW_BACKWARD, FFTW_ESTIMATE);

    if (!fft->fwd_plan || !fft->inv_plan) {
        fftw_destroy_plan(fft->fwd_plan);
        fftw_destroy_plan(fft->inv_plan);
        fftw_free(fft->d_in);
        fftw_free(fft->d_out);
        free(fft);
        return NULL;
    }

    return fft;
}

void rg_fft_free(rg_fft_t *fft)
{
    if (!fft) return;
    fftw_destroy_plan(fft->fwd_plan);
    fftw_destroy_plan(fft->inv_plan);
    fftw_free(fft->d_in);
    fftw_free(fft->d_out);
    free(fft);
}

void rg_fft_forward(rg_fft_t *fft, rg_cplx_t *dst, const rg_cplx_t *src)
{
    int N = fft->size;
    int i;

    /* Copy float input to double (interleaved real/imag) */
    double *d_in  = fft->d_in;
    double *d_out = fft->d_out;
    for (i = 0; i < N; i++) {
        d_in[2*i]     = (double)src[i].re;
        d_in[2*i + 1] = (double)src[i].im;
    }

    /* Execute forward DFT (unnormalized) */
    fftw_execute(fft->fwd_plan);

    /* Copy double output back to float */
    for (i = 0; i < N; i++) {
        dst[i].re = (float)d_out[2*i];
        dst[i].im = (float)d_out[2*i + 1];
    }
}

void rg_fft_inverse(rg_fft_t *fft, rg_cplx_t *dst, const rg_cplx_t *src)
{
    int N = fft->size;
    int i;

    /* Copy float input to double (interleaved real/imag) */
    double *d_in  = fft->d_in;
    double *d_out = fft->d_out;
    for (i = 0; i < N; i++) {
        d_in[2*i]     = (double)src[i].re;
        d_in[2*i + 1] = (double)src[i].im;
    }

    /* Execute inverse DFT (unnormalized) */
    fftw_execute(fft->inv_plan);

    /* Copy double output back to float */
    for (i = 0; i < N; i++) {
        dst[i].re = (float)d_out[2*i];
        dst[i].im = (float)d_out[2*i + 1];
    }
}
