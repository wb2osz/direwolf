
#ifndef HDLC_REC2_H
#define HDLC_REC2_H 1


#include "rrbb.h"
#include "ax25_pad.h"	/* for packet_t */

typedef enum retry_e {
		RETRY_NONE=0,
		RETRY_SINGLE=1,
		RETRY_DOUBLE=2,
		RETRY_TRIPLE=3,
		RETRY_TWO_SEP=4 } retry_t;

#if defined(DIREWOLF_C) || defined(ATEST_C) || defined(UDPTEST_C)

static const char * retry_text[] = {
		"NONE",
		"SINGLE",
		"DOUBLE",
		"TRIPLE",
		"TWO_SEP" };
#endif

void hdlc_rec2_block (rrbb_t block, retry_t fix_bits);

void hdlc_rec2_try_to_fix_later (rrbb_t block, int chan, int subchan, int alevel);

/* Provided by the top level application to process a complete frame. */

void app_process_rec_packet (int chan, int subchan, packet_t pp, int level, retry_t retries, char *spectrum);


#endif
