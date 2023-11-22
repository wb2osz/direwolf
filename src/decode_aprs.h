
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

        char g_src[AX25_MAX_ADDR_LEN];	// In the case of a packet encapsulated by a 3rd party
					// header, this is the encapsulated source.

        char g_dest[AX25_MAX_ADDR_LEN];

        char g_data_type_desc[100];	/* APRS data type description.  Telemetry descriptions get pretty long. */

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

	// This is so pfilter.c:filt_t does not need to duplicate the same work.

	int g_has_thirdparty_header;
	enum packet_type_e {
			packet_type_none=0,
			packet_type_position,
			packet_type_weather,
			packet_type_object,
			packet_type_item,
			packet_type_message,
			packet_type_query,
			packet_type_capabilities,
			packet_type_status,
			packet_type_telemetry,
			packet_type_userdefined,
			packet_type_nws
		} g_packet_type;

	enum message_subtype_e { message_subtype_invalid = 0,
				message_subtype_message,
				message_subtype_ack,
				message_subtype_rej,
				message_subtype_bulletin,
				message_subtype_nws,
				message_subtype_telem_parm,
				message_subtype_telem_unit,
				message_subtype_telem_eqns,
				message_subtype_telem_bits,
				message_subtype_directed_query
		} g_message_subtype;	/* Various cases of the overloaded "message." */

	char g_message_number[12];	/* Message number.  Should be 1 - 5 alphanumeric characters if used. */
					/* Addendum 1.1 has new format {mm} or {mm}aa with only two */
					/* characters for message number and an ack riding piggyback. */

        float g_speed_mph;		/* Speed in MPH.  */
					/* The APRS transmission uses knots so watch out for */
					/* conversions when sending and receiving APRS packets. */

        float g_course;			/* 0 = North, 90 = East, etc. */
	
        int g_power;			/* Transmitter power in watts. */

        int g_height;			/* Antenna height above average terrain, feet. */
					// TODO:  rename to g_height_ft

        int g_gain;			/* Antenna gain in dBi. */

        char g_directivity[12];		/* Direction of max signal strength */

        float g_range;			/* Precomputed radio range in miles. */

        float g_altitude_ft;		/* Feet above median sea level.  */
					/* I used feet here because the APRS specification */
					/* has units of feet for altitude.  Meters would be */
					/* more natural to the other 96% of the world. */

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





extern void decode_aprs (decode_aprs_t *A, packet_t pp, int quiet, char *third_party_src);

extern void decode_aprs_print (decode_aprs_t *A);


#endif
