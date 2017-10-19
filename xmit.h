

#ifndef XMIT_H
#define XMIT_H 1

#include "audio.h"	/* for struct audio_s */


extern void xmit_init (struct audio_s *p_modem, int debug_xmit_packet);

extern void xmit_set_txdelay (int channel, int value);

extern void xmit_set_persist (int channel, int value);

extern void xmit_set_slottime (int channel, int value);

extern void xmit_set_txtail (int channel, int value);

extern void xmit_set_fulldup (int channel, int value);


extern int xmit_speak_it (char *script, int c, char *msg);

#endif

/* end xmit.h */

