/* multi_modem.h */

#ifndef MULTI_MODEM_H
#define MULTI_MODEM 1

/* Needed for typedef retry_t. */
#include "hdlc_rec2.h"

/* Needed for struct audio_s */
#include "audio.h"


void multi_modem_init (struct audio_s *pmodem); 

void multi_modem_process_sample (int c, int audio_sample);

int multi_modem_get_dc_average (int chan);

// Deprecated.  Replace with ...packet
void multi_modem_process_rec_frame (int chan, int subchan, int slice, unsigned char *fbuf, int flen, alevel_t alevel, retry_t retries, fec_type_t fec_type);

void multi_modem_process_rec_packet (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, retry_t retries, fec_type_t fec_type);

#endif
