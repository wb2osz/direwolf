
/* log.h */


#include "hdlc_rec2.h"		// for retry_t

#include "decode_aprs.h"	// for decode_aprs_t




void log_init (char *path);	

void log_write (int chan, decode_aprs_t *A, packet_t pp, int alevel, retry_t retries);

void log_term (void); 	