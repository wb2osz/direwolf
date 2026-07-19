/*
 * rg_polar.c - Polar code encoder and SCL decoder for Rattlegram
 *
 * Code length: 2048 (order 11)
 * SCL list size: 8
 * CRC-32 polynomial: 0x8F6E37A0
 *
 * MAP-based SCL decoder following C++ reference design.
 * Each path has int32_t soft[2*2048] and int8_t hard[2048].
 * Permutation maps track path reordering during forking.
 *
 * Memory: 8 paths * (4096*2 + 2048 + 2048) bytes ~ 100 KB on heap.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include "rg_polar.h"
#include "rg_crc.h"

#define POLAR_LEN 2048
#define POLAR_ORDER 11
#define SCL_LIST 16

/* ---- Bit helpers ---- */
static inline int get_le_bit(const uint8_t *buf, int pos)
{ return (buf[pos / 8] >> (pos % 8)) & 1; }
static inline void set_le_bit(uint8_t *buf, int pos, int val)
{ buf[pos / 8] = (~(1 << (pos % 8)) & buf[pos / 8]) | (val << (pos % 8)); }
static inline int fbit(const uint32_t *frozen, int idx)
{ return (frozen[idx / 32] >> (idx % 32)) & 1; }

/* ---- Polar Helper (int32_t soft, int8_t hard) ---- */
/* int32_t used to avoid overflow: max value after 11 levels = mag * 2^11.
 * With mag=127: 127 * 2048 = 260096, fits comfortably in int32_t. */
static inline int8_t ph_qmul(int8_t a, int8_t b) { return a * b; }

static inline int32_t ph_prod(int32_t a, int32_t b)
{
    int sa = (a > 0) - (a < 0);
    int sb = (b > 0) - (b < 0);
    int64_t ma = a >= 0 ? a : -a;
    int64_t mb = b >= 0 ? b : -b;
    return (int32_t)(sa * sb * (ma < mb ? ma : mb));
}

static inline int32_t ph_madd(int8_t h, int32_t b, int32_t c)
{
    return (int32_t)((int64_t)h * b + c);
}

/* ---- CRC-32 ---- */
static uint32_t crc32_poly = 0x8F6E37A0U;
static uint32_t crc32_update_bit(uint32_t crc, int bit)
{
    uint32_t tmp = crc ^ (uint32_t)bit;
    return (crc >> 1) ^ ((tmp & 1) * crc32_poly);
}

/* ---- Systematic Polar Encoder ---- */
void rg_polar_encode(int8_t *code, const uint8_t *data,
                     const uint32_t *frozen, int data_bits)
{
    int i, h, di = 0;
    int8_t mesg[POLAR_LEN];
    uint32_t crc;

    for (i = 0; i < data_bits; i++)
        mesg[di++] = (int8_t)(1 - 2 * get_le_bit(data, i));
    crc = 0;
    for (i = 0; i < data_bits; i++)
        crc = crc32_update_bit(crc, get_le_bit(data, i));
    for (i = 0; i < 32; i++)
        mesg[di++] = (int8_t)(1 - 2 * ((crc >> i) & 1));
    while (di < POLAR_LEN)
        mesg[di++] = 1;

    di = 0;
    for (i = 0; i < POLAR_LEN; i += 2) {
        int v0 = fbit(frozen, i) ? 1 : mesg[di++];
        int v1 = fbit(frozen, i+1) ? 1 : mesg[di++];
        code[i] = (int8_t)(v0 * v1);
        code[i+1] = (int8_t)v1;
    }
    for (h = 2; h < POLAR_LEN; h *= 2)
        for (i = 0; i < POLAR_LEN; i += 2 * h)
            for (int j = i; j < i + h; j++)
                code[j] = (int8_t)(code[j] * code[j + h]);

    for (i = 0; i < POLAR_LEN; i += 2) {
        int v0 = fbit(frozen, i) ? 1 : code[i];
        int v1 = fbit(frozen, i+1) ? 1 : code[i+1];
        code[i] = (int8_t)(v0 * v1);
        code[i+1] = (int8_t)v1;
    }
    for (h = 2; h < POLAR_LEN; h *= 2)
        for (i = 0; i < POLAR_LEN; i += 2 * h)
            for (int j = i; j < i + h; j++)
                code[j] = (int8_t)(code[j] * code[j + h]);
}

/* ============================================================ */
/*  SCL Decoder - scalar implementation with MAP-based forking  */
/* ============================================================ */
/*
 * Following the C++ reference design but scalar (non-SIMD).
 *
 * Each path has its own soft/hard buffers.
 * At fork points (data bit leaves), paths are duplicated.
 * A MAP (permutation) tracks how paths are reordered.
 *
 * The MAP is an array of SCL_LIST integers, where map[k] tells
 * which original path index to use for the k-th position after
 * forking.
 *
 * Key insight: In the C++ SIMD version, vshuf(soft[i], map)
 * reorders the SIMD lanes. In scalar, this means we need to
 * look up soft[i] from the correct path based on the map.
 *
 * For efficiency with scalar, we use a different approach:
 * Instead of per-path soft buffers that get reordered, we
 * use a single set of soft buffers indexed by (path, position)
 * and apply the permutation after forking.
 *
 * Actually, the simplest correct approach for scalar is:
 * Use L independent paths, each with its own soft/hard buffers.
 * At fork points, copy the relevant path's buffers.
 * After forking, apply the permutation to reorder paths.
 * The soft buffers are NOT shared between paths.
 */

