

#ifndef CDIGIPEATER_H
#define CDIGIPEATER_H 1

#include "regex.h"

#include "direwolf.h"		/* for MAX_CHANS */
#include "ax25_pad.h"		/* for packet_t */
#include "audio.h"		/* for radio channel properties */


/*
 * Information required for Connected mode digipeating.
 *
 * The configuration file reader fills in this information
 * and it is passed to cdigipeater_init at application start up time.
 */


struct cdigi_config_s {

/*
 * Rules for each of the [from_chan][to_chan] combinations.
 */
	int	enabled[MAX_CHANS][MAX_CHANS];	// Is it enabled for from/to pair?

	int has_alias[MAX_CHANS][MAX_CHANS];	// If there was no alias in the config file,
						// the structure below will not be set up
						// properly and an attempt to use it could
						// result in a crash.  (fixed v1.5)
						// Not needed for [APRS] DIGIPEAT because
						// the alias is mandatory there.
	regex_t	alias[MAX_CHANS][MAX_CHANS];

	char *cfilter_str[MAX_CHANS][MAX_CHANS];
						// NULL or optional Packet Filter strings such as "t/m".
};

/*
 * Call once at application start up time.
 */

extern void cdigipeater_init (struct audio_s *p_audio_config, struct cdigi_config_s *p_cdigi_config);

/*
 * Call this for each packet received.
 * Suitable packets will be queued for transmission.
 */

extern void cdigipeater (int from_chan, packet_t pp);


/* Make statistics available. */

int cdigipeater_get_count (int from_chan, int to_chan);


#endif 

/* end cdigipeater.h */

