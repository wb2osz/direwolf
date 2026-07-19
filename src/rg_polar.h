/* rg_polar.h - Polar code encoder and SCL decoder for Rattlegram */

#ifndef RG_POLAR_H
#define RG_POLAR_H

#include <stdint.h>

/* Polar code: length 2048 (order 11) */
/* Encoder: systematic, with CRC-32 */
/* data: input message bytes (data_bits/8 bytes) */
/* code: output codeword (2048 int8_t values, +1/-1) */
/* frozen: frozen bit table (64 uint32_t values, bit mask) */
/* data_bits: number of data bits (excluding CRC-32) */
void rg_polar_encode(int8_t *code, const uint8_t *data,
                     const uint32_t *frozen, int data_bits);

/* SCL decoder: list size 4 */
/* code: input codeword soft values (2048 int8_t) */
/* msg: output message bytes */
/* frozen: frozen bit table */
/* data_bits: number of data bits (excluding CRC-32) */
/* Returns >= 0 on success (number of bit flips vs received), -1 on failure */
int rg_polar_decode(uint8_t *msg, const int8_t *code,
                    const uint32_t *frozen, int data_bits);

#endif /* RG_POLAR_H */
