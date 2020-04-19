
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2020  John Langner, WB2OSZ
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//


/********************************************************************************
 *
 * File:	ais.c
 *
 * Purpose:	Functions for processing received AIS transmissions and
 *		converting to NMEA sentence representation.
 *
 * References:	AIVDM/AIVDO protocol decoding by Eric S. Raymond
 *		https://gpsd.gitlab.io/gpsd/AIVDM.html
 *
 *		Sample recording with about 100 messages.  Test with "atest -B AIS xxx.wav"
 *		https://github.com/freerange/ais-on-sdr/wiki/example-data/long-beach-160-messages.wav
 *
 *		Useful on-line decoder for AIS NMEA sentences.
 *		https://www.aggsoft.com/ais-decoder.htm
 *
 * Future?	Add an interface to feed AIS data into aprs.fi.
 *		https://aprs.fi/page/ais_feeding
 *		
 *******************************************************************************/

#include "direwolf.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "textcolor.h"
#include "ais.h"


/*-------------------------------------------------------------------
 *
 * Functions to get and set element of a bit vector.
 *
 *--------------------------------------------------------------------*/

static const unsigned char mask[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

static inline unsigned int get_bit (unsigned char *base, unsigned int offset)
{
	return ( (base[offset >> 3] & mask[offset & 0x7]) != 0);
}

static inline void set_bit (unsigned char *base, unsigned int offset, int val)
{
	if (val) {
	  base[offset >> 3] |= mask[offset & 0x7];
	}
	else {
	  base[offset >> 3] &= ~ mask[offset & 0x7];
	}
}


/*-------------------------------------------------------------------
 *
 * Extract a variable length field from a bit vector.
 *
 *--------------------------------------------------------------------*/

static unsigned int get_field (unsigned char *base, unsigned int start, unsigned int len)
{
	unsigned int result = 0;
	for (int k = 0; k < len; k++) {
	  result <<= 1;
	  result |= get_bit (base, start + k);
	}
	return (result);
}

static void set_field (unsigned char *base, unsigned int start, unsigned int len, unsigned int val)
{
	for (int k = 0; k < len; k++) {
	  set_bit (base, start + k, (val >> (len - 1 - k) ) & 1);
	}
}


static int get_field_signed (unsigned char *base, unsigned int start, unsigned int len)
{
	int result = (int) get_field(base, start, len);
	// Sign extend.
	result <<= (32 - len);
	result >>= (32 - len);
	return (result);
}

static double get_field_latlon (unsigned char *base, unsigned int start, unsigned int len)
{
	// Latitude of 0x3412140 (91 deg) means not available.
	// Longitude of 0x6791AC0 (181 deg) means not available.
	return ((double)get_field_signed(base, start, len) / 600000.0);
}

static float get_field_speed (unsigned char *base, unsigned int start, unsigned int len)
{
	// Raw 1023 means not available.
	// Multiply by 0.1 to get knots.
	return ((float)get_field_signed(base, start, len) * 0.1);
}

static float get_field_course (unsigned char *base, unsigned int start, unsigned int len)
{
	// Raw 3600 means not available.
	// Multiply by 0.1 to get degrees
	return ((float)get_field_signed(base, start, len) * 0.1);
}


/*-------------------------------------------------------------------
 *
 * Convert between 6 bit values and printable characters used in 
 * in the AIS NMEA sentences.
 *
 *--------------------------------------------------------------------*/

// Characters '0' thru 'W'  become values 0 thru 39.
// Characters '`' thru 'w'  become values 40 thru 63.

static int char_to_sextet (char ch)
{
	if (ch >= '0' && ch <= 'W') {
	  return (ch - '0');
	}
	else if (ch >= '`' && ch <= 'w') {
	  return (ch - '`' + 40);
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid character \"%c\" found in AIS NMEA sentence payload.\n", ch);
	  return (0);
	}
}


// Values 0 thru 39 become characters '0' thru 'W'.
// Values 40 thru 63 become characters '`' thru 'w'.
// This is known as "Payload Armoring."

