/* demod_rattlegram.h - Rattlegram modem for Dire Wolf */

#ifndef DEMOD_RATTLEGRAM_H
#define DEMOD_RATTLEGRAM_H

#include <stdint.h>
#include "fsk_demod_state.h"

/* Decoder */
int demod_rattlegram_init(int chan, int subchan, int samples_per_sec,
                          int baud, struct demodulator_state_s *D);
void demod_rattlegram_process_sample(int chan, int subchan, int sam,
                                     struct demodulator_state_s *D);
void demod_rattlegram_free(int chan, int subchan);

#endif /* DEMOD_RATTLEGRAM_H */

