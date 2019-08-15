
/* demod_afsk.h */


void demod_afsk_init (int samples_per_sec, int baud, int mark_freq,
			int space_freq, char profile, struct demodulator_state_s *D);

void demod_afsk_process_sample (int chan, int subchan, int sam, struct demodulator_state_s *D);
