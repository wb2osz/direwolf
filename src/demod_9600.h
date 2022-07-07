

/* demod_9600.h */


#include "fsk_demod_state.h"


void demod_9600_init (enum modem_t modem_type, int original_sample_rate, int upsample, int baud, struct demodulator_state_s *D);

void demod_9600_process_sample (int chan, int sam, int upsample, struct demodulator_state_s *D);




/* Undo data scrambling for 9600 baud. */

static inline int descramble (int in, int *state)
{
	int out;

	out = (in ^ (*state >> 16) ^ (*state >> 11)) & 1;
	*state = (*state << 1) | (in & 1);
	return (out);
}
