/* rg_sc.h - Schmidl-Cox synchronization correlator for Rattlegram */

#ifndef RG_SC_H
#define RG_SC_H

#include <stdint.h>
#include "rg_dsp.h"

typedef struct rg_sc_s rg_sc_t;

/* Create SC correlator.
 * symbol_len = symbol_length / 2 (half the OFDM symbol)
 * search_pos = extended_length
 * guard_len = guard_length
 * mls_seq = MLS NRZ sequence (127 values for SC sync)
 * cor_seq_len = length of MLS sequence (127) */
rg_sc_t *rg_sc_create(int symbol_len, int search_pos, int guard_len,
                      const int8_t *mls_seq, int cor_seq_len);
void rg_sc_free(rg_sc_t *sc);

/* Process one complex sample. Returns 1 on sync detection.
 * On success, use getters below. */
int rg_sc_process(rg_sc_t *sc, const rg_cplx_t *sample);

/* Getters */
int rg_sc_get_symbol_pos(rg_sc_t *sc);
float rg_sc_get_cfo_rad(rg_sc_t *sc);
int rg_sc_get_half_sym(rg_sc_t *sc);
int rg_sc_get_guard_len(rg_sc_t *sc);

#endif
