

/* xid.h */


#include "ax25_pad.h"		// for enum ax25_modulo_e


struct xid_param_s {

	int full_duplex;
	
	// Order is important because negotiation keeps the lower value of
	// REJ  (srej_none),  SREJ (default without negotiation), Multi-SREJ (if both agree).

	enum srej_e { srej_none=0, srej_single=1, srej_multi=2, srej_not_specified=3 } srej;

	enum ax25_modulo_e modulo;

	int i_field_length_rx;	/* In bytes.  XID has it in bits. */

	int window_size_rx;

	int ack_timer;		/* "T1" in mSec. */

	int retries;		/* "N1" */
};


int xid_parse (unsigned char *info, int info_len, struct xid_param_s *result, char *desc, int desc_size);

int xid_encode (struct xid_param_s *param, unsigned char *info, cmdres_t cr);