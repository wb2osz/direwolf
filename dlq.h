
/*------------------------------------------------------------------
 *
 * Module:      dlq.h
 *
 *---------------------------------------------------------------*/

#ifndef DLQ_H
#define DLQ_H 1

#include "ax25_pad.h"
#include "audio.h"



/* Types of things that can be in queue. */

typedef enum dlq_type_e {DLQ_REC_FRAME, DLQ_CONNECT_REQUEST, DLQ_DISCONNECT_REQUEST, DLQ_XMIT_DATA_REQUEST} dlq_type_t; 


/* A queue item. */

// TODO: call this event rather than item.
// TODO: should add fences.

typedef struct dlq_item_s {

	struct dlq_item_s *nextp;	/* Next item in queue. */

	dlq_type_t type;		/* Type of item. */
					/* DLQ_REC_FRAME, DLQ_CONNECT_REQUEST, DLQ_DISCONNECT_REQUEST, DLQ_XMIT_DATA_REQUEST	 */

	int chan;			/* Radio channel of origin. */

// Used for received frame.

	int subchan;			/* Winning "subchannel" when using multiple */
					/* decoders on one channel.  */
					/* Special case, -1 means DTMF decoder. */
					/* Maybe we should have a different type in this case? */

	int slice;			/* Winning slicer. */

	packet_t pp;			/* Pointer to frame structure. */

	alevel_t alevel;		/* Audio level. */

	retry_t retries;		/* Effort expended to get a valid CRC. */

	char spectrum[MAX_SUBCHANS*MAX_SLICERS+1];	/* "Spectrum" display for multi-decoders. */

// Used by requests from a client application.

	char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];

	int num_addr;			/* Range 2 .. 10. */

	int client;

	int pid;

	/* TODO: xmit data */

} dlq_item_t;



void dlq_init (void);



void dlq_rec_frame (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, retry_t retries, char *spectrum);

void dlq_connect_request (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, int chan, int client, int pid);

void dlq_disconnect_request (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, int chan, int client);

void dlq_xmit_data_request (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, int chan, int clienti, int pid, char *xdata_ptr, int xdata_len);


int dlq_wait_while_empty (double timeout_val);

struct dlq_item_s *dlq_remove (void);

void dlq_delete (struct dlq_item_s *pitem);

#endif

/* end dlq.h */
