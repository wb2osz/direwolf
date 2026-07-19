/*
 * rg_sc.c - Schmidl-Cox synchronization correlator for Rattlegram
 *
 * Port of DSP::SchmidlCox<float, cmplx, search_position, symbol_length/2, guard_length>
 * from the C++ reference.
 *
 * Uses a BipBuffer (mirrored circular buffer) matching the C++ implementation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "rg_sc.h"
#include "rg_fft.h"
#include "rg_dsp.h"

/* ---- BipBuffer (mirrors C++ DSP::BipBuffer) ---- */
/* The buffer has 2*NUM elements. pos0 and pos1 are always NUM apart.
 * Writing mirrors the sample at both pos0 and pos1. The valid window
 * is always contiguous starting at min(pos0, pos1). */

/* ---- SWA: Sliding Window Accelerator (binary tree) ---- */
static float swa_push(float *tree, int size, int *leaf, float val)
{
    tree[*leaf] = val;
    for (int pos = *leaf, parent = *leaf / 2; parent; pos = parent, parent /= 2)
        tree[parent] = tree[pos] + tree[pos ^ 1];
    if (++(*leaf) >= 2 * size) *leaf = size;
    return tree[1];
}

/* ---- Delay line ---- */
static float delay_push(float *buf, int *pos, int len, float val)
{
    float out = buf[*pos];
    buf[*pos] = val;
    *pos = (*pos + 1) % len;
    return out;
}

/* ---- Triggers ---- */
static int schmitt_push(int *state, float low, float high, float val)
{
    if (!*state && val > high) *state = 1;
    else if (*state && val < low) *state = 0;
    return *state;
}

static int falling_push(int *prev, int val)
{
    int out = *prev && !val;
    *prev = val;
    return out;
}

/* ---- Internal struct ---- */
struct rg_sc_s {
    int half_sym;       /* symbol_length / 2 */
    int guard_len;
    int search_pos;     /* extended_length */
    int match_len;      /* guard_len | 1 */
    int match_del;      /* (match_len - 1) / 2 */
    int buf_len;        /* buffer_length = 4 * extended_length */
    int buf_size;       /* 2 * buf_len */

    /* BipBuffer */
    rg_cplx_t *buf;     /* 2 * buf_len elements */
    int pos0;
    int pos1;

    /* SWA trees for SMA4 */
    float *cor_re_swa;  /* size: half_sym * 2 */
    float *cor_im_swa;  /* size: half_sym * 2 */
    float *pwr_swa;     /* size: 2*half_sym * 2 */
    float *match_swa;   /* size: match_len * 2 */

    /* Leaf positions */
    int cor_re_leaf;
    int cor_im_leaf;
    int pwr_leaf;
    int match_leaf;

    /* Delay for phase alignment */
    float *phase_delay;
    int phase_delay_pos;

    /* Triggers */
    int schmitt_state;
    float schmitt_low;
    float schmitt_high;
    int falling_prev;

    /* Peak tracking */
    float timing_max;
    float phase_max;
    int index_max;

    /* Output */
    int symbol_pos;
    float cfo_rad;
    float frac_cfo;

    /* FFT workspace */
    rg_fft_t *fwd_fft;
    rg_fft_t *bwd_fft;
    rg_cplx_t *tmp0;
    rg_cplx_t *tmp1;
    rg_cplx_t *kern;
};

/* Get pointer to BipBuffer window (valid contiguous region) */
static const rg_cplx_t *bip_buffer_window(rg_sc_t *sc)
{
    int start = sc->pos0 < sc->pos1 ? sc->pos0 : sc->pos1;
    return sc->buf + start;
}

