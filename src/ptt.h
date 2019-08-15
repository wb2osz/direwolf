

#ifndef PTT_H
#define PTT_H 1


#include "audio.h"	/* for struct audio_s and definitions for octype values */


void ptt_set_debug(int debug);

void ptt_init (struct audio_s *p_modem);

void ptt_set (int octype, int chan, int ptt); 

void ptt_term (void);

int get_input (int it, int chan);

#endif


/* end ptt.h */



