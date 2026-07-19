#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rg_encoder.h"
#include "rg_fft.h"
#include "rg_mls.h"
#include "rg_crc.h"
#include "rg_bch.h"
#include "rg_polar.h"
#include "rg_polar_tables.h"
#include "rg_xorshift.h"
#include "rg_dsp.h"

#define M_PI 3.14159265358979323846

struct rg_encoder_s {
    int rate;
    int symbol_length;
    int guard_length;
    int extended_length;
    int carrier_offset;
    int data_bits;
    int mode;
    int count_down;
    int symbol_number;
    int skip_papr;
    int noise_count;
    uint8_t payload[170];
    int8_t call[9];
    uint64_t meta_data;
    int8_t code[2048];
    rg_cplx_t *freq;
    rg_cplx_t *temp;
    rg_cplx_t *prev;
    rg_cplx_t *guard;
    rg_fft_t *fft;
    int papr_fact;
    rg_cplx_t *papr_freq;
};

static int nrz(int bit) { return 1 - 2 * bit; }

static int base37_map(int c) {
    if (c >= '0' && c <= '9') return c - '0' + 1;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 11;
    if (c >= 'a' && c <= 'z') return c - 'a' + 11;
    return 0;
}

static uint64_t base37(const char *s) {
    uint64_t val = 0;
    for (int i = 0; i < 8 && s[i]; i++)
        val = val * 37 + base37_map(s[i]);
    return val;
}

static void improve_papr(rg_encoder_t *enc) {
    int sl = enc->symbol_length;
    int fact = enc->papr_fact;
    rg_fft_t *fft_papr;
    rg_cplx_t *buf;

    if (fact <= 1) {
        /* C++ ImprovePAPR<cmplx, size, 1>: IFFT -> scale -> clip -> FFT -> scale */
        fft_papr = rg_fft_create(sl);
        rg_cplx_t *freq = enc->freq;
        rg_cplx_t *paprtmp = enc->papr_freq;
        int i;
        /* Mark used carriers */
        for (i = 0; i < sl; i++) {
            if (freq[i].re != 0 || freq[i].im != 0) paprtmp[i].re = 1;
            else paprtmp[i].re = 0;
        }
        /* IFFT */
        rg_fft_inverse(fft_papr, enc->temp, freq);
        float inv_scale = 1.0f / sqrtf((float)sl);
        /* Scale */
        for (i = 0; i < sl; i++) {
            enc->temp[i].re *= inv_scale;
            enc->temp[i].im *= inv_scale;
        }
        /* Clip |temp| > 1 */
        for (i = 0; i < sl; i++) {
            float p = enc->temp[i].re*enc->temp[i].re + enc->temp[i].im*enc->temp[i].im;
            if (p > 1.0f) {
                float s = 1.0f / sqrtf(p);
                enc->temp[i].re *= s;
                enc->temp[i].im *= s;
            }
        }
        /* FFT back, overwriting freq (C++: fwd(freq, temp)) */
        rg_fft_forward(fft_papr, freq, enc->temp);
        /* Scale by 1/sqrt(size) */
        for (i = 0; i < sl; i++) {
            if (paprtmp[i].re != 0) {
                freq[i].re *= inv_scale;
                freq[i].im *= inv_scale;
            } else {
                freq[i].re = 0;
                freq[i].im = 0;
            }
        }
        rg_fft_free(fft_papr);
    } else {
        int big_len = fact * sl;
        buf = enc->papr_freq;
        memset(buf, 0, big_len * sizeof(rg_cplx_t));
        for (int i = 0; i < sl; i++) buf[i] = enc->freq[i];
        fft_papr = rg_fft_create(big_len);
        rg_fft_inverse(fft_papr, buf, buf);
        for (int i = 0; i < big_len; i++) {
            float p = buf[i].re*buf[i].re + buf[i].im*buf[i].im;
            if (p > 1.0f) { float s = 1.0f/sqrtf(p); buf[i].re *= s; buf[i].im *= s; }
        }
        rg_fft_forward(fft_papr, buf, buf);
        for (int i = 0; i < sl; i++) enc->freq[i] = buf[i];
        rg_fft_free(fft_papr);
    }
}

static void transform(rg_encoder_t *enc) {
    if (!enc->skip_papr) improve_papr(enc);
    rg_fft_inverse(enc->fft, enc->temp, enc->freq);
    float scale = 1.0f / sqrtf(8.0f * enc->symbol_length);
    for (int i = 0; i < enc->extended_length; i++) {
        enc->temp[i].re *= scale;
        enc->temp[i].im *= scale;
    }
}

static int enc_bin(rg_encoder_t *enc, int carrier) {
    return (carrier + enc->carrier_offset + enc->symbol_length) % enc->symbol_length;
}

