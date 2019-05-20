
/* demod_psk.h */


void demod_psk_init (enum modem_t modem_type, enum v26_e v26_alt, int samples_per_sec, int bps, char profile, struct demodulator_state_s *D);

void demod_psk_process_sample (int chan, int subchan, int sam, struct demodulator_state_s *D);