static int sextet_to_char (int val)
{
	if (val >= 0 && val <= 39) {
	  return ('0' + val);
	}
	else if (val >= 40 && val <= 63) {
	  return ('`' + val - 40);
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid 6 bit value %d from AIS HDLC payload.\n", val);
	  return ('0');
	}
}


/*-------------------------------------------------------------------
 *
 * Convert AIS binary block (from HDLC frame) to NMEA sentence. 
 *
 * In:	Pointer to AIS binary block and number of bytes.
 * Out:	NMEA sentence.  Provide size to avoid string overflow.
 *
 *--------------------------------------------------------------------*/

void ais_to_nmea (unsigned char *ais, int ais_len, char *nmea, int nmea_size)
{
	char payload[256];
	// Number of resulting characters for payload.
	int ns = (ais_len * 8 + 5) / 6;
	if (ns+1 > sizeof(payload)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("AIS HDLC payload of %d bytes is too large.\n", ais_len);
	  ns = sizeof(payload) - 1;
	}
	for (int k = 0; k < ns; k++) {
	  payload[k] = sextet_to_char(get_field(ais, k*6, 6));
	}
	payload[ns] = '\0';

	strlcpy (nmea, "!AIVDM,1,1,,A,", nmea_size);
	strlcat (nmea, payload, nmea_size);

	// If the number of bytes in is not a multiple of 3, this does not
	// produce a whole number of characters out. Extra padding bits were
	// added to get the last character.  Include this number so the
	// decoding application can drop this number of bits from the end.
	// At least, I think that is the way it should work.
	// The examples all have 0.
	char pad_bits[8];
	snprintf (pad_bits, sizeof(pad_bits), ",%d", ns * 6 - ais_len * 8);
	strlcat (nmea, pad_bits, nmea_size);

	// Finally the NMEA style checksum.
	int cs = 0;
	for (char *p = nmea + 1; *p != '\0'; p++) {
	  cs ^= *p;
	}
	char checksum[8];
	snprintf (checksum, sizeof(checksum), "*%02X", cs & 0x7f);
	strlcat (nmea, checksum, nmea_size);
}


/*-------------------------------------------------------------------
 *
 * Name:        ais_parse
 *
 * Purpose:    	Parse AIS sentence and extract interesing parts.
 *
 * Inputs:	sentence	NMEA sentence.
 *
 *		quiet		Suppress printing of error messages.
 *
 * Outputs:	descr		Description of AIS message type.
 *		mssi		9 digit identifier.
 *		odlat		latitude.
 *		odlon		longitude.
 *		ofknots		speed.
 *		ofcourse	direction of travel.
 *		
 * Returns:	0 for success, -1 for error.
 *
 *--------------------------------------------------------------------*/

// Maximum NMEA sentence length is 82 according to some people.  
// Make buffer considerably larger to be safe.
#define NMEA_MAX_LEN 240

int ais_parse (char *sentence, int quiet, char *descr, int descr_size, char *mssi, int mssi_size, double *odlat, double *odlon, float *ofknots, float *ofcourse)
{
	char stemp[NMEA_MAX_LEN];	/* Make copy because parsing is destructive. */

	strlcpy (mssi, "?", mssi_size);
	*odlat = G_UNKNOWN;
	*odlon = G_UNKNOWN;
	*ofknots = G_UNKNOWN;
	*ofcourse = G_UNKNOWN;

	strlcpy (stemp, sentence, sizeof(stemp));

// Verify and remove checksum.

        unsigned char cs = 0;
        char *p;

        for (p = stemp+1; *p != '*' && *p != '\0'; p++) {
          cs ^= *p;
        }

        p = strchr (stemp, '*');
        if (p == NULL) {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_INFO);
            dw_printf("Missing AIS sentence checksum.\n");
	  }
          return (-1);
        }
        if (cs != strtoul(p+1, NULL, 16)) {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("AIS sentence checksum error. Expected %02x but found %s.\n", cs, p+1);
	  }
          return (-1);
        }
        *p = '\0';      // Remove the checksum.