static void rg_enc_noise(rg_encoder_t *enc) {
    int sl = enc->symbol_length;
    float factor = sqrtf(sl / 256.0f);
    rg_mls_t seq; rg_mls_init(&seq, 0x15a1, 1);
    memset(enc->freq, 0, sl * sizeof(rg_cplx_t));
    for (int i = 0; i < 256; i++) {
        int c = nrz(rg_mls_next(&seq));
        int s = nrz(rg_mls_next(&seq));
        enc->freq[enc_bin(enc, i - 128)] = rg_cmk(c * factor, s * factor);
    }
    transform(enc);
}

static void rg_enc_sc_sync(rg_encoder_t *enc) {
    int sl = enc->symbol_length;
    rg_mls_t seq; rg_mls_init(&seq, 0x89, 1);
    float factor = sqrtf(2.0f * sl / 127);
    memset(enc->freq, 0, sl * sizeof(rg_cplx_t));
    enc->freq[enc_bin(enc, -128)] = rg_cf(factor);
    for (int i = 0; i < 127; i++)
        enc->freq[enc_bin(enc, 2*i - 126)] = rg_cf(rg_mls_nrz(&seq));
    for (int i = 0; i < 127; i++)
        enc->freq[enc_bin(enc, 2*i - 126)] = rg_cmul(
            enc->freq[enc_bin(enc, 2*i - 126)],
            enc->freq[enc_bin(enc, 2*(i-1) - 126)]);
    transform(enc);
}

static void rg_enc_preamble(rg_encoder_t *enc) {
    int sl = enc->symbol_length;
    enc->skip_papr = 1;
    uint8_t data[9] = {0};
    uint8_t parity[23] = {0};
    /* Pack metadata MSB-first (matching C++ reference):
     * bits 0-7: operation_mode, bits 8-54: callsign */
    for (int i = 0; i < 55; i++)
        data[i/8] |= ((enc->meta_data >> (54 - i)) & 1) << (7 - (i % 8));
    /* CRC-16 over metadata << 9, matching C++ CRC<uint16_t> byte-by-byte */
    uint16_t cs = rg_crc16_compute(enc->meta_data << 9);
    for (int i = 0; i < 16; i++)
        data[(55+i)/8] |= ((cs >> (15-i)) & 1) << (7 - ((55+i) % 8));
    rg_bch_encode(data, parity);
    rg_mls_t seq; rg_mls_init(&seq, 0x12B, 1);
    float factor = sqrtf(sl / 255.0f);
    memset(enc->freq, 0, sl * sizeof(rg_cplx_t));
    /* Pilot carrier at pre_seq_off - 1 = -128 */
    enc->freq[enc_bin(enc, -128)] = rg_cf(factor);
    for (int i = 0; i < 255; i++) {
        int bit;
        if (i < 71) bit = (data[i/8] >> (i%8)) & 1;
        else bit = (parity[(i-71)/8] >> ((i-71)%8)) & 1;
        enc->freq[enc_bin(enc, i - 127)] = rg_cf(nrz(bit));
    }
    /* Differential encoding */
    for (int i = 0; i < 255; i++)
        enc->freq[enc_bin(enc, i - 127)] = rg_cmul(
            enc->freq[enc_bin(enc, i - 127)],
            enc->freq[enc_bin(enc, (i-1) - 127)]);
    /* MLS spreading */
    for (int i = 0; i < 255; i++)
        enc->freq[enc_bin(enc, i - 127)] = rg_cmul(
            enc->freq[enc_bin(enc, i - 127)], rg_cf(rg_mls_nrz(&seq)));
    for (int i = 0; i < 256; i++)
        enc->prev[i] = enc->freq[enc_bin(enc, i - 128)];
    transform(enc);
}

static void rg_enc_payload(rg_encoder_t *enc) {
    int sl = enc->symbol_length;
    int sym = enc->symbol_number;
    memset(enc->freq, 0, sl * sizeof(rg_cplx_t));
    for (int i = 0; i < 256; i++) {
        int ci = sym * 512 + i * 2;
        float r = (enc->code[ci] >= 0) ? 0.70710678f : -0.70710678f;
        float im = (enc->code[ci+1] >= 0) ? 0.70710678f : -0.70710678f;
        rg_cplx_t m = rg_cmk(r, im);
        enc->prev[i] = rg_cmul(enc->prev[i], m);
        enc->freq[enc_bin(enc, i - 128)] = enc->prev[i];
    }
    transform(enc);
}

static void rg_enc_silence(rg_encoder_t *enc) {
    memset(enc->temp, 0, enc->extended_length * sizeof(rg_cplx_t));
}

static void rg_enc_output(rg_encoder_t *enc, int16_t *buf, int data_symbol) {
    int gl = enc->guard_length;
    int sl = enc->symbol_length;
    for (int i = 0; i < gl; i++) {
        float x = (float)i / (float)(gl - 1);
        float ratio = data_symbol ? 0.5f : 0.0f;
        if (ratio > 0 && x > ratio) x = ratio / ratio;
        else if (ratio > 0) x = x / ratio;
        else x = 0;
        float y = 0.5f * (1.0f - cosf(M_PI * x));
        rg_cplx_t g = enc->guard[i];
        rg_cplx_t t = enc->temp[i + sl - gl];
        float re = (1-y)*g.re + y*t.re;
        float im = (1-y)*g.im + y*t.im;
        int sv = (int)roundf(re * 32767.0f);
        buf[i] = sv > 32767 ? 32767 : (sv < -32768 ? -32768 : sv);
    }
    for (int i = 0; i < gl; i++) enc->guard[i] = enc->temp[i];
    for (int i = 0; i < sl; i++) {
        int sv = (int)roundf(enc->temp[i].re * 32767.0f);
        buf[gl+i] = sv > 32767 ? 32767 : (sv < -32768 ? -32768 : sv);
    }
}

