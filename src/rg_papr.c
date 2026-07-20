/* rg_papr.c - PAPR reduction for Rattlegram encoder */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rg_papr.h"

/* For the typical case: fact >= 2, use oversized FFT */
static void papr_process_generic(rg_papr_t *p, rg_cplx_t *freq)
{
    int size = p->size;
    int fact = p->fact;
    int i;

    /* Track which carriers are used */
    for (i = 0; i < size; i++)
        p->used[i] = (freq[i].re != 0 || freq[i].im != 0);

    /* Zero-pad and split: place first half in center, second half at end */
    memset(p->over, 0, fact * size * sizeof(rg_cplx_t));
    for (i = 0; i < size / 2; i++)
        p->over[i] = freq[i];
    for (i = size / 2; i < size; i++)
        p->over[size * (fact - 1) + i] = freq[i];

    /* IFFT to time domain */
    rg_fft_inverse(p->bwd_fft, p->temp, p->over);

    /* Scale */
    {
        float factor = 1.0f / sqrtf((float)(fact * size));
        for (i = 0; i < fact * size; i++)
            p->temp[i] = rg_cmul_f(p->temp[i], factor);
    }

    /* Clip any sample with power > 1 */
    for (i = 0; i < fact * size; i++) {
        float pwr = rg_cnorm(p->temp[i]);
        if (pwr > 1.0f)
            p->temp[i] = rg_cdiv_f(p->temp[i], sqrtf(pwr));
    }

    /* FFT back to frequency domain */
    rg_fft_forward(p->fwd_fft, p->over, p->temp);

    /* Extract original carriers */
    {
        float factor = (float)fact;
        for (i = 0; i < size / 2; i++)
            if (p->used[i])
                freq[i] = rg_cmul_f(p->over[i], factor);
        for (i = size / 2; i < size; i++)
            if (p->used[i])
                freq[i] = rg_cmul_f(p->over[size * (fact - 1) + i], factor);
    }
}

/* Special case: fact == 1, simpler version */
static void papr_process_nofact(rg_papr_t *p, rg_cplx_t *freq)
{
    int size = p->size;
    int i;

    for (i = 0; i < size; i++)
        p->used[i] = (freq[i].re != 0 || freq[i].im != 0);

    /* IFFT */
    rg_fft_inverse(p->bwd_fft, p->temp, freq);

    /* Scale */
    {
        float factor = 1.0f / sqrtf((float)size);
        for (i = 0; i < size; i++)
            p->temp[i] = rg_cmul_f(p->temp[i], factor);
    }

    /* Clip */
    for (i = 0; i < size; i++) {
        float pwr = rg_cnorm(p->temp[i]);
        if (pwr > 1.0f)
            p->temp[i] = rg_cdiv_f(p->temp[i], sqrtf(pwr));
    }

    /* FFT back */
    rg_fft_forward(p->fwd_fft, freq, p->temp);

    /* Scale and zero unused */
    {
        float factor = 1.0f;
        for (i = 0; i < size; i++) {
            if (p->used[i])
                freq[i] = rg_cmul_f(freq[i], factor);
            else
                freq[i] = rg_cz();
        }
    }
}

rg_papr_t *rg_papr_create(int size, int fact)
{
    rg_papr_t *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->size = size;
    p->fact = fact;

    if (fact >= 2) {
        p->fwd_fft = rg_fft_create(fact * size);
        p->bwd_fft = rg_fft_create(fact * size);
        p->temp = calloc(fact * size, sizeof(rg_cplx_t));
        p->over = calloc(fact * size, sizeof(rg_cplx_t));
    } else {
        p->fwd_fft = rg_fft_create(size);
        p->bwd_fft = rg_fft_create(size);
        p->temp = calloc(size, sizeof(rg_cplx_t));
        p->over = NULL;
    }
    p->used = calloc(size, sizeof(int));

    if (!p->fwd_fft || !p->bwd_fft || !p->temp || !p->used ||
        (fact >= 2 && !p->over)) {
        rg_papr_free(p);
        return NULL;
    }
    return p;
}

void rg_papr_free(rg_papr_t *p)
{
    if (p) {
        rg_fft_free(p->fwd_fft);
        rg_fft_free(p->bwd_fft);
        free(p->temp);
        free(p->over);
        free(p->used);
        free(p);
    }
}

void rg_papr_process(rg_papr_t *p, rg_cplx_t *freq)
{
    if (p->fact >= 2)
        papr_process_generic(p, freq);
    else
        papr_process_nofact(p, freq);
}
