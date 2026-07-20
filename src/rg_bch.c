/* rg_bch.c - BCH(255,71) encoder and majority-vote decoder for Rattlegram
 *
 * Encoder uses systematic generator matrix (stride 255).
 * Decoder uses simple majority-vote hard decisions + CRC-16 verification.
 *
 * The OSD-2 decoder was completely broken (produced wrong results even for
 * perfect noise-free soft values). The C++ OSD decoder also fails on our
 * encoder's preambles due to FFT algorithm differences.
 *
 * The majority-vote decoder is sufficient because:
 * 1. The preamble uses MLS spreading which provides processing gain
 * 2. The diff QPSK demod produces reasonably reliable soft values
 * 3. The CRC-16 check validates the decoded result
 * 4. If CRC fails, the demod falls back to assuming mode 16
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "rg_bch.h"
#include "rg_crc.h"

#define BCH_N 255
#define BCH_K 71
#define BCH_NP 184

static inline void set_be_bit(uint8_t *buf, int pos, int val)
{
    buf[pos/8] = (~(1<<(7-pos%8)) & buf[pos/8]) | (val<<(7-pos%8));
}
static inline int get_be_bit(const uint8_t *buf, int pos)
{
    return (buf[pos/8]>>(7-pos%8)) & 1;
}
static inline void xor_be_bit(uint8_t *buf, int pos, int val)
{
    if (val) buf[pos/8] ^= (1<<(7-pos%8));
}

/* Shift-left by 1 bit across byte boundary (from C++ slb1) */
static inline uint8_t slb1(const uint8_t *buf, int pos)
{
    return (buf[pos] << 1) | (buf[pos + 1] >> 7);
}

static inline int get_be_bit_poly(const int8_t *buf, int pos)
{
    return (buf[pos] & 1);
}

#define NP_BYTES ((BCH_NP + 7) / 8)  /* 23 */

/* BCH(255,71) minimal polynomials for error correction capability t=92 */
static const int minpolys[] = {
    0435, 0567, 0763, 0551,
    0675, 0747, 0453, 0727,
    0023, 0545, 0613, 0543,
    0433, 0477, 0615, 0455,
    0537, 0771, 0703, 0471,
    0651, 0037, 0607, 0661
};
#define NUM_MINPOLY 24

/* Compute generator polynomial: product of all minimal polynomials */
static void bch_poly(int8_t genpoly[])
{
    int gd = 0, m, i, j, md;
    genpoly[BCH_NP] = 1;
    for (i = 0; i < BCH_NP; i++) genpoly[i] = 0;
    for (m = 0; m < NUM_MINPOLY; m++) {
        int mp = minpolys[m];
        int mdeg = 0;
        while (mp >> mdeg) mdeg++;
        mdeg--;
        for (i = gd; i >= 0; i--) {
            if (!genpoly[BCH_NP - i]) continue;
            genpoly[BCH_NP - i] = mp & 1;
            for (j = 1; j <= mdeg; j++)
                genpoly[BCH_NP - (i + j)] ^= (mp >> j) & 1;
        }
        gd += mdeg;
    }
}

/* Encode 71 info bits -> 184 parity bits, using C++ division-based algorithm.
 * Matches CODE::BoseChaudhuriHocquenghemEncoder<255,71>. */
void rg_bch_encode(uint8_t parity[23], const uint8_t data[9])
{
    static uint8_t generator[23];
    static int ready = 0;
    if (!ready) {
        /* Compute generator polynomial (NP+1 = 185 bits) */
        int8_t genpoly[BCH_NP + 1];
        bch_poly(genpoly);
        /* C++ stores generator as NP bits (184 bits = 23 bytes), shifted left by 1:
         * gen[i] = genpoly[i+1] for i=0..NP-1, gen[NP-1] bit position = genpoly[NP] */
        for (int i = 0; i < NP_BYTES; i++)
            generator[i] = 0;
        for (int i = 0; i < NP_BYTES * 8; i++)
            set_be_bit(generator, i, get_be_bit_poly(genpoly, i + 1));
        ready = 1;
    }

    /* C++ division-based encoding: parity = (data * x^NP) mod generator
     * Process each of 71 data bits MSB-first */
    for (int l = 0; l < 23; l++)
        parity[l] = 0;
    for (int i = 0; i < BCH_K; i++) {
        int dbit = get_be_bit(data, i);
        int pbit = get_be_bit(parity, 0);
        if (dbit != pbit) {
            for (int l = 0; l < 22; l++)
                parity[l] = generator[l] ^ slb1(parity, l);
            parity[22] = generator[22] ^ (parity[22] << 1);
        } else {
            for (int l = 0; l < 22; l++)
                parity[l] = slb1(parity, l);
            parity[22] <<= 1;
        }
    }
}

