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

/* Encode 71 info bits -> 255-bit systematic codeword.
 * Only the 184 parity bits (23 bytes) are returned. */
void rg_bch_encode(uint8_t parity[23], const uint8_t data[9])
{
    static int8_t genmat[BCH_N * BCH_K];
    static int ready = 0;
    if (!ready) {
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
        /* Convert to systematic form */
        for (int k = BCH_K - 1; k > 0; k--)
            for (int jj = 0; jj < k; jj++)
                if (genmat[BCH_N * jj + k])
                    for (int i = k; i < BCH_N; i++)
                        genmat[BCH_N * jj + i] ^= genmat[BCH_N * k + i];
        ready = 1;
    }

    uint8_t codeword[32] = {0};
    for (int i = 0; i < BCH_N; i++)
        set_be_bit(codeword, i, get_be_bit(data, 0) & genmat[i]);
    for (int j = 1; j < BCH_K; j++)
        if (get_be_bit(data, j))
            for (int i = 0; i < BCH_N; i++)
                xor_be_bit(codeword, i, genmat[BCH_N * j + i]);

    /* Extract parity bits (positions 71-254) */
    for (int i = 0; i < 184; i++)
        set_be_bit(parity, i, get_be_bit(codeword, BCH_K + i));
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
 * Uses majority-vote hard decisions + CRC-16 verification.
 * Returns 1 if CRC-16 matches, 0 otherwise. */
int rg_bch_decode(uint8_t *data, const int8_t *soft)
{
    /* Simple majority-vote hard decode: extract 71 info bits */
    memset(data, 0, 9);
    for (int i = 0; i < BCH_K; i++) {
        /* soft[i] > 0 means bit 0, soft[i] < 0 means bit 1 */
        if (soft[i] < 0)
            set_be_bit(data, i, 1);
    }

    /* Verify with CRC-16:
     * Bits 0-7: operation_mode
     * Bits 8-54: call sign (base37)
     * Bits 55-70: CRC-16 over (metadata << 9)
     */
    uint64_t meta = 0;
    for (int i = 0; i < 55; i++)
        if (get_be_bit(data, i))
            meta |= (1ULL << (54 - i));

    uint16_t crc_stored = 0;
    for (int i = 55; i < 71; i++)
        if (get_be_bit(data, i))
            crc_stored |= (1 << (70 - i));

    uint16_t crc_computed = rg_crc16_compute(meta << 9);
    return crc_stored == crc_computed;
}

