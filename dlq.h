
/*------------------------------------------------------------------
 *
 * Module:      dlq.h
 *
 *---------------------------------------------------------------*/

#ifndef DLQ_H
#define DLQ_H 1

#include "ax25_pad.h"
#include "audio.h"


void dlq_init (void);

/* Types of things that can be in queue. */

typedef enum dlq_type_e {DLQ_REC_FRAME} dlq_type_t; 

void dlq_append (dlq_type_t type, int chan, int subchan, int slice, packet_t pp, alevel_t alevel, retry_t retries, char *spectrum);

void dlq_wait_while_empty (void);

int dlq_remove (dlq_type_t *type, int *chan, int *subchan, int *slice, packet_t *pp, alevel_t *alevel, retry_t *retries, char *spectrum, size_t spectrumsize); 

#endif

/* end dlq.h */