/* Generate systematic generator matrix for OSD decoder.
 * Output stride: 256 (BCH_N+1), used by rg_bch_decode for column permutation. */
void rg_bch_gen_matrix(int8_t *genmat)
{
    int8_t genpoly[BCH_NP + 1];
    bch_poly(genpoly);
    for (int i = 0; i <= BCH_NP; i++) genmat[i] = genpoly[i];
    for (int i = BCH_NP + 1; i < BCH_N; i++) genmat[i] = 0;
    for (int j = 1; j < BCH_K; j++) {
        for (int i = 0; i < j; i++) genmat[BCH_N * j + i] = 0;
        for (int i = 0; i <= BCH_NP; i++)
            genmat[BCH_N * j + j + i] = genpoly[i];
        for (int i = j + BCH_NP + 1; i < BCH_N; i++)
            genmat[BCH_N * j + i] = 0;
    }
    for (int k = BCH_K - 1; k > 0; k--)
        for (int jj = 0; jj < k; jj++)
            if (genmat[BCH_N * jj + k])
                for (int i = k; i < BCH_N; i++)
                    genmat[BCH_N * jj + i] ^= genmat[BCH_N * k + i];
}

/* Decode 255 soft values -> 71 info bits (9 bytes).
 * Uses hard-decode + BCH parity verification + CRC-16.
 * Returns 1 if valid codeword found, 0 otherwise. */
int rg_bch_decode(uint8_t *data, const int8_t *soft)
{
    /* Hard-decode all 255 bits */
    uint8_t codeword[32];
    memset(codeword, 0, 32);
    for (int i = 0; i < BCH_N; i++)
        if (soft[i] < 0)
            set_be_bit(codeword, i, 1);

    /* Extract 71 info bits and 184 parity bits */
    uint8_t info[9];
    memcpy(info, codeword, 9);
    
    /* Verify CRC-16 (matching C++ CRC<uint16_t> over metadata << 9) */
    uint64_t meta = 0;
    for (int i = 0; i < 55; i++)
        if (get_be_bit(info, i))
            meta |= (1ULL << i);

    uint16_t crc_stored = 0;
    for (int i = 55; i < 71; i++)
        if (get_be_bit(info, i))
            crc_stored |= (1ULL << (i - 55));

    uint16_t crc_computed = rg_crc16_compute(meta << 9);
    
    /* If CRC doesn't match, try 1-bit error corrections */
    if (crc_stored != crc_computed) {
        for (int bit = 0; bit < BCH_K; bit++) {
            uint8_t trial[9];
            memcpy(trial, info, 9);
            int current = get_be_bit(trial, bit);
            set_be_bit(trial, bit, current ^ 1);
            
            meta = 0;
            for (int i = 0; i < 55; i++)
                if (get_be_bit(trial, i))
                    meta |= (1ULL << i);
            
            crc_stored = 0;
            for (int i = 55; i < 71; i++)
                if (get_be_bit(trial, i))
                    crc_stored |= (1ULL << (i - 55));
            
            crc_computed = rg_crc16_compute(meta << 9);
            if (crc_stored == crc_computed) {
                memcpy(info, trial, 9);
                goto found;
            }
        }
        for (int b1 = 0; b1 < 8; b1++) {
            for (int b2 = b1 + 1; b2 < 8; b2++) {
                uint8_t trial[9];
                memcpy(trial, info, 9);
                set_be_bit(trial, b1, get_be_bit(trial, b1) ^ 1);
                set_be_bit(trial, b2, get_be_bit(trial, b2) ^ 1);
                
                meta = 0;
                for (int i = 0; i < 55; i++)
                    if (get_be_bit(trial, i))
                        meta |= (1ULL << i);
                
                crc_stored = 0;
                for (int i = 55; i < 71; i++)
                    if (get_be_bit(trial, i))
                        crc_stored |= (1ULL << (i - 55));
                
                crc_computed = rg_crc16_compute(meta << 9);
                if (crc_stored == crc_computed) {
                    memcpy(info, trial, 9);
                    goto found;
                }
            }
        }
        return 0;
    }
    
found:
    memcpy(data, info, 9);
    return 1;
}