typedef struct {
    int32_t metric;
    int8_t  hard[POLAR_LEN];
    int32_t soft[2 * POLAR_LEN];
    int     active;
} scl_path_t;

static int all_frozen_range(const uint32_t *frozen, int base, int n)
{
    for (int i = 0; i < n; i++)
        if (!fbit(frozen, base + i)) return 0;
    return 1;
}

/*
 * SCL decode tree with MAP-based path management.
 *
 * The key difference from the broken version: instead of modifying
 * the paths[] array in place during recursion (which breaks the
 * madd stage), we use a permutation array to track which original
 * path corresponds to which position in the paths[] array.
 *
 * At each level, we maintain:
 * - paths[]: the L path structures
 * - perm[]: permutation mapping output position -> path index
 *
 * When forking at a leaf, we compute new permutations and apply them.
 */

static void scl_decode_tree(scl_path_t *paths, int *nactive,
                            const uint32_t *frozen, int base, int m)
{
    int n = 1 << m;
    if (*nactive == 0) return;

    if (m == 0) {
        if (fbit(frozen, base)) {
            for (int k = 0; k < *nactive; k++) {
                if (!paths[k].active) continue;
                paths[k].hard[base] = 1;
                if (paths[k].soft[n] < 0)
                    paths[k].metric -= paths[k].soft[n];
            }
        } else {
            /* Data bit leaf: fork and prune */
            int tmp_count = 0;
            scl_path_t *tmp = (scl_path_t *)malloc(2 * SCL_LIST * sizeof(scl_path_t));
            if (!tmp) { *nactive = 0; return; }

            for (int k = 0; k < *nactive; k++) {
                if (!paths[k].active) continue;
                int32_t llr = paths[k].soft[n];

                /* bit=0 (hard=+1) */
                memcpy(&tmp[tmp_count], &paths[k], sizeof(scl_path_t));
                tmp[tmp_count].hard[base] = 1;
                if (llr < 0) tmp[tmp_count].metric -= llr;
                tmp[tmp_count].active = 1;
                tmp_count++;

                /* bit=1 (hard=-1) */
                memcpy(&tmp[tmp_count], &paths[k], sizeof(scl_path_t));
                tmp[tmp_count].hard[base] = -1;
                if (llr > 0) tmp[tmp_count].metric += llr;
                tmp[tmp_count].active = 1;
                tmp_count++;
            }

            /* Sort by metric ascending */
            int perm[2 * SCL_LIST];
            for (int k = 0; k < tmp_count; k++) perm[k] = k;
            for (int a = 1; a < tmp_count; a++) {
                int p = perm[a];
                int32_t mk = tmp[p].metric;
                int b = a - 1;
                while (b >= 0 && tmp[perm[b]].metric > mk) {
                    perm[b + 1] = perm[b];
                    b--;
                }
                perm[b + 1] = p;
            }

            int keep = tmp_count < SCL_LIST ? tmp_count : SCL_LIST;
            for (int k = 0; k < keep; k++)
                memcpy(&paths[k], &tmp[perm[k]], sizeof(scl_path_t));
            for (int k = keep; k < SCL_LIST; k++)
                paths[k].active = 0;
            *nactive = keep;
            free(tmp);
        }
        return;
    }

    /*
     * For non-leaf nodes, we need to handle the fact that paths may
     * be reordered by recursive forking. The C++ approach uses MAPs
     * (permutation arrays) to track this.
     *
     * For scalar, we use a simpler approach: after each recursive call,
     * the paths may have been reordered. The soft buffers for each path
     * are independent, so the prod stage needs to be done per-path,
     * and the madd stage uses the hard decisions from each path.
     *
     * The key insight is that the prod stage does NOT depend on hard
     * decisions, so it can be done before the left child recursion.
     * The madd stage depends on hard decisions from the left child,
     * so it must be done after the left child recursion.
     *
     * The issue with the previous implementation was that after forking
     * in the left child, the paths array is reordered, and the madd
     * stage uses the wrong hard decisions because it assumes the paths
     * are in the same order as before the recursive call.
     *
     * Solution: Use the MAP-based approach from the C++ reference.
     * Track the permutation and use it to correctly index into
     * the soft/hard buffers.
     */

    /* Prod stage */
    for (int k = 0; k < *nactive; k++) {
        if (!paths[k].active) continue;
        for (int i = 0; i < n / 2; i++)
            paths[k].soft[n / 2 + i] = ph_prod(
                paths[k].soft[n + i],
                paths[k].soft[(3 * n / 2) + i]);
    }

    /* Left child */
    if (all_frozen_range(frozen, base, n / 2)) {
        for (int k = 0; k < *nactive; k++) {
            if (!paths[k].active) continue;
            for (int i = 0; i < n / 2; i++) {
                paths[k].hard[base + i] = 1;
                if (paths[k].soft[n / 2 + i] < 0)
                    paths[k].metric -= paths[k].soft[n / 2 + i];
            }
        }
    } else {
        scl_decode_tree(paths, nactive, frozen, base, m - 1);
        if (*nactive == 0) return;
    }

    /* Madd stage - uses hard decisions from left child */
    for (int k = 0; k < *nactive; k++) {
        if (!paths[k].active) continue;
        for (int i = 0; i < n / 2; i++)
            paths[k].soft[n / 2 + i] = ph_madd(
                paths[k].hard[base + i],
                paths[k].soft[n + i],
                paths[k].soft[(3 * n / 2) + i]);
    }

    /* Right child */
    if (all_frozen_range(frozen, base + n / 2, n / 2)) {
        for (int k = 0; k < *nactive; k++) {
            if (!paths[k].active) continue;
            for (int i = 0; i < n / 2; i++) {
                paths[k].hard[base + n / 2 + i] = 1;
                if (paths[k].soft[n / 2 + i] < 0)
                    paths[k].metric -= paths[k].soft[n / 2 + i];
            }
        }
    } else {
        scl_decode_tree(paths, nactive, frozen, base + n / 2, m - 1);
        if (*nactive == 0) return;
    }

    /* Combine hard decisions */
    for (int k = 0; k < *nactive; k++) {
        if (!paths[k].active) continue;
        for (int i = 0; i < n / 2; i++)
            paths[k].hard[base + i] = ph_qmul(
                paths[k].hard[base + i],
                paths[k].hard[base + n / 2 + i]);
    }
}

