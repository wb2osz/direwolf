
#ifndef DIGIPEATER_H
#define DIGIPEATER_H 1

#include "regex.h"

#include "direwolf.h"		/* for MAX_CHANS */
#include "ax25_pad.h"		/* for packet_t */
#include "audio.h"		/* for radio channel properties */


/*
 * Information required for digipeating.
 *
 * The configuration file reader fills in this information
 * and it is passed to digipeater_init at application start up time.
 */


struct digi_config_s {


	int	dedupe_time;	/* Don't digipeat duplicate packets */
				/* within this number of seconds. */

#define DEFAULT_DEDUPE 30

/*
 * Rules for each of the [from_chan][to_chan] combinations.
 */

	regex_t	alias[MAX_CHANS][MAX_CHANS];

	regex_t	wide[MAX_CHANS][MAX_CHANS];

	int	enabled[MAX_CHANS][MAX_CHANS];

	enum preempt_e { PREEMPT_OFF, PREEMPT_DROP, PREEMPT_MARK, PREEMPT_TRACE } preempt[MAX_CHANS][MAX_CHANS];

	// ATGP is an ugly hack for the specific need of ATGP which needs more that 8 digipeaters.
	// DO NOT put this in the User Guide.  On a need to know basis.

	char atgp[MAX_CHANS][MAX_CHANS][AX25_MAX_ADDR_LEN];

	char *filter_str[MAX_CHANS+1][MAX_CHANS+1];
						// NULL or optional Packet Filter strings such as "t/m".
						// Notice the size of arrays is one larger than normal.
						// That extra position is for the IGate.

	int regen[MAX_CHANS][MAX_CHANS];	// Regenerate packet.  
						// Sort of like digipeating but passed along unchanged.
};

/*
 * Call once at application start up time.
 */

extern void digipeater_init (struct audio_s *p_audio_config, struct digi_config_s *p_digi_config);

/*
 * Call this for each packet received.
 * Suitable packets will be queued for transmission.
 */

extern void digipeater (int from_chan, packet_t pp);

void digi_regen (int from_chan, packet_t pp);


/* Make statistics available. */

int digipeater_get_count (int from_chan, int to_chan);


#endif 

/* end digipeater.h */