/* ---- Public API ---- */

int rg_encoder_symbol_length(rg_encoder_t *enc) { return enc->symbol_length; }
int rg_encoder_extended_length(rg_encoder_t *enc) { return enc->extended_length; }

rg_encoder_t *rg_encoder_create(int sample_rate) {
    rg_encoder_t *enc = calloc(1, sizeof(*enc));
    if (!enc) return NULL;
    enc->rate = sample_rate;
    enc->symbol_length = (1280 * sample_rate) / 8000;
    enc->guard_length = enc->symbol_length / 8;
    enc->extended_length = enc->symbol_length + enc->guard_length;
    enc->carrier_offset = 0;
    enc->fft = rg_fft_create(enc->symbol_length);
    enc->freq = calloc(enc->symbol_length, sizeof(rg_cplx_t));
    enc->temp = calloc(enc->extended_length, sizeof(rg_cplx_t));
    enc->prev = calloc(256, sizeof(rg_cplx_t));
    enc->guard = calloc(enc->guard_length, sizeof(rg_cplx_t));
    enc->papr_fact = (32000 + sample_rate/2) / sample_rate;
    enc->papr_freq = calloc(enc->papr_fact * enc->symbol_length, sizeof(rg_cplx_t));
    return enc;
}

void rg_encoder_configure(rg_encoder_t *enc,
    const uint8_t *payload, int payload_len,
    const char *callsign, int carrier_freq,
    int noise_symbols, int fancy_header) {
    int len = payload_len;
    if (len <= 0) { enc->mode = 0; enc->data_bits = 0; }
    else if (len <= 85) { enc->mode = 16; enc->data_bits = 680; }
    else if (len <= 128) { enc->mode = 15; enc->data_bits = 1024; }
    else { enc->mode = 14; enc->data_bits = 1360; }
    
    memcpy(enc->payload, payload, payload_len < 170 ? payload_len : 170);
    rg_xorshift32_t xs; rg_xorshift32_init(&xs); xs.y = 2463534242u; for (int j = 0; j < len; j++) enc->payload[j] ^= (uint8_t)rg_xorshift32_next(&xs);
    
    const uint32_t *frozen;
    if (enc->mode == 14) frozen = rg_frozen_2048_1392;
    else if (enc->mode == 15) frozen = rg_frozen_2048_1056;
    else frozen = rg_frozen_2048_712;
    rg_polar_encode(enc->code, enc->payload, frozen, enc->data_bits);
    
    for (int i = 0; i < 9; i++) enc->call[i] = 0;
    for (int i = 0; i < 9 && callsign[i]; i++) enc->call[i] = base37_map(callsign[i]);
    enc->meta_data = (base37(callsign) << 8) | enc->mode;
    enc->carrier_offset = (carrier_freq * enc->symbol_length) / enc->rate;
    enc->noise_count = noise_symbols;
    enc->symbol_number = 0;
    int total = noise_symbols + 1 + 1 + 4 + 1;
    enc->count_down = 5;
}

int rg_encoder_produce(rg_encoder_t *enc, int16_t *buf) {
    int data_symbol = 0;
    if (enc->count_down <= 0) {
        memset(buf, 0, enc->extended_length * sizeof(int16_t));
        return 0;
    }
    switch (enc->count_down) {
        case 5: /* Noise symbols */
            if (enc->noise_count) {
                enc->noise_count--;
                rg_enc_noise(enc);
                break;
            }
            enc->count_down--;
            /* FALLTHROUGH to case 4 */
        case 4: /* SC sync */
            rg_enc_sc_sync(enc);
            data_symbol = 1;
            enc->count_down--;
            break;
        case 3: /* Preamble */
            rg_enc_preamble(enc);
            data_symbol = 1;
            enc->count_down--;
            break;
        case 2: /* Payload symbols */
            rg_enc_payload(enc);
            enc->symbol_number++;
            data_symbol = 1;
            if (enc->symbol_number >= 4)
                enc->count_down--;
            break;
        case 1: /* Silence */
            rg_enc_silence(enc);
            enc->count_down--;
            break;
        default:
            memset(buf, 0, enc->extended_length * sizeof(int16_t));
            return 0;
    }
    rg_enc_output(enc, buf, data_symbol);
    return 1;
}

void rg_encoder_free(rg_encoder_t *enc) {
    if (!enc) return;
    free(enc->papr_freq); free(enc->guard); free(enc->prev);
    free(enc->temp); free(enc->freq);
    rg_fft_free(enc->fft); free(enc);
}
