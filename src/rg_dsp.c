/* rg_dsp.c - Rattlegram DSP primitives implementation */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rg_dsp.h"

/* ---- BlockDC filter ---- */
void rg_blockdc_init(rg_blockdc_t *bd, int samples)
{
    bd->a = (float)(samples - 1) / (float)samples;
    bd->b = (1.0f + bd->a) / 2.0f;
    bd->x1 = 0.0f;
    bd->y1 = 0.0f;
}

float rg_blockdc_process(rg_blockdc_t *bd, float x0)
{
    float y0 = bd->b * (x0 - bd->x1) + bd->a * bd->y1;
    bd->x1 = x0;
    bd->y1 = y0;
    return y0;
}

/* ---- Hilbert transform (analytic signal) ---- */
/* Kaiser windowed FIR Hilbert transformer */
static float kaiser_i0(float x)
{
    /* I_0(x) via series, same as C++ reference */
    float sum = 1.0f;
    float val = 1.0f;
    int n;
    for (n = 1; n < 35; n++) {
        val *= x / (2.0f * (float)n);
        sum += val * val;
    }
    return sum;
}

static float kaiser_win(float a, int n, int N)
{
    /* C++ formula: sqrt(1 - (2*n/(N-1) - 1)^2) */
    float x = 2.0f * (float)n / (float)(N - 1) - 1.0f;
    float arg = M_PI * a * sqrtf(1.0f - x * x);
    return kaiser_i0(arg) / kaiser_i0(M_PI * a);
}

rg_hilbert_t *rg_hilbert_create(int taps)
{
    rg_hilbert_t *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->taps = taps;
    h->real_buf = calloc(taps, sizeof(float));
    h->imco = calloc((taps - 1) / 4, sizeof(float));
    if (!h->real_buf || !h->imco) {
        free(h->real_buf);
        free(h->imco);
        free(h);
        return NULL;
    }
    /* Kaiser window, alpha=2 */
    {
        int half = (taps - 1) / 2;
        float a = 2.0f;
        int i;
        h->reco = kaiser_win(a, half, taps);
        for (i = 0; i < (taps - 1) / 4; i++) {
            float w = kaiser_win(a, (2*i + 1) + half, taps);
            h->imco[i] = w * 2.0f / ((2*i + 1) * M_PI);
        }
    }
    return h;
}

void rg_hilbert_free(rg_hilbert_t *h)
{
    if (h) {
        free(h->real_buf);
        free(h->imco);
        free(h);
    }
}

rg_cplx_t rg_hilbert_process(rg_hilbert_t *h, float input)
{
    int half = (h->taps - 1) / 2;
    int i;
    float re = h->reco * h->real_buf[half];
    float im = h->imco[0] * (h->real_buf[half - 1] - h->real_buf[half + 1]);
    for (i = 1; i < (h->taps - 1) / 4; i++) {
        im += h->imco[i] * (h->real_buf[half - (2*i + 1)] - h->real_buf[half + (2*i + 1)]);
    }
    /* shift buffer */
    for (i = 0; i < h->taps - 1; i++)
        h->real_buf[i] = h->real_buf[i + 1];
    h->real_buf[h->taps - 1] = input;
    return (rg_cplx_t){re, im};
}

/* ---- Phasor (NCO) ---- */
void rg_phasor_init(rg_phasor_t *p)
{
    p->prev = (rg_cplx_t){1.0f, 0.0f};
    p->delta = (rg_cplx_t){1.0f, 0.0f};
}

void rg_phasor_omega(rg_phasor_t *p, float v)
{
    p->delta = (rg_cplx_t){cosf(v), sinf(v)};
}

rg_cplx_t rg_phasor_step(rg_phasor_t *p)
{
    rg_cplx_t tmp = p->prev;
    p->prev = rg_cmul(p->prev, p->delta);
    /* normalize to prevent drift */
    {
        float mag = rg_cabs(p->prev);
        if (mag > 0.0f)
            p->prev = rg_cdiv_f(p->prev, mag);
    }
    return tmp;
}

/* ---- SMA4 (streaming moving average via SWA tree) ---- */
rg_sma4_t *rg_sma4_create(int num)
{
    rg_sma4_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->num = num;
    s->tree = calloc(2 * num, sizeof(float));
    if (!s->tree) {
        free(s);
        return NULL;
    }
    s->leaf = num;  /* first leaf position */
    return s;
}

void rg_sma4_free(rg_sma4_t *s)
{
    if (s) {
        free(s->tree);
        free(s);
    }
}

/* Returns the sliding window SUM (not average) */
float rg_sma4_process(rg_sma4_t *s, float input)
{
    s->tree[s->leaf] = input;
    {
        int child = s->leaf;
        int parent = child / 2;
        while (parent) {
            s->tree[parent] = s->tree[child] + s->tree[child ^ 1];
            child = parent;
            parent /= 2;
        }
    }
    if (++s->leaf >= 2 * s->num)
        s->leaf = s->num;
    return s->tree[1] / (float)s->num;  /* return average */
}

/* ---- Delay line ---- */
rg_delay_t *rg_delay_create(int num)
{
    rg_delay_t *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->num = num;
    d->buf = calloc(num, sizeof(float));
    if (!d->buf) {
        free(d);
        return NULL;
    }
    d->pos = 0;
    return d;
}

void rg_delay_free(rg_delay_t *d)
{
    if (d) {
        free(d->buf);
        free(d);
    }
}

float rg_delay_process(rg_delay_t *d, float input)
{
    float tmp = d->buf[d->pos];
    d->buf[d->pos] = input;
    if (++d->pos >= d->num)
        d->pos = 0;
    return tmp;
}

/* ---- Schmitt Trigger ---- */
void rg_schmitt_init(rg_schmitt_t *st, float low, float high)
{
    st->low = low;
    st->high = high;
    st->prev = 0;
}

int rg_schmitt_process(rg_schmitt_t *st, float input)
{
    if (st->prev) {
        if (input < st->low)
            st->prev = 0;
    } else {
        if (input > st->high)
            st->prev = 1;
    }
    return st->prev;
}

/* ---- Falling Edge Trigger ---- */
void rg_falling_edge_init(rg_falling_edge_t *fe)
{
    fe->prev = 0;
}

int rg_falling_edge_process(rg_falling_edge_t *fe, int input)
{
    int tmp = fe->prev;
    fe->prev = input;
    return tmp && !input;
}
