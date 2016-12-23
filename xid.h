

/* xid.h */

#include "ax25_pad.h"		// for enum ax25_modulo_e


struct xid_param_s {

	int full_duplex;
	
	// Order is important because negotiation keeps the lower value.
	// We will support only 1 & 2.

	enum rej_e {unknown_reject=0, implicit_reject=1, selective_reject=2, selective_reject_reject=3 } rej;

	enum ax25_modulo_e modulo;

	int i_field_length_rx;	/* In bytes.  XID has it in bits. */

	int window_size_rx;

	int ack_timer;		/* "T1" in mSec. */

	int retries;		/* "N1" */
};


int xid_parse (unsigned char *info, int info_len, struct xid_param_s *result, char *desc, int desc_size);

int xid_encode (struct xid_param_s *param, unsigned char *info);