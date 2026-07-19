/* rg_bch.h - BCH(255,71) encoder and OSD decoder for Rattlegram */

#ifndef RG_BCH_H
#define RG_BCH_H

#include <stdint.h>

/* BCH(255,71) encoder: 71 data bits -> 184 parity bits */
/* data: 9 bytes (71 bits, big-endian bit order, first 55 = metadata, next 16 = CRC) */
/* parity: 23 bytes (184 bits, big-endian bit order) */
void rg_bch_encode(uint8_t *parity, const uint8_t *data);

/* BCH(255,71) OSD-2 decoder */
/* soft: 255 int8_t soft values (positive = 0, negative = 1, magnitude = confidence) */
/* data: output 32 bytes (255 coded bits, big-endian bit order) */
/* Returns 1 on success (unique best candidate), 0 on failure */
int rg_bch_decode(uint8_t *data, const int8_t *soft);

/* Generate generator matrix for OSD decoder (called once at init) */
/* genmat: output buffer, 255*71 bytes */
void rg_bch_gen_matrix(int8_t *genmat);

#endif /* RG_BCH_H */
