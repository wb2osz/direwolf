

/* demod.h */

#include "audio.h" 	/* for struct audio_s */


int demod_init (struct audio_s *pa);

int demod_get_sample (void);

void demod_process_sample (int chan, int subchan, int sam);

void demod_print_agc (int chan, int subchan);

int demod_get_audio_level (int chan, int subchan);