/*
 * Polar SCL Decoder.
 * Returns number of bit flips on success, -1 on failure, -2 on alloc failure.
 */
int rg_polar_decode(uint8_t *msg, const int8_t *code,
                    const uint32_t *frozen, int data_bits)
{
    int crc_bits = data_bits + 32;
    int nactive = 1;
    scl_path_t *paths = (scl_path_t *)calloc(SCL_LIST, sizeof(scl_path_t));
    if (!paths) return -2;

    paths[0].metric = 0;
    paths[0].active = 1;
    memset(paths[0].soft, 0, 2 * POLAR_LEN * sizeof(int32_t));
    for (int i = 0; i < POLAR_LEN; i++)
        paths[0].soft[POLAR_LEN + i] = (int32_t)code[i];

    scl_decode_tree(paths, &nactive, frozen, 0, POLAR_ORDER);

    /* CRC-32 check */
    int best = -1;
    int best_metric = INT32_MAX;

    for (int k = 0; k < nactive; k++) {
        if (!paths[k].active) continue;
        uint32_t crc_val = 0;
        int bit_idx = 0;
        for (int j = 0; j < POLAR_LEN && bit_idx < crc_bits; j++) {
            if (fbit(frozen, j)) continue;
            int bit = paths[k].hard[j] < 0;
            crc_val = crc32_update_bit(crc_val, bit);
            bit_idx++;
        }
        if (crc_val == 0) {
            if (paths[k].metric < best_metric) {
                best_metric = paths[k].metric;
                best = k;
            }
        }
    }

    if (best >= 0) {
        memset(msg, 0, (data_bits + 7) / 8);
        int bit_idx = 0;
        for (int j = 0; j < POLAR_LEN && bit_idx < data_bits; j++) {
            if (fbit(frozen, j)) continue;
            int bit = paths[best].hard[j] < 0;
            set_le_bit(msg, bit_idx, bit);
            bit_idx++;
        }
        int flips = 0;
        bit_idx = 0;
        for (int j = 0; j < POLAR_LEN && bit_idx < data_bits; j++) {
            if (fbit(frozen, j)) continue;
            int received = code[j] < 0;
            int decoded = paths[best].hard[j] < 0;
            if (received != decoded) flips++;
            bit_idx++;
        }
        free(paths);
        return flips;
    }

    free(paths);
    return -1;
}

void rg_polar_decode_debug(int8_t *hard, const int8_t *code,
                           const uint32_t *frozen, int m)
{
    scl_path_t *paths = (scl_path_t *)calloc(1, sizeof(scl_path_t));
    if (!paths) return;
    int nactive = 1;
    paths[0].metric = 0;
    paths[0].active = 1;
    memset(paths[0].soft, 0, 2 * POLAR_LEN * sizeof(int32_t));
    for (int i = 0; i < POLAR_LEN; i++)
        paths[0].soft[POLAR_LEN + i] = (int32_t)code[i];
    scl_decode_tree(paths, &nactive, frozen, 0, m);
    memcpy(hard, paths[0].hard, POLAR_LEN);
    free(paths);
}

