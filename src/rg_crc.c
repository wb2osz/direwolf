/* rg_crc.c - CRC implementations for Rattlegram */

#include "rg_crc.h"

/* ---- CRC-16 (poly 0xA8F4) ---- */
/* This is a recursive bit-by-bit CRC. The poly is reflected/normal form.
 * Matching the C++ reference: CRC<16>(0xA8F4) uses bit-by-bit processing
 * with update(prev, data) = (prev >> 1) ^ ((tmp & 1) * poly) where tmp = prev ^ data */

static uint16_t crc16_update_bit_impl(uint16_t prev, int data)
{
    uint16_t tmp = prev ^ (uint16_t)data;
    return (prev >> 1) ^ ((tmp & 1) * (uint16_t)0xA8F4);
}

void rg_crc16_init(rg_crc16_t *crc)
{
    crc->crc = 0;
}

uint16_t rg_crc16_update_bit(rg_crc16_t *crc, int bit)
{
    return crc->crc = crc16_update_bit_impl(crc->crc, bit);
}

/* Process a uint64_t byte-by-byte (LSB byte first), matching C++ CRC template */
uint16_t rg_crc16_update(rg_crc16_t *crc, uint64_t data)
{
    /* Precompute lookup table (matches C++ CRC class construction) */
    static uint16_t lut[256];
    static int lut_ready = 0;
    if (!lut_ready) {
        for (int j = 0; j < 256; j++) {
            uint16_t tmp = j;
            for (int i = 8; i; i--)
                tmp = crc16_update_bit_impl(tmp, 0);
            lut[j] = tmp;
        }
        lut_ready = 1;
    }
    /* Process bytes MSB-first (byte 7 first), matching C++ operator()(uint64_t) */
    for (int i = 7; i >= 0; i--) {
        uint8_t byte = (uint8_t)((data >> ((uint64_t)i * 8)) & 255);
        uint16_t tmp = crc->crc ^ byte;
        crc->crc = (crc->crc >> 8) ^ lut[tmp & 255];
    }
    return crc->crc;
}

uint16_t rg_crc16_value(rg_crc16_t *crc)
{
    return crc->crc;
}

/* ---- CRC-32 (poly 0x8F6E37A0) ---- */

static uint32_t crc32_update_bit_impl(uint32_t prev, int data)
{
    uint32_t tmp = prev ^ (uint32_t)data;
    return (prev >> 1) ^ ((tmp & 1) * (uint32_t)0x8F6E37A0);
}

void rg_crc32_init(rg_crc32_t *crc)
{
    crc->crc = 0;
}

uint32_t rg_crc32_update_bit(rg_crc32_t *crc, int bit)
{
    return crc->crc = crc32_update_bit_impl(crc->crc, bit);
}

uint32_t rg_crc32_update_byte(rg_crc32_t *crc, uint8_t data)
{
    int i;
    for (i = 0; i < 8; i++) {
        crc->crc = crc32_update_bit_impl(crc->crc, (int)(data & 1));
        data >>= 1;
    }
    return crc->crc;
}

uint32_t rg_crc32_value(rg_crc32_t *crc)
{
    return crc->crc;
}

uint16_t rg_crc16_compute(uint64_t data)
{
    rg_crc16_t crc;
    rg_crc16_init(&crc);
    rg_crc16_update(&crc, data);
    return crc.crc;
}
