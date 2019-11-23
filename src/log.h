
/* log.h */


#include "hdlc_rec2.h"		// for retry_t

#include "decode_aprs.h"	// for decode_aprs_t

#include "ax25_pad.h"



void log_init (int daily_names, char *path);

void log_write (int chan, decode_aprs_t *A, packet_t pp, alevel_t alevel, retry_t retries);

void log_rr_bits (decode_aprs_t *A, packet_t pp);

void log_term (void); 	