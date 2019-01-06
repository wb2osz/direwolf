
/* ax25_link.h */


#ifndef AX25_LINK_H
#define AX25_LINK_H 1

#include "ax25_pad.h"		// for AX25_MAX_INFO_LEN

#include "dlq.h"		// for dlq_item_t

#include "config.h"		// for struct misc_config_s



// Limits and defaults for parameters.


#define AX25_N1_PACLEN_MIN 1		// Max bytes in Information part of frame.
#define AX25_N1_PACLEN_DEFAULT 256	// some v2.0 implementations have 128
#define AX25_N1_PACLEN_MAX AX25_MAX_INFO_LEN	// from ax25_pad.h


#define AX25_N2_RETRY_MIN 1		// Number of times to retry before giving up.
#define AX25_N2_RETRY_DEFAULT 10
#define AX25_N2_RETRY_MAX 15


#define AX25_T1V_FRACK_MIN 1		// Number of seconds to wait before retrying.
#define AX25_T1V_FRACK_DEFAULT 3	// KPC-3+ has 4.  TM-D710A has 3.
#define AX25_T1V_FRACK_MAX 15


#define AX25_K_MAXFRAME_BASIC_MIN 1		// Window size - number of I frames to send before waiting for ack.
#define AX25_K_MAXFRAME_BASIC_DEFAULT 4
#define AX25_K_MAXFRAME_BASIC_MAX 7

#define AX25_K_MAXFRAME_EXTENDED_MIN 1
#define AX25_K_MAXFRAME_EXTENDED_DEFAULT 32
#define AX25_K_MAXFRAME_EXTENDED_MAX 63		// In theory 127 but I'm restricting as explained in SREJ handling.



// Call once at startup time.

void ax25_link_init (struct misc_config_s *pconfig);



// IMPORTANT:

// These functions must be called on a single thread, one at a time.
// The Data Link Queue (DLQ) is used to serialize events from multiple sources.

// Maybe the dispatch switch should be moved to ax25_link.c so they can all
// be made static and they can't be called from the wrong place accidentally.

void dl_connect_request (dlq_item_t *E);

void dl_disconnect_request (dlq_item_t *E);

void dl_data_request (dlq_item_t *E);

void dl_register_callsign (dlq_item_t *E);

void dl_unregister_callsign (dlq_item_t *E);

void dl_outstanding_frames_request (dlq_item_t *E);

void dl_client_cleanup (dlq_item_t *E);


void lm_data_indication (dlq_item_t *E);

void lm_seize_confirm (dlq_item_t *E);

void lm_channel_busy (dlq_item_t *E);


void dl_timer_expiry (void);


double ax25_link_get_next_timer_expiry (void);


#endif

/* end ax25_link.h */