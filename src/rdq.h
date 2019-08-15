
/*------------------------------------------------------------------
 *
 * Module:      rdq.h
 *
 * Purpose:   	Retry decode queue - Hold raw received frames with errors
 *		for retrying the decoding later.
 *		
 *---------------------------------------------------------------*/

#ifndef RDQ_H
#define RDQ_H 1

#include "rrbb.h"
//#include "audio.h"

void rdq_init (void);

void rdq_append (rrbb_t rrbb);

void rdq_wait_while_empty (void);

rrbb_t rdq_remove (void);


#endif

/* end rdq.h */
