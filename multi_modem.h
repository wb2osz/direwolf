/* multi_modem.h */

#ifndef MULTI_MODEM_H
#define MULTI_MODEM 1

/* Needed for typedef retry_t. */
#include "hdlc_rec2.h"

/* Needed for struct audio_s */
#include "audio.h"


void multi_modem_init (struct audio_s *pmodem); 

void multi_modem_process_sample (int c, int audio_sample);

void multi_modem_process_rec_frame (int chan, int subchan, int slice, unsigned char *fbuf, int flen, alevel_t alevel, retry_t retries);

#endif
