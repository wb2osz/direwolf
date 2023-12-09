

/* demod.h */

#include "audio.h" 	/* for struct audio_s */
#include "ax25_pad.h"	/* for alevel_t */


int demod_init (struct audio_s *pa);

void demod_mute_input (int chan, int mute);

int demod_get_sample (int a);

void demod_process_sample (int chan, int subchan, int sam);

void demod_print_agc (int chan, int subchan);

alevel_t demod_get_audio_level (int chan, int subchan);