// Extract the comma separated fields.

	char *next;

	char *talker;			/* Expecting !AIVDM */
	char *frag_count;		/* ignored */
	char *frag_num;			/* ignored */
	char *msg_id;			/* ignored */
	char *radio_chan;		/* ignored */
	char *payload;			/* Encoded as 6 bits per character. */
	char *fill_bits;		/* Number of bits to discard. */

	next = stemp;
	talker = strsep(&next, ",");
	frag_count = strsep(&next, ",");
	frag_num = strsep(&next, ",");	
	msg_id = strsep(&next, ",");
	radio_chan = strsep(&next, ",");
	payload = strsep(&next, ",");
	fill_bits = strsep(&next, ",");

	/* Suppress the 'set but not used' compiler warnings. */
	/* Alternatively, we might use __attribute__((unused)) */

	(void)(talker);
	(void)(frag_count);
	(void)(frag_num);
	(void)(msg_id);
	(void)(radio_chan);

	if (payload == NULL || strlen(payload) == 0) {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Payload is missing from AIS sentence.\n");
	  }
	  return (-1);
	}

// Convert character representation to bit vector.
	
	unsigned char ais[256];
	memset (ais, 0, sizeof(ais));

	int plen = strlen(payload);

	for (int k = 0; k < plen; k++) {
	  set_field (ais, k*6, 6, char_to_sextet(payload[k]));
	}

// Verify number of filler bits.

	int nfill = atoi(fill_bits);
	int nbytes = (plen * 6) / 8;

	if (nfill != plen * 6 - nbytes * 8) {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Number of filler bits is %d when %d is expected.\n",
			nfill, plen * 6 - nbytes * 8);
	  }
	}

// Extract the fields of interest from a few message types.
// Don't get too carried away.

	int type = get_field(ais, 0, 6);
	switch (type) {

	  case 1:	// Position Report Class A
	  case 2:
	  case 3:

	    snprintf (descr, descr_size, "AIS %d: Position Report Class A", type);
	    snprintf (mssi, mssi_size, "%d", get_field(ais, 8, 30));
	    *odlon = get_field_latlon(ais, 61, 28);
	    *odlat = get_field_latlon(ais, 89, 27);
	    *ofknots = get_field_speed(ais, 50, 10);
	    *ofcourse = get_field_course(ais, 116, 12);
	    break;

	  case 4:	// Base Station Report

	    snprintf (descr, descr_size, "AIS %d: Base Station Report", type);
	    snprintf (mssi, mssi_size, "%d", get_field(ais, 8, 30));
	    //year = get_field(ais, 38, 14);
	    //month = get_field(ais, 52, 4);
	    //day = get_field(ais, 56, 5);
	    //hour = get_field(ais, 61, 5);
	    //minute = get_field(ais, 66, 6);
	    //second = get_field(ais, 72, 6);
	    *odlon = get_field_latlon(ais, 79, 28);
	    *odlat = get_field_latlon(ais, 107, 27);
	    break;

	  case 18:	// Standard Class B CS Position Report

	    snprintf (descr, descr_size, "AIS %d: Standard Class B CS Position Report", type);
	    snprintf (mssi, mssi_size, "%d", get_field(ais, 8, 30));
	    *odlon = get_field_latlon(ais, 57, 28);
	    *odlat = get_field_latlon(ais, 85, 27);
	    break;

	  case 19:	// Extended Class B CS Position Report

	    snprintf (descr, descr_size, "AIS %d: Extended Class B CS Position Report", type);
	    snprintf (mssi, mssi_size, "%d", get_field(ais, 8, 30));
	    *odlon = get_field_latlon(ais, 57, 28);
	    *odlat = get_field_latlon(ais, 85, 27);
	    break;

	  default:
	    snprintf (descr, descr_size, "AIS message type %d", type);
	    break;
	}

	return (0);

} /* end ais_parse */

// end ais.c