rg_sc_t *rg_sc_create(int symbol_len, int search_pos, int guard_len,
                      const int8_t *mls_seq, int cor_seq_len)
{
    int half = symbol_len / 2;
    int match_len = guard_len | 1;
    int match_del = (match_len - 1) / 2;
    int buf_len = 4 * (2 * symbol_len + guard_len);  /* 4 * extended_length = 4 * (symbol_length + guard_length) */

    rg_sc_t *sc = calloc(1, sizeof(*sc));
    if (!sc) return NULL;

    sc->half_sym = half;
    sc->guard_len = guard_len;
    sc->search_pos = search_pos;
    sc->match_len = match_len;
    sc->match_del = match_del;
    sc->buf_len = buf_len;
    sc->buf_size = 2 * buf_len;
    sc->pos0 = 0;
    sc->pos1 = buf_len;

    /* BipBuffer */
    sc->buf = calloc(sc->buf_size, sizeof(rg_cplx_t));

    /* SWA trees */
    sc->cor_re_swa = calloc(half * 2, sizeof(float));
    sc->cor_im_swa = calloc(half * 2, sizeof(float));
    sc->pwr_swa = calloc(half * 4, sizeof(float));
    sc->match_swa = calloc(match_len * 2, sizeof(float));
    sc->phase_delay = calloc(match_del > 0 ? match_del : 1, sizeof(float));
    sc->phase_delay_pos = 0;

    /* Leaf positions (start at NUM, matching C++ SWA constructor) */
    sc->cor_re_leaf = half;
    sc->cor_im_leaf = half;
    sc->pwr_leaf = half * 2;
    sc->match_leaf = match_len;

    /* Triggers */
    sc->schmitt_low = 0.17f * match_len;
    sc->schmitt_high = 0.19f * match_len;
    sc->schmitt_state = 0;
    sc->falling_prev = 0;

    /* FFT plans */
    sc->fwd_fft = rg_fft_create(half);
    sc->bwd_fft = rg_fft_create(half);
    sc->tmp0 = calloc(half, sizeof(rg_cplx_t));
    sc->tmp1 = calloc(half, sizeof(rg_cplx_t));
    sc->kern = calloc(half, sizeof(rg_cplx_t));

    if (!sc->buf || !sc->cor_re_swa || !sc->cor_im_swa || !sc->pwr_swa ||
        !sc->match_swa || !sc->phase_delay || !sc->fwd_fft || !sc->bwd_fft ||
        !sc->tmp0 || !sc->tmp1 || !sc->kern) {
        rg_sc_free(sc);
        return NULL;
    }

    /* Build correlation kernel (matching C++ SchmidlCox constructor) */
    {
        int i, N = half;
        memset(sc->kern, 0, N * sizeof(rg_cplx_t));
        for (i = 0; i < cor_seq_len; i++) {
            int bin = ((cor_seq_len == 127 ? -63 : 0) + i + N) % N;
            /* cor_seq_off = -126, cor_seq_off/2 = -63 */
            /* But use the passed mls_seq values */
            sc->kern[bin] = rg_cf((float)mls_seq[i]);
        }

        rg_fft_forward(sc->fwd_fft, sc->kern, sc->kern);
        for (i = 0; i < N; i++) {
            sc->kern[i] = rg_cconj(sc->kern[i]);
            sc->kern[i].re /= (float)N;
            sc->kern[i].im /= (float)N;
        }
    }

    return sc;
}

void rg_sc_free(rg_sc_t *sc)
{
    if (!sc) return;
    free(sc->buf);
    free(sc->cor_re_swa);
    free(sc->cor_im_swa);
    free(sc->pwr_swa);
    free(sc->match_swa);
    free(sc->phase_delay);
    rg_fft_free(sc->fwd_fft);
    rg_fft_free(sc->bwd_fft);
    free(sc->tmp0);
    free(sc->tmp1);
    free(sc->kern);
    free(sc);
}

