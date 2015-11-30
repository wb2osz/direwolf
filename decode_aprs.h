
/* decode_aprs.h */


#ifndef DECODE_APRS_H

#define DECODE_APRS_H 1



#ifndef G_UNKNOWN
#include "latlong.h"
#endif

#ifndef AX25_MAX_ADDR_LEN
#include "ax25_pad.h"
#endif 

#ifndef APRSTT_LOC_DESC_LEN
#include "aprs_tt.h"
#endif

typedef struct decode_aprs_s {

	int g_quiet;			/* Suppress error messages when decoding. */

        char g_src[AX25_MAX_ADDR_LEN];

        char g_msg_type[60];		/* Message type.  Telemetry descriptions get pretty long. */

        char g_symbol_table;		/* The Symbol Table Identifier character selects one */
					/* of the two Symbol Tables, or it may be used as */
					/* single-character (alpha or numeric) overlay, as follows: */
					
					/*	/ 	Primary Symbol Table (mostly stations) */

					/* 	\ 	Alternate Symbol Table (mostly Objects) */

					/*	0-9 	Numeric overlay. Symbol from Alternate Symbol */
					/*		Table (uncompressed lat/long data format) */

					/*	a-j	Numeric overlay. Symbol from Alternate */
					/*		Symbol Table (compressed lat/long data */
					/*		format only). i.e. a-j maps to 0-9 */

					/*	A-Z	Alpha overlay. Symbol from Alternate Symbol Table */


        char g_symbol_code;		/* Where the Symbol Table Identifier is 0-9 or A-Z (or a-j */
					/* with compressed position data only), the symbol comes from */
					/* the Alternate Symbol Table, and is overlaid with the */
					/* identifier (as a single digit or a capital letter). */

	char g_aprstt_loc[APRSTT_LOC_DESC_LEN];		/* APRStt location from !DAO! */

        double g_lat, g_lon;		/* Location, degrees.  Negative for South or West. */
					/* Set to G_UNKNOWN if missing or error. */

        char g_maidenhead[12];		/* 4 or 6 (or 8?) character maidenhead locator. */

        char g_name[12];		/* Object or item name. Max. 9 characters. */

	char g_addressee[12];		/* Addressee for a "message."  Max. 9 characters. */
					/* Also for Directed Station Query which is a */
					/* special case of message. */

        float g_speed_mph;		/* Speed in MPH.  */

        float g_course;			/* 0 = North, 90 = East, etc. */
	
        int g_power;			/* Transmitter power in watts. */

        int g_height;			/* Antenna height above average terrain, feet. */

        int g_gain;			/* Antenna gain in dB. */

        char g_directivity[12];		/* Direction of max signal strength */

        float g_range;			/* Precomputed radio range in miles. */

        float g_altitude_ft;		/* Feet above median sea level.  */

        char g_mfr[80];			/* Manufacturer or application. */

        char g_mic_e_status[32];	/* MIC-E message. */

        double g_freq;			/* Frequency, MHz */

        float g_tone;			/* CTCSS tone, Hz, one fractional digit */

        int g_dcs;			/* Digital coded squelch, print as 3 octal digits. */

        int g_offset;			/* Transmit offset, kHz */


	char g_query_type[12];		/* General Query: APRS, IGATE, WX, ... */
					/* Addressee is NOT set. */

					/* Directed Station Query: exactly 5 characters. */
					/* APRSD, APRST, PING?, ... */
					/* Addressee is set. */
	
	double g_footprint_lat;		/* A general query may contain a foot print. */
	double g_footprint_lon;		/* Set all to G_UNKNOWN if not used. */
	float g_footprint_radius;	/* Radius in miles. */

	char g_query_callsign[12];	/* Directed query may contain callsign.  */
					/* e.g. tell me all objects from that callsign. */


        char g_weather[500];		/* Weather.  Can get quite long. Rethink max size. */

        char g_telemetry[256];		/* Telemetry data.  Rethink max size. */

        char g_comment[256];		/* Comment. */

} decode_aprs_t;





extern void decode_aprs (decode_aprs_t *A, packet_t pp, int quiet);

extern void decode_aprs_print (decode_aprs_t *A);


#endif