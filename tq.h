
/*------------------------------------------------------------------
 *
 * Module:      tq.h
 *
 * Purpose:   	Transmit queue - hold packets for transmission until the channel is clear.
 *		
 *---------------------------------------------------------------*/

#ifndef TQ_H
#define TQ_H 1

#include "ax25_pad.h"
#include "audio.h"

#define TQ_NUM_PRIO 2				/* Number of priorities. */

#define TQ_PRIO_0_HI 0
#define TQ_PRIO_1_LO 1



void tq_init (struct audio_s *audio_config_p);

void tq_append (int chan, int prio, packet_t pp);

void lm_data_request (int chan, int prio, packet_t pp);

void lm_seize_request (int chan);

void tq_wait_while_empty (int chan);

packet_t tq_remove (int chan, int prio);

packet_t tq_peek (int chan, int prio);

int tq_count (int chan, int prio, char *source, char *dest, int bytes);

#endif

/* end tq.h */
