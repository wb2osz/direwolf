
/*----------------------------------------------------------------------------
 * 
 * Name:	igate.h
 *
 * Purpose:	Interface to the Internet Gateway functions.
 *
 *-----------------------------------------------------------------------------*/


#ifndef IGATE_H
#define IGATE_H 1


#include "ax25_pad.h"
#include "digipeater.h"
#include "audio.h"


#define DEFAULT_IGATE_PORT 14580



struct igate_config_s {

/*
 * For logging into the IGate server.
 */
	char t2_server_name[40];	/* Tier 2 IGate server name. */

	int t2_server_port;		/* Typically 14580. */

	char t2_login[AX25_MAX_ADDR_LEN];/* e.g. WA9XYZ-15 */
					/* Note that the ssid could be any two alphanumeric */
					/* characters not just 1 thru 15. */
					/* Could be same or different than the radio call(s). */
					/* Not sure what the consequences would be. */

	char t2_passcode[8];		/* Max. 5 digits. Could be "-1". */

	char *t2_filter;		/* Optional filter for IS -> RF direction. */
					/* This is the "server side" filter. */
					/* A better name would be subscription or something */
					/* like that because we can only ask for more. */

/*
 * For transmitting.
 */
	int tx_chan;			/* Radio channel for transmitting. */
					/* 0=first, etc.  -1 for none. */
					/* Presently IGate can transmit on only a single channel. */
					/* A future version might generalize this.  */
					/* Each transmit channel would have its own client side filtering. */

	char tx_via[80];		/* VIA path for transmitting third party packets. */
					/* Usual text representation.  */
					/* Must start with "," if not empty so it can */
					/* simply be inserted after the destination address. */

	int max_digi_hops;		/* Maximum number of digipeater hops possible for via path. */
					/* Derived from the SSID when last character of address is a digit. */
					/* e.g.  "WIDE1-1,WIDE5-2" would be 3. */
					/* This is useful to know so we can determine how many */
					/* stations we might be able to reach. */

	int tx_limit_1;			/* Max. packets to transmit in 1 minute. */

	int tx_limit_5;			/* Max. packets to transmit in 5 minutes. */

	int igmsp;			/* Number of message sender position reports to allow. */
					/* Common practice is to default to 1.  */
					/* We allow additional flexibility of 0 to disable feature */
					/* or a small number to allow more. */

/*
 * Receiver to IS data options.
 */
	int rx2ig_dedupe_time;		/* seconds.  0 to disable. */

/*
 * Special SATgate mode to delay packets heard directly.
 */
	int satgate_delay;		/* seconds.  0 to disable. */
};


#define IGATE_TX_LIMIT_1_DEFAULT 6
#define IGATE_TX_LIMIT_1_MAX     20

#define IGATE_TX_LIMIT_5_DEFAULT 20
#define IGATE_TX_LIMIT_5_MAX     80

#define IGATE_RX2IG_DEDUPE_TIME 0		/* Issue 85.  0 means disable dupe checking in RF>IS direction. */
						/* See comments in rx_to_ig_remember & rx_to_ig_allow. */
						/* Currently there is no configuration setting to change this. */

#define DEFAULT_SATGATE_DELAY 10
#define MIN_SATGATE_DELAY 5
#define MAX_SATGATE_DELAY 30


/* Call this once at startup */

void igate_init (struct audio_s *p_audio_config, struct igate_config_s *p_igate_config, struct digi_config_s *p_digi_config, int debug_level);

/* Call this with each packet received from the radio. */

void igate_send_rec_packet (int chan, packet_t recv_pp);

/* This when digipeater transmits.  Set bydigi to 1 . */

void ig_to_tx_remember (packet_t pp, int chan, int bydigi);



/* Get statistics for IGATE status beacon. */

int igate_get_msg_cnt (void);

int igate_get_pkt_cnt (void);

int igate_get_upl_cnt (void);

int igate_get_dnl_cnt (void);



#endif
