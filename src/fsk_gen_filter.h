

#ifndef FSK_GEN_FILTER_H
#define FSK_GEN_FILTER_H 1

#include "audio.h"
#include "fsk_demod_state.h"

void fsk_gen_filter (int samples_per_sec, 
			int baud, 
			int mark_freq, int space_freq, 
			char profile,
			struct demodulator_state_s *D);

#endif