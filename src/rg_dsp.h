/* rg_dsp.h - Rattlegram DSP primitives (complex math, BlockDC, Hilbert) */

#ifndef RG_DSP_H
#define RG_DSP_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Complex number type ---- */
typedef struct {
    float re;
    float im;
} rg_cplx_t;

static inline rg_cplx_t rg_cz(void) { return (rg_cplx_t){0, 0}; }
static inline rg_cplx_t rg_cf(float r) { return (rg_cplx_t){r, 0}; }
static inline rg_cplx_t rg_cmk(float r, float i) { return (rg_cplx_t){r, i}; }
static inline rg_cplx_t rg_cadd(rg_cplx_t a, rg_cplx_t b) { return (rg_cplx_t){a.re + b.re, a.im + b.im}; }
static inline rg_cplx_t rg_csub(rg_cplx_t a, rg_cplx_t b) { return (rg_cplx_t){a.re - b.re, a.im - b.im}; }
static inline rg_cplx_t rg_cmul(rg_cplx_t a, rg_cplx_t b) { return (rg_cplx_t){a.re*b.re - a.im*b.im, a.re*b.im + a.im*b.re}; }
static inline rg_cplx_t rg_cmul_f(rg_cplx_t a, float f) { return (rg_cplx_t){a.re*f, a.im*f}; }
static inline rg_cplx_t rg_cdiv(rg_cplx_t a, rg_cplx_t b) {
    float d = b.re*b.re + b.im*b.im;
    return (rg_cplx_t){(a.re*b.re + a.im*b.im)/d, (a.im*b.re - a.re*b.im)/d};
}
static inline rg_cplx_t rg_cdiv_f(rg_cplx_t a, float f) { return (rg_cplx_t){a.re/f, a.im/f}; }
static inline rg_cplx_t rg_cconj(rg_cplx_t a) { return (rg_cplx_t){a.re, -a.im}; }
static inline float rg_cnorm(rg_cplx_t a) { return a.re*a.re + a.im*a.im; }
static inline float rg_cabs(rg_cplx_t a) { return sqrtf(a.re*a.re + a.im*a.im); }
static inline float rg_carg(rg_cplx_t a) { return atan2f(a.im, a.re); }
static inline rg_cplx_t rg_cpolar(float r, float theta) { return (rg_cplx_t){r*cosf(theta), r*sinf(theta)}; }
static inline rg_cplx_t rg_clerp(rg_cplx_t a, rg_cplx_t b, float t) {
    return (rg_cplx_t){(1.0f-t)*a.re + t*b.re, (1.0f-t)*a.im + t*b.im};
}

/* ---- BlockDC filter ---- */
typedef struct {
    float x1, y1, a, b;
} rg_blockdc_t;

void rg_blockdc_init(rg_blockdc_t *bd, int samples);
float rg_blockdc_process(rg_blockdc_t *bd, float x0);

/* ---- Hilbert transform ---- */
typedef struct {
    int taps;
    float *real_buf;
    float *imco;
    float reco;
} rg_hilbert_t;

rg_hilbert_t *rg_hilbert_create(int taps);
void rg_hilbert_free(rg_hilbert_t *h);
rg_cplx_t rg_hilbert_process(rg_hilbert_t *h, float input);

/* ---- Phasor (NCO) ---- */
typedef struct {
    rg_cplx_t prev;
    rg_cplx_t delta;
} rg_phasor_t;

void rg_phasor_init(rg_phasor_t *p);
void rg_phasor_omega(rg_phasor_t *p, float v);
rg_cplx_t rg_phasor_step(rg_phasor_t *p);

/* ---- SMA4 (sliding window average via SWA tree) ---- */
typedef struct {
    float *tree;
    int num;
    int leaf;
} rg_sma4_t;

rg_sma4_t *rg_sma4_create(int num);
void rg_sma4_free(rg_sma4_t *s);
float rg_sma4_process(rg_sma4_t *s, float input);

/* ---- Delay line ---- */
typedef struct {
    float *buf;
    int num;
    int pos;
} rg_delay_t;

rg_delay_t *rg_delay_create(int num);
void rg_delay_free(rg_delay_t *d);
float rg_delay_process(rg_delay_t *d, float input);

/* ---- Schmitt Trigger ---- */
typedef struct {
    float low, high;
    int prev;
} rg_schmitt_t;

void rg_schmitt_init(rg_schmitt_t *st, float low, float high);
int rg_schmitt_process(rg_schmitt_t *st, float input);

/* ---- Falling Edge Trigger ---- */
typedef struct {
    int prev;
} rg_falling_edge_t;

void rg_falling_edge_init(rg_falling_edge_t *fe);
int rg_falling_edge_process(rg_falling_edge_t *fe, int input);

#endif /* RG_DSP_H */
