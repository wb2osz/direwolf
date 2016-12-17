/* dtmf.h */


#include "audio.h"

void dtmf_init (struct audio_s *p_audio_config, int amp);

char dtmf_sample (int c, float input);

int dtmf_send (int chan, char *str, int speed, int txdelay, int txtail);


/* end dtmf.h */

