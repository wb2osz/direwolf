

#ifndef PTT_H
#define PTT_H 1


#include "audio.h"	/* for struct audio_s */


void ptt_init (struct audio_s *p_modem);

void ptt_set (int chan, int ptt); 

void ptt_term (void);


#endif


/* end ptt.h */



