#ifndef RG_ENCODER_H
#define RG_ENCODER_H

#include <stdint.h>

/* Rattlegram encoder */
typedef struct rg_encoder_s rg_encoder_t;

rg_encoder_t *rg_encoder_create(int sample_rate);
void rg_encoder_configure(rg_encoder_t *enc,
                          const uint8_t *payload, int payload_len,
                          const char *callsign, int carrier_freq,
                          int noise_symbols, int fancy_header);
int rg_encoder_produce(rg_encoder_t *enc, int16_t *buf);
extern int rg_encoder_symbol_length(rg_encoder_t *);
extern int rg_encoder_extended_length(rg_encoder_t *);
void rg_encoder_free(rg_encoder_t *enc);

#endif
