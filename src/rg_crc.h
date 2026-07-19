/* rg_crc.h - CRC implementations for Rattlegram */

#ifndef RG_CRC_H
#define RG_CRC_H

#include <stdint.h>

/* CRC-16 with polynomial 0xA8F4 (used for metadata CRC) */
typedef struct {
    uint16_t crc;
} rg_crc16_t;

void rg_crc16_init(rg_crc16_t *crc);
uint16_t rg_crc16_update_bit(rg_crc16_t *crc, int bit);
uint16_t rg_crc16_update(rg_crc16_t *crc, uint64_t data);
uint16_t rg_crc16_value(rg_crc16_t *crc);
uint16_t rg_crc16_compute(uint64_t data); /* Single-call CRC (init + update + value) */

/* CRC-32 with polynomial 0x8F6E37A0 (used for Polar code CRC) */
typedef struct {
    uint32_t crc;
} rg_crc32_t;

void rg_crc32_init(rg_crc32_t *crc);
uint32_t rg_crc32_update_bit(rg_crc32_t *crc, int bit);
uint32_t rg_crc32_update_byte(rg_crc32_t *crc, uint8_t data);
uint32_t rg_crc32_value(rg_crc32_t *crc);

#endif /* RG_CRC_H */