int rg_sc_process(rg_sc_t *sc, const rg_cplx_t *sample)
{
    int N = sc->half_sym;
    int search = sc->search_pos;
    int i;

    /* Write sample to BipBuffer (mirror at both positions) */
    sc->buf[sc->pos0] = sc->buf[sc->pos1] = *sample;
    if (++sc->pos0 >= sc->buf_size) sc->pos0 = 0;
    if (++sc->pos1 >= sc->buf_size) sc->pos1 = 0;

    /* Get window pointer (contiguous valid region) */
    const rg_cplx_t *samples = bip_buffer_window(sc);

    /* P = samples[search + N] * conj(samples[search + 2*N]) */
    rg_cplx_t a = samples[search + N];
    rg_cplx_t b = samples[search + 2 * N];
    rg_cplx_t P;
    P.re = a.re * b.re + a.im * b.im;
    P.im = a.im * b.re - a.re * b.im;

    /* SMA4 for complex P (separate real and imaginary) */
    float P_re_sum = swa_push(sc->cor_re_swa, N, &sc->cor_re_leaf, P.re);
    float P_im_sum = swa_push(sc->cor_im_swa, N, &sc->cor_im_leaf, P.im);

    /* SMA4 for power */
    float norm_b = b.re * b.re + b.im * b.im;
    float R_sum = 0.5f * swa_push(sc->pwr_swa, 2 * N, &sc->pwr_leaf, norm_b);
    float min_R = 0.00001f * (float)N;
    if (R_sum < min_R) R_sum = min_R;

    /* Timing metric: |P|^2 / R^2 */
    float norm_P = P_re_sum * P_re_sum + P_im_sum * P_im_sum;
    float timing_raw = norm_P / (R_sum * R_sum);
    float timing_sum = swa_push(sc->match_swa, sc->match_len, &sc->match_leaf, timing_raw);

    /* Phase delay */
    float phase = delay_push(sc->phase_delay, &sc->phase_delay_pos,
                             sc->match_del, atan2f(P_im_sum, P_re_sum));

    /* Triggers */
    int collect = schmitt_push(&sc->schmitt_state, sc->schmitt_low,
                                sc->schmitt_high, timing_sum);
    int process = falling_push(&sc->falling_prev, collect);

    if (!collect && !process)
        return 0;

    /* Track peak timing and corresponding phase */
    if (timing_sum > sc->timing_max) {
        sc->timing_max = timing_sum;
        sc->phase_max = phase;
        sc->index_max = sc->match_del;
    } else if (sc->index_max < N + sc->guard_len + sc->match_del) {
        sc->index_max++;
    }

    if (!process)
        return 0;

    /* === Falling edge: fine CFO estimation === */
    sc->frac_cfo = sc->phase_max / (float)N;

    rg_phasor_t osc;
    rg_phasor_init(&osc);
    rg_phasor_omega(&osc, sc->frac_cfo);

    sc->symbol_pos = search - sc->index_max;
    sc->index_max = 0;
    sc->timing_max = 0;

    /* Extract half symbol with CFO correction */
    for (i = 0; i < N; i++) {
        sc->tmp1[i] = rg_cmul(samples[sc->symbol_pos + N + i],
                              rg_phasor_step(&osc));
    }

    rg_fft_forward(sc->fwd_fft, sc->tmp0, sc->tmp1);

    /* Differential BPSK demod in frequency domain */
    {
        float min_pwr = 0;
        for (i = 0; i < N; i++)
            min_pwr += sc->tmp0[i].re * sc->tmp0[i].re +
                       sc->tmp0[i].im * sc->tmp0[i].im;
        min_pwr /= (float)N;

        for (i = 0; i < N; i++) {
            int prev_idx = (i - 1 + N) % N;
            rg_cplx_t curr = sc->tmp0[i];
            rg_cplx_t prev = sc->tmp0[prev_idx];
            float pc = curr.re * curr.re + curr.im * curr.im;
            float pp = prev.re * prev.re + prev.im * prev.im;
            if (pc > min_pwr && pp > min_pwr) {
                rg_cplx_t cons = rg_cdiv(curr, prev);
                float cn = cons.re * cons.re + cons.im * cons.im;
                sc->tmp1[i] = (cn < 4.0f) ? cons : rg_cz();
            } else {
                sc->tmp1[i] = rg_cz();
            }
        }
    }

    /* FFT of demodulated signal */
    rg_fft_forward(sc->fwd_fft, sc->tmp0, sc->tmp1);

    /* Correlate with kernel */
    for (i = 0; i < N; i++)
        sc->tmp0[i] = rg_cmul(sc->tmp0[i], sc->kern[i]);

    /* IFFT (using conj-FFT-conj trick for unnormalized IFFT) */
    for (i = 0; i < N; i++)
        sc->tmp1[i] = (rg_cplx_t){sc->tmp0[i].re, -sc->tmp0[i].im};
    rg_fft_forward(sc->bwd_fft, sc->tmp1, sc->tmp1);
    for (i = 0; i < N; i++)
        sc->tmp1[i] = (rg_cplx_t){sc->tmp1[i].re, -sc->tmp1[i].im};

    /* Find peak */
    {
        float peak = 0, next_peak = 0;
        int shift = 0;
        for (i = 0; i < N; i++) {
            float pwr = sc->tmp1[i].re * sc->tmp1[i].re +
                        sc->tmp1[i].im * sc->tmp1[i].im;
            if (pwr > peak) {
                next_peak = peak;
                peak = pwr;
                shift = i;
            } else if (pwr > next_peak) {
                next_peak = pwr;
            }
        }

        if (peak <= next_peak * 4.0f)
            return 0;

        int pos_err = (int)nearbyint(atan2f(sc->tmp1[shift].im,
                                             sc->tmp1[shift].re) *
                                      (float)N / (2.0f * M_PI));
        if (pos_err > sc->guard_len / 2 || pos_err < -sc->guard_len / 2)
            return 0;

        sc->symbol_pos -= pos_err;

        sc->cfo_rad = (float)shift * (2.0f * M_PI / (float)N) - sc->frac_cfo;
        if (sc->cfo_rad >= M_PI)
            sc->cfo_rad -= 2.0f * M_PI;

        return 1;
    }
}

int rg_sc_get_symbol_pos(rg_sc_t *sc) { return sc->symbol_pos; }
float rg_sc_get_cfo_rad(rg_sc_t *sc) { return sc->cfo_rad; }
int rg_sc_get_half_sym(rg_sc_t *sc) { return sc->half_sym; }
int rg_sc_get_guard_len(rg_sc_t *sc) { return sc->guard_len; }

/* Process with flat buffer (legacy API, for compatibility) */
int rg_sc_process_flat(rg_sc_t *sc, const rg_cplx_t *buf, int buf_len, int needed)
{
    /* Feed samples one at a time through the BipBuffer */
    for (int i = 0; i < needed; i++) {
        int idx = i % buf_len;
        if (rg_sc_process(sc, &buf[idx]))
            return 1;
    }
    return 0;
}
