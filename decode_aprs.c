//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015  John Langner, WB2OSZ
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


/*------------------------------------------------------------------
 *
 * File:	decode_aprs.c
 *
 * Purpose:	Decode the information part of APRS frame.
 *
 * Description: Present the packet contents in human readable format.
 *		This is a fairly complete implementation with error messages
 *		pointing out various specication violations. 
 *
 * Assumptions:	ax25_from_frame() has been called to 
 *		separate the header and information.
 *
 *------------------------------------------------------------------*/

#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>	/* for atof */
#include <string.h>	/* for strtok */

#include <math.h>	/* for pow */
#include <ctype.h>	/* for isdigit */
#include <fcntl.h>

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 1
#endif
#include "regex.h"

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "symbols.h"
#include "latlong.h"
#include "dwgpsnmea.h"
#include "decode_aprs.h"
#include "telemetry.h"


#define TRUE 1
#define FALSE 0



/* Position & symbol fields common to several message formats. */

typedef struct {
	  char lat[8];
	  char sym_table_id;		/* / \ 0-9 A-Z */
	  char lon[9];
	  char symbol_code;
	} position_t;

typedef struct {
	  char sym_table_id;		/* / \ a-j A-Z */
					/* "The presence of the leading Symbol Table Identifier */
					/* instead of a digit indicates that this is a compressed */
					/* Position Report and not a normal lat/long report." */
					/* "a-j" is not a typographical error. */
					/* The first 10 lower case letters represent the overlay */
					/* characters of 0-9 in the compressed format. */

	  char y[4];			/* Compressed Latitude. */
	  char x[4];			/* Compressed Longitude. */
	  char symbol_code;
	  char c;			/* Course/speed or altitude. */
	  char s;
	  char t	;		/* Compression type. */
	} compressed_position_t;


/* Range of digits for Base 91 representation. */

#define B91_MIN '!'
#define B91_MAX '{'
#define isdigit91(c) ((c) >= B91_MIN && (c) <= B91_MAX)


//static void print_decoded (decode_aprs_t *A);
static void aprs_ll_pos (decode_aprs_t *A, unsigned char *, int);
static void aprs_ll_pos_time (decode_aprs_t *A, unsigned char *, int);
static void aprs_raw_nmea (decode_aprs_t *A, unsigned char *, int);
static void aprs_mic_e (decode_aprs_t *A, packet_t, unsigned char *, int);
//static void aprs_compressed_pos (decode_aprs_t *A, unsigned char *, int);
static void aprs_message (decode_aprs_t *A, unsigned char *, int, int quiet);
static void aprs_object (decode_aprs_t *A, unsigned char *, int);
static void aprs_item (decode_aprs_t *A, unsigned char *, int);
static void aprs_station_capabilities (decode_aprs_t *A, char *, int);
static void aprs_status_report (decode_aprs_t *A, char *, int);
static void aprs_general_query (decode_aprs_t *A, char *, int, int quiet);
static void aprs_directed_station_query (decode_aprs_t *A, char *addressee, char *query, int quiet);
static void aprs_telemetry (decode_aprs_t *A, char *, int, int quiet);
static void aprs_raw_touch_tone (decode_aprs_t *A, char *, int);
static void aprs_morse_code (decode_aprs_t *A, char *, int);
static void aprs_positionless_weather_report (decode_aprs_t *A, unsigned char *, int);
static void weather_data (decode_aprs_t *A, char *wdata, int wind_prefix);
static void aprs_ultimeter (decode_aprs_t *A, char *, int);
static void third_party_header (decode_aprs_t *A, char *, int);
static void decode_position (decode_aprs_t *A, position_t *ppos);
static void decode_compressed_position (decode_aprs_t *A, compressed_position_t *ppos);
static double get_latitude_8 (char *p, int quiet);
static double get_longitude_9 (char *p, int quiet);
static time_t get_timestamp (decode_aprs_t *A, char *p);
static int get_maidenhead (decode_aprs_t *A, char *p);
static int data_extension_comment (decode_aprs_t *A, char *pdext);
static void decode_tocall (decode_aprs_t *A, char *dest);
//static void get_symbol (decode_aprs_t *A, char dti, char *src, char *dest);
static void process_comment (decode_aprs_t *A, char *pstart, int clen);




/*------------------------------------------------------------------
 *
 * Function:	decode_aprs
 *
 * Purpose:	Split APRS packet into separate properties that it contains.
 *
 * Inputs:	pp	- APRS packet object.
 *
 *		quiet	- Suppress error messages.
 *
 * Outputs:	A->	g_symbol_table, g_symbol_code,
 *			g_lat, g_lon, 
 *			g_speed_mph, g_course, g_altitude_ft,
 *			g_comment
 *			... and many others...
 *
 * Major Revisions: 1.1	Reorganized so parts are returned in a structure.
 *			Print function is now called separately.
 *
 *------------------------------------------------------------------*/

void decode_aprs (decode_aprs_t *A, packet_t pp, int quiet)
{

	char dest[AX25_MAX_ADDR_LEN];
	unsigned char *pinfo;
	int info_len;


  	info_len = ax25_get_info (pp, &pinfo);

	memset (A, 0, sizeof (*A));

	A->g_quiet = quiet;

	snprintf (A->g_msg_type, sizeof(A->g_msg_type), "Unknown message type %c", *pinfo);

	A->g_symbol_table = '/';	/* Default to primary table. */
	A->g_symbol_code = ' ';		/* What should we have for default symbol? */

	A->g_lat = G_UNKNOWN;
	A->g_lon = G_UNKNOWN;

	A->g_speed_mph = G_UNKNOWN;
	A->g_course = G_UNKNOWN;

	A->g_power = G_UNKNOWN;
	A->g_height = G_UNKNOWN;
	A->g_gain = G_UNKNOWN;

	A->g_range = G_UNKNOWN;
	A->g_altitude_ft = G_UNKNOWN;
	A->g_freq = G_UNKNOWN;
	A->g_tone = G_UNKNOWN;
	A->g_dcs = G_UNKNOWN;
	A->g_offset = G_UNKNOWN;

	A->g_footprint_lat = G_UNKNOWN;
	A->g_footprint_lon = G_UNKNOWN;
	A->g_footprint_radius = G_UNKNOWN;



/*
 * Extract source and destination including the SSID.
 */
	
	ax25_get_addr_with_ssid (pp, AX25_SOURCE, A->g_src);
	ax25_get_addr_with_ssid (pp, AX25_DESTINATION, dest);


	switch (*pinfo) {	/* "DTI" data type identifier. */

	    case '!':		/* Position without timestamp (no APRS messaging). */
				/* or Ultimeter 2000 WX Station */

	    case '=':		/* Position without timestamp (with APRS messaging). */

	      if (strncmp((char*)pinfo, "!!", 2) == 0)
	      {
		aprs_ultimeter (A, (char*)pinfo, info_len);
	      }
	      else
	      {	     
	        aprs_ll_pos (A, pinfo, info_len);
	      }
	      break;


	    //case '#':		/* Peet Bros U-II Weather station */
	    //case '*':		/* Peet Bros U-II Weather station */
	      //break;
		
	    case '$':		/* Raw GPS data or Ultimeter 2000 */
		
	      if (strncmp((char*)pinfo, "$ULTW", 5) == 0)
	      {
		aprs_ultimeter (A, (char*)pinfo, info_len);
	      }
	      else
	      {
	        aprs_raw_nmea (A, pinfo, info_len);
	      }
	      break;

	    case '\'':		/* Old Mic-E Data (but Current data for TM-D700) */
	    case '`':		/* Current Mic-E Data (not used in TM-D700) */

	      aprs_mic_e (A, pp, pinfo, info_len);
	      break;

	    case ')':		/* Item. */

	      aprs_item (A, pinfo, info_len);
	      break;
		
	    case '/':		/* Position with timestamp (no APRS messaging) */
	    case '@':		/* Position with timestamp (with APRS messaging) */

	      aprs_ll_pos_time (A, pinfo, info_len);
	      break;


	    case ':':		/* Message */
				/* Directed Station Query */

	      aprs_message (A, pinfo, info_len, quiet);
	      break;

	    case ';':		/* Object */

	      aprs_object (A, pinfo, info_len);
	      break;

	    case '<':		/* Station Capabilities */

	      aprs_station_capabilities (A, (char*)pinfo, info_len);
	      break;

	    case '>':		/* Status Report */

	      aprs_status_report (A, (char*)pinfo, info_len);
	      break;

	    
	    case '?':		/* General Query */

	      aprs_general_query (A, (char*)pinfo, info_len, quiet);
	      break;
		
	    case 'T':		/* Telemetry */

	      aprs_telemetry (A, (char*)pinfo, info_len, quiet);
	      break;

	    case '_':		/* Positionless Weather Report */

	      aprs_positionless_weather_report (A, pinfo, info_len);
	      break;

	    case '{':		/* user defined data */
				/* http://www.aprs.org/aprs11/expfmts.txt */

	      if (strncmp((char*)pinfo, "{tt", 3) == 0) {
	        aprs_raw_touch_tone (A, (char*)pinfo, info_len);
	      }
	      else if (strncmp((char*)pinfo, "{mc", 3) == 0) {
	        aprs_morse_code (A, (char*)pinfo, info_len);
	      }
	      else {
	        //aprs_user_defined (A, pinfo, info_len);
	      }
	      break;

	    case 't':		/* Raw touch tone data - NOT PART OF STANDARD */
				/* Used to convey raw touch tone sequences to */
				/* to an application that might want to interpret them. */
				/* Might move into user defined data, above. */

	      aprs_raw_touch_tone (A, (char*)pinfo, info_len);
	      break;

	    case 'm':		/* Morse Code data - NOT PART OF STANDARD */
				/* Used by APRStt gateway to put audible responses */
				/* into the transmit queue.  Could potentially find */
				/* other uses such as CW ID for station. */
				/* Might move into user defined data, above. */

	      aprs_morse_code (A, (char*)pinfo, info_len);
	      break;

	    case '}':		/* third party header */

	      third_party_header (A, (char*)pinfo, info_len);
	      break;


	    //case '\r':		/* CR or LF? */
	    //case '\n':
	
	      //break;

	    default:

	      break;
	}


/*
 * Look in other locations if not found in information field.
 */

	if (A->g_symbol_table == ' ' || A->g_symbol_code == ' ') {

	  symbols_from_dest_or_src (*pinfo, A->g_src, dest, &A->g_symbol_table, &A->g_symbol_code);
	}

/*
 * Application might be in the destination field for most message types.
 * MIC-E format has part of location in the destination field.
 */

	switch (*pinfo) {	/* "DTI" data type identifier. */

	  case '\'':		/* Old Mic-E Data */
	  case '`':		/* Current Mic-E Data */
	    break;

	  default:
	    decode_tocall (A, dest);
	    break;
	}
	
} /* end decode_aprs */


void decode_aprs_print (decode_aprs_t *A) {

	char stemp[200];
	//char tmp2[2];
	double absll;
	char news;
	int deg;
	double min;
	char s_lat[30];
	char s_lon[30];
	int n;
	char symbol_description[100];

/*
 * First line has:
 * - message type 
 * - object name
 * - symbol
 * - manufacturer/application
 * - mic-e status
 * - power/height/gain, range
 */
	strlcpy (stemp, A->g_msg_type, sizeof(stemp));

	if (strlen(A->g_name) > 0) {
	  strlcat (stemp, ", \"", sizeof(stemp));
	  strlcat (stemp, A->g_name, sizeof(stemp));
	  strlcat (stemp, "\"", sizeof(stemp));
	}

	if (A->g_symbol_code != ' ') {
	  symbols_get_description (A->g_symbol_table, A->g_symbol_code, symbol_description, sizeof(symbol_description));	
	  strlcat (stemp, ", ", sizeof(stemp));
	  strlcat (stemp, symbol_description, sizeof(stemp));
	}

	if (strlen(A->g_mfr) > 0) {
	  strlcat (stemp, ", ", sizeof(stemp));
	  strlcat (stemp, A->g_mfr, sizeof(stemp));
	}

	if (strlen(A->g_mic_e_status) > 0) {
	  strlcat (stemp, ", ", sizeof(stemp));
	  strlcat (stemp, A->g_mic_e_status, sizeof(stemp));
	}


	if (A->g_power > 0) {
	  char phg[100];

	  /* Protcol spec doesn't mention whether this is dBd or dBi.  */
	  /* Clarified later. */
	  /* http://eng.usna.navy.mil/~bruninga/aprs/aprs11.html */
	  /* "The Antenna Gain in the PHG format on page 28 is in dBi." */

	  snprintf (phg, sizeof(phg), ", %d W height=%d %ddBi %s", A->g_power, A->g_height, A->g_gain, A->g_directivity);
	  strlcat (stemp, phg, sizeof(stemp));
	}

	if (A->g_range > 0) {
	  char rng[100];

	  snprintf (rng, sizeof(rng), ", range=%.1f", A->g_range);
	  strlcat (stemp, rng, sizeof(stemp));
	}
	text_color_set(DW_COLOR_DECODED);
	dw_printf("%s\n", stemp);

/*
 * Second line has:
 * - Latitude
 * - Longitude
 * - speed
 * - direction
 * - altitude
 * - frequency
 */


/*
 * Convert Maidenhead locator to latitude and longitude.
 * 
 * Any example was checked for each hemihemisphere using
 * http://www.amsat.org/cgi-bin/gridconv
 */

	if (strlen(A->g_maidenhead) > 0) {

	  if (A->g_lat == G_UNKNOWN && A->g_lon == G_UNKNOWN) {

	    ll_from_grid_square (A->g_maidenhead, &(A->g_lat), &(A->g_lon));
	  }

	  dw_printf("Grid square = %s, ", A->g_maidenhead);
	}

	strlcpy (stemp, "", sizeof(stemp));

	if (A->g_lat != G_UNKNOWN || A->g_lon != G_UNKNOWN) {

// Have location but it is posible one part is invalid.

	  if (A->g_lat != G_UNKNOWN) {
  
	    if (A->g_lat >= 0) {
	      absll = A->g_lat;
	      news = 'N';
	    }
	    else {
	      absll = - A->g_lat;
	      news = 'S';
	    }
	    deg = (int) absll;
	    min = (absll - deg) * 60.0;
	    snprintf (s_lat, sizeof(s_lat), "%c %02d%s%07.4f", news, deg, CH_DEGREE, min);
	  }
	  else {
	    strlcpy (s_lat, "Invalid Latitude", sizeof(s_lat));
	  }

	  if (A->g_lon != G_UNKNOWN) {

	    if (A->g_lon >= 0) {
	      absll = A->g_lon;
	      news = 'E';
	    }
	    else {
	      absll = - A->g_lon;
	      news = 'W';
	    }
	    deg = (int) absll;
	    min = (absll - deg) * 60.0;
	    snprintf (s_lon, sizeof(s_lon), "%c %03d%s%07.4f", news, deg, CH_DEGREE, min);
	  }
	  else {
	    strlcpy (s_lon, "Invalid Longitude", sizeof(s_lon));
	  }	

	  snprintf (stemp, sizeof(stemp), "%s, %s", s_lat, s_lon);
	}

	if (strlen(A->g_aprstt_loc) > 0) {
	  if (strlen(stemp) > 0) strlcat (stemp, ", ", sizeof(stemp));
	  strlcat (stemp, A->g_aprstt_loc, sizeof(stemp));
	};

	if (A->g_speed_mph != G_UNKNOWN) {
	  char spd[20];

	  if (strlen(stemp) > 0) strlcat (stemp, ", ", sizeof(stemp));
	  snprintf (spd, sizeof(spd), "%.0f MPH", A->g_speed_mph);
	  strlcat (stemp, spd, sizeof(stemp));
	};

	if (A->g_course != G_UNKNOWN) {
	  char cse[20];

	  if (strlen(stemp) > 0) strlcat (stemp, ", ", sizeof(stemp));
	  snprintf (cse, sizeof(cse), "course %.0f", A->g_course);
	  strlcat (stemp, cse, sizeof(stemp));
	};

	if (A->g_altitude_ft != G_UNKNOWN) {
	  char alt[20];

	  if (strlen(stemp) > 0) strlcat (stemp, ", ", sizeof(stemp));
	  snprintf (alt, sizeof(alt), "alt %.0f ft", A->g_altitude_ft);
	  strlcat (stemp, alt, sizeof(stemp));
	};

	if (A->g_freq != G_UNKNOWN) {
	  char ftemp[30];

	  snprintf (ftemp, sizeof(ftemp), ", %.3f MHz", A->g_freq);
	  strlcat (stemp, ftemp, sizeof(stemp));
	}

	if (A->g_offset != G_UNKNOWN) {
	  char ftemp[30];

	  if (A->g_offset % 1000 == 0) {
	    snprintf (ftemp, sizeof(ftemp), ", %+dM", A->g_offset/1000);
	  }
	  else {
	    snprintf (ftemp, sizeof(ftemp), ", %+dk", A->g_offset);
	  }
	  strlcat (stemp, ftemp, sizeof(stemp));
	}

	if (A->g_tone != G_UNKNOWN) {
	  if (A->g_tone == 0) {
	    strlcat (stemp, ", no PL", sizeof(stemp));
	  }
	  else {
	    char ftemp[30];

	    snprintf (ftemp, sizeof(ftemp), ", PL %.1f", A->g_tone);
	    strlcat (stemp, ftemp, sizeof(stemp));
	  }
	}

	if (A->g_dcs != G_UNKNOWN) {

	  char ftemp[30];

	  snprintf (ftemp, sizeof(ftemp), ", DCS %03o", A->g_dcs);
	  strlcat (stemp, ftemp, sizeof(stemp));
	}

	if (strlen (stemp) > 0) {
	  text_color_set(DW_COLOR_DECODED);
	  dw_printf("%s\n", stemp);
	}


/*
 * Finally, any weather and/or comment.
 *
 * Non-printable characters are changed to safe hexadecimal representations.
 * For example, carriage return is displayed as <0x0d>.
 *
 * Drop annoying trailing CR LF.  Anyone who cares can see it in the raw datA->
 */

	n = strlen(A->g_weather);
	if (n >= 1 && A->g_weather[n-1] == '\n') {
	  A->g_weather[n-1] = '\0';
	  n--;
	}
	if (n >= 1 && A->g_weather[n-1] == '\r') {
	  A->g_weather[n-1] = '\0';
	  n--;
	}
	if (n > 0) {  
	  ax25_safe_print (A->g_weather, -1, 0);
	  dw_printf("\n");
	}


	if (strlen(A->g_telemetry) > 0) {
	  ax25_safe_print (A->g_telemetry, -1, 0);
	  dw_printf("\n");
	}


	n = strlen(A->g_comment);
	if (n >= 1 && A->g_comment[n-1] == '\n') {
	  A->g_comment[n-1] = '\0';
	  n--;
	}
	if (n >= 1 && A->g_comment[n-1] == '\r') {
	  A->g_comment[n-1] = '\0';
	  n--;
	}
	if (n > 0) {
	  int j;

	  ax25_safe_print (A->g_comment, -1, 0);
	  dw_printf("\n");

/*
 * Point out incorrect attempts a degree symbol.
 * 0xb0 is degree in ISO Latin1.
 * To be part of a valid UTF-8 sequence, it would need to be preceded by 11xxxxxx or 10xxxxxx.
 * 0xf8 is degree in Microsoft code page 437.
 * To be part of a valid UTF-8 sequence, it would need to be followed by 10xxxxxx.
 */

	  if ( ! A->g_quiet) {

	    for (j=0; j<n; j++) {
	      if ((unsigned)A->g_comment[j] == (char)0xb0 &&  (j == 0 || ! (A->g_comment[j-1] & 0x80))) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf("Character code 0xb0 is probably an attempt at a degree symbol.\n");
	        dw_printf("The correct encoding is 0xc2 0xb0 in UTF-8.\n");
	      }	    	
	    }
	    for (j=0; j<n; j++) {
	      if ((unsigned)A->g_comment[j] == (char)0xf8 && (j == n-1 || (A->g_comment[j+1] & 0xc0) != 0xc0)) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf("Character code 0xf8 is probably an attempt at a degree symbol.\n");
	        dw_printf("The correct encoding is 0xc2 0xb0 in UTF-8.\n");	    	
	      }	
	    }
	  }	
	}
}



/*------------------------------------------------------------------
 *
 * Function:	aprs_ll_pos
 *
 * Purpose:	Decode "Lat/Long Position Report - without Timestamp"
 *
 *		Reports without a timestamp can be regarded as real-time.
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	A->g_lat, A->g_lon, A->g_symbol_table, A->g_symbol_code, A->g_speed_mph, A->g_course, A->g_altitude_ft.
 *
 * Description:	Type identifier '=' has APRS messaging.
 *		Type identifier '!' does not have APRS messaging.
 *
 *		The location can be in either compressed or human-readable form.
 *
 *		When the symbol code is '_' this is a weather report.
 *
 * Examples:	!4309.95NS07307.13W#PHG3320 W2,NY2 Mt Equinox VT k2lm@arrl.net
 *		!4237.14NS07120.83W#
 * 		=4246.40N/07115.15W# {UIV32}
 *
 *		TODO: (?) Special case, DF report when sym table id = '/' and symbol code = '\'.
 *
 * 		=4903.50N/07201.75W\088/036/270/729
 *
 *------------------------------------------------------------------*/

static void aprs_ll_pos (decode_aprs_t *A, unsigned char *info, int ilen) 
{

	struct aprs_ll_pos_s {
	  char dti;			/* ! or = */
	  position_t pos;
	  char comment[43]; 		/* Start of comment could be data extension(s). */
	} *p;

	struct aprs_compressed_pos_s {
	  char dti;			/* ! or = */
	  compressed_position_t cpos;
	  char comment[40]; 		/* No data extension allowed for compressed location. */
	} *q;


	strlcpy (A->g_msg_type, "Position", sizeof(A->g_msg_type));

	p = (struct aprs_ll_pos_s *)info;
	q = (struct aprs_compressed_pos_s *)info;
	
	if (isdigit((unsigned char)(p->pos.lat[0]))) 	/* Human-readable location. */
        {
	  decode_position (A, &(p->pos));

	  if (A->g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, we expect 7 byte "data extension" */
	    /* for the wind direction and speed. */

	    strlcpy (A->g_msg_type, "Weather Report", sizeof(A->g_msg_type));
	    weather_data (A, p->comment, TRUE);
	  } 
	  else {
	    /* Regular position report. */

	    data_extension_comment (A, p->comment);
	  }
	}
	else					/* Compressed location. */
	{
	  decode_compressed_position (A, &(q->cpos));

	  if (A->g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, the wind direction and speed are in the */
	    /* compressed data so we don't expect a 7 byte "data */
	    /* extension" for them. */

	    strlcpy (A->g_msg_type, "Weather Report", sizeof(A->g_msg_type));
	    weather_data (A, q->comment, FALSE);
	  } 
	  else {
	    /* Regular position report. */

	    process_comment (A, q->comment, -1);
	  }
	}


}



/*------------------------------------------------------------------
 *
 * Function:	aprs_ll_pos_time
 *
 * Purpose:	Decode "Lat/Long Position Report - with Timestamp"
 *
 *		Reports sent with a timestamp might contain very old information.
 *
 *		Otherwise, same as above.
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	A->g_lat, A->g_lon, A->g_symbol_table, A->g_symbol_code, A->g_speed_mph, A->g_course, A->g_altitude_ft.
 *
 * Description:	Type identifier '@' has APRS messaging.
 *		Type identifier '/' does not have APRS messaging.
 *
 *		The location can be in either compressed or human-readable form.
 *
 *		When the symbol code is '_' this is a weather report.
 *
 * Examples:	@041025z4232.32N/07058.81W_124/000g000t036r000p000P000b10229h65/wx rpt
 * 		@281621z4237.55N/07120.20W_017/002g006t022r000p000P000h85b10195.Dvs
 *		/092345z4903.50N/07201.75W>Test1234
 *
 * 		I think the symbol code of "_" indicates weather report.
 *
 *		(?) Special case, DF report when sym table id = '/' and symbol code = '\'.
 *
 *		@092345z4903.50N/07201.75W\088/036/270/729
 *		/092345z4903.50N/07201.75W\000/000/270/729
 *
 *------------------------------------------------------------------*/



static void aprs_ll_pos_time (decode_aprs_t *A, unsigned char *info, int ilen) 
{

	struct aprs_ll_pos_time_s {
	  char dti;			/* / or @ */
	  char time_stamp[7];
	  position_t pos;
	  char comment[43]; 		/* First 7 bytes could be data extension. */
	} *p;

	struct aprs_compressed_pos_time_s {
	  char dti;			/* / or @ */
	  char time_stamp[7];
	  compressed_position_t cpos;
	  char comment[40]; 		/* No data extension in this case. */
	} *q;


	strlcpy (A->g_msg_type, "Position with time", sizeof(A->g_msg_type));

	time_t ts = 0;


	p = (struct aprs_ll_pos_time_s *)info;
	q = (struct aprs_compressed_pos_time_s *)info;
	

	if (isdigit((unsigned char)(p->pos.lat[0]))) 		/* Human-readable location. */
        {
	  ts = get_timestamp (A, p->time_stamp);
	  decode_position (A, &(p->pos));

	  if (A->g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, we expect 7 byte "data extension" */
	    /* for the wind direction and speed. */

	    strlcpy (A->g_msg_type, "Weather Report", sizeof(A->g_msg_type));
	    weather_data (A, p->comment, TRUE);
	  } 
	  else {
	    /* Regular position report. */

	    data_extension_comment (A, p->comment);
	  }
	}
	else					/* Compressed location. */
	{
	  ts = get_timestamp (A, p->time_stamp);

	  decode_compressed_position (A, &(q->cpos));

	  if (A->g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, the wind direction and speed are in the */
	    /* compressed data so we don't expect a 7 byte "data */
	    /* extension" for them. */

	    strlcpy (A->g_msg_type, "Weather Report", sizeof(A->g_msg_type));
	    weather_data (A, q->comment, FALSE);
	  } 
	  else {
	    /* Regular position report. */

	    process_comment (A, q->comment, -1);
	  }
	}

	(void)(ts);	// suppress 'set but not used' warning.
}


/*------------------------------------------------------------------
 *
 * Function:	aprs_raw_nmea
 *
 * Purpose:	Decode "Raw NMEA Position Report"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	A-> ...
 *
 * Description:	APRS recognizes raw ASCII data strings conforming to the NMEA 0183
 *		Version 2.0 specification, originating from navigation equipment such 
 *		as GPS and LORAN receivers. It is recommended that APRS stations 
 *		interpret at least the following NMEA Received Sentence types:
 *
 *		GGA Global Positioning System Fix Data
 *		GLL Geographic Position, Latitude/Longitude Data
 *		RMC Recommended Minimum Specific GPS/Transit Data
 *		VTG Velocity and Track Data
 *		WPL Way Point Location
 *
 *		We presently recognize only RMC and GGA.
 *
 * Examples:	$GPGGA,102705,5157.9762,N,00029.3256,W,1,04,2.0,75.7,M,47.6,M,,*62
 *		$GPGLL,2554.459,N,08020.187,W,154027.281,A
 *		$GPRMC,063909,A,3349.4302,N,11700.3721,W,43.022,89.3,291099,13.6,E*52
 *		$GPVTG,318.7,T,,M,35.1,N,65.0,K*69
 *
 *------------------------------------------------------------------*/


static void aprs_raw_nmea (decode_aprs_t *A, unsigned char *info, int ilen) 
{
	if (strncmp((char*)info, "$GPRMC,", 7) == 0)
	{
	  float speed_knots = G_UNKNOWN;

	  (void) dwgpsnmea_gprmc ((char*)info, A->g_quiet, &(A->g_lat), &(A->g_lon), &speed_knots, &(A->g_course));
	  A->g_speed_mph = DW_KNOTS_TO_MPH(speed_knots);
	}
	else if (strncmp((char*)info, "$GPGGA,", 7) == 0)
	{
	  float alt_meters = G_UNKNOWN;
	  int num_sat = 0;

	  (void) dwgpsnmea_gpgga ((char*)info, A->g_quiet, &(A->g_lat), &(A->g_lon), &alt_meters, &num_sat);
	  A->g_altitude_ft = DW_METERS_TO_FEET(alt_meters);
	}

	// TODO (low): add a few other sentence types.

} /* end aprs_raw_nmea */



/*------------------------------------------------------------------
 *
 * Function:	aprs_mic_e
 *
 * Purpose:	Decode MIC-E (also Kenwood D7 & D700) packet.
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	
 *
 * Description:	
 *
 *		Destination Address Field - 
 *
 *		The 7-byte Destination Address field contains
 *		the following encoded information:
 *
 *		* The 6 latitude digits.
 *		* A 3-bit Mic-E message identifier, specifying one of 7 Standard Mic-E
 *		   Message Codes or one of 7 Custom Message Codes or an Emergency
 *		   Message Code.
 *		* The North/South and West/East Indicators.
 *		* The Longitude Offset Indicator.
 *		* The generic APRS digipeater path code.
 *
 *		"Although the destination address appears to be quite unconventional, it is
 *		still a valid AX.25 address, consisting only of printable 7-bit ASCII values."
 *
 * References:	Mic-E TYPE CODES -- http://www.aprs.org/aprs12/mic-e-types.txt
 *
 *		Mic-E TEST EXAMPLES -- http://www.aprs.org/aprs12/mic-e-examples.txt 
 *		
 * Examples:	`b9Z!4y>/>"4N}Paul's_TH-D7
 *
 * TODO:	Destination SSID can contain generic digipeater path.
 *
 * Bugs:	Doesn't handle ambiguous position.  "space" treated as zero.
 *		Invalid data results in a message but latitude is not set to unknown.
 *
 *------------------------------------------------------------------*/

/* a few test cases

# example from http://www.aprs.org/aprs12/mic-e-examples.txt produces 4 errors.
# TODO:  Analyze all the bits someday and possibly report problem with document.

N0CALL>ABCDEF:'abc123R/text

# Let's use an actual valid location and concentrate on the manufacturers
# as listed in http://www.aprs.org/aprs12/mic-e-types.txt

N1ZZN-9>T2SP0W:`c_Vm6hk/`"49}Jeff Mobile_%

N1ZZN-9>T2SP0W:`c_Vm6hk/ "49}Originl Mic-E (leading space)

N1ZZN-9>T2SP0W:`c_Vm6hk/>"49}TH-D7A walkie Talkie
N1ZZN-9>T2SP0W:`c_Vm6hk/>"49}TH-D72 walkie Talkie=
N1ZZN-9>T2SP0W:`c_Vm6hk/]"49}TM-D700 MObile Radio
N1ZZN-9>T2SP0W:`c_Vm6hk/]"49}TM-D710 Mobile Radio=

# Note: next line has trailing space character after _

N1ZZN-9>T2SP0W:`c_Vm6hk/`"49}Yaesu VX-8_ 
N1ZZN-9>T2SP0W:`c_Vm6hk/`"49}Yaesu FTM-350_"
N1ZZN-9>T2SP0W:`c_Vm6hk/`"49}Yaesu VX-8G_#
N1ZZN-9>T2SP0W:`c_Vm6hk/`"49}Yaesu FT1D_$
N1ZZN-9>T2SP0W:`c_Vm6hk/`"49}Yaesu FTM-400DR_%

N1ZZN-9>T2SP0W:'c_Vm6hk/`"49}Byonics TinyTrack3|3
N1ZZN-9>T2SP0W:'c_Vm6hk/`"49}Byonics TinyTrack4|4

# The next group starts with metacharacter "T" which can be any of space > ] ` '
# But space is for original Mic-E, # > and ] are for Kenwood, 
# so ` ' would probably be less ambigous choices but any appear to be valid.

N1ZZN-9>T2SP0W:'c_Vm6hk/`"49}Hamhud\9
N1ZZN-9>T2SP0W:'c_Vm6hk/`"49}Argent/9
N1ZZN-9>T2SP0W:'c_Vm6hk/`"49}HinzTec anyfrog^9
N1ZZN-9>T2SP0W:'c_Vm6hk/`"49}APOZxx www.KissOZ.dk Tracker. OZ1EKD and OZ7HVO*9
N1ZZN-9>T2SP0W:'c_Vm6hk/`"49}OTHER~9


# TODO:  Why is manufacturer unknown?  Should we explicitly say unknown?

[0] VE2VL-9>TU3V0P,VE2PCQ-3,WIDE1,W1UWS-1,UNCAN,WIDE2*:`eB?l")v/"3y}
MIC-E, VAN, En Route

[0] VE2VL-9>TU3U5Q,VE2PCQ-3,WIDE1,W1UWS-1,N1NCI-3,WIDE2*:`eBgl"$v/"42}73 de Julien, Tinytrak 3
MIC-E, VAN, En Route

[0] W1ERB-9>T1SW8P,KB1AEV-15,N1NCI-3,WIDE2*:`dI8l!#j/"3m}
MIC-E, JEEP, In Service

[0] W1ERB-9>T1SW8Q,KB1AEV-15,N1NCI-3,WIDE2*:`dI6l{^j/"4+}IntheJeep..try146.79(PVRA)
"146.79" in comment looks like a frequency in non-standard format.
For most systems to recognize it, use exactly this form "146.790MHz" at beginning of comment.
MIC-E, JEEP, In Service

*/

static int mic_e_digit (decode_aprs_t *A, char c, int mask, int *std_msg, int *cust_msg)
{

 	if (c >= '0' && c <= '9') {
	  return (c - '0');
	}

	if (c >= 'A' && c <= 'J') {
	  *cust_msg |= mask;
	  return (c - 'A');
	}

	if (c >= 'P' && c <= 'Y') {
	  *std_msg |= mask;
	  return (c - 'P');
	}

	/* K, L, Z should be converted to space. */
	/* others are invalid. */
	/* But caller expects only values 0 - 9. */

	if (c == 'K') {
	  *cust_msg |= mask;
	  return (0);
	}

	if (c == 'L') {
	  return (0);
	}

	if (c == 'Z') {
	  *std_msg |= mask;
	  return (0);
	}

	if ( ! A->g_quiet) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character \"%c\" in MIC-E destination/latitude.\n", c);
	}

	return (0);
}


static void aprs_mic_e (decode_aprs_t *A, packet_t pp, unsigned char *info, int ilen) 
{
	struct aprs_mic_e_s {
	  char dti;			/* ' or ` */
	  unsigned char lon[3];		/* "d+28", "m+28", "h+28" */
	  unsigned char speed_course[3];		
	  char symbol_code;
	  char sym_table_id;
	} *p;

	char dest[10];
	int ch;
	int n;
	int offset;
	int std_msg = 0;
	int cust_msg = 0;
	const char *std_text[8] = {"Emergency", "Priority", "Special", "Committed", "Returning", "In Service", "En Route", "Off Duty" };
	const char *cust_text[8] = {"Emergency", "Custom-6", "Custom-5", "Custom-4", "Custom-3", "Custom-2", "Custom-1", "Custom-0" }; 
	unsigned char *pfirst, *plast;

	strlcpy (A->g_msg_type, "MIC-E", sizeof(A->g_msg_type));

	p = (struct aprs_mic_e_s *)info;

/* Destination is really latitude of form ddmmhh. */
/* Message codes are buried in the first 3 digits. */

	ax25_get_addr_with_ssid (pp, AX25_DESTINATION, dest);

	A->g_lat = mic_e_digit(A, dest[0], 4, &std_msg, &cust_msg) * 10 + 
		mic_e_digit(A, dest[1], 2, &std_msg, &cust_msg) +
		(mic_e_digit(A, dest[2], 1, &std_msg, &cust_msg) * 1000 + 
		 mic_e_digit(A, dest[3], 0, &std_msg, &cust_msg) * 100 + 
		 mic_e_digit(A, dest[4], 0, &std_msg, &cust_msg) * 10 + 
		 mic_e_digit(A, dest[5], 0, &std_msg, &cust_msg)) / 6000.0;


/* 4th character of desination indicates north / south. */

	if ((dest[3] >= '0' && dest[3] <= '9') || dest[3] == 'L') {
	  /* South */
	  A->g_lat = ( - A->g_lat);
	}
	else if (dest[3] >= 'P' && dest[3] <= 'Z') 
	{
	  /* North */
	}
	else 
	{
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid MIC-E N/S encoding in 4th character of destination.\n");	  
	  }
	}


/* Longitude is mostly packed into 3 bytes of message but */
/* has a couple bits of information in the destination. */

	if ((dest[4] >= '0' && dest[4] <= '9') || dest[4] == 'L') 
	{
	  offset = 0;
	}
	else if (dest[4] >= 'P' && dest[4] <= 'Z') 
	{
	  offset = 1;
	}
	else 
	{
	  offset = 0;
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid MIC-E Longitude Offset in 5th character of destination.\n");
	  }
	}

/* First character of information field is longitude in degrees. */
/* It is possible for the unprintable DEL character to occur here. */

/* 5th character of desination indicates longitude offset of +100. */
/* Not quite that simple :-( */

	ch = p->lon[0];

	if (offset && ch >= 118 && ch <= 127) 
	{
	    A->g_lon = ch - 118;			/* 0 - 9 degrees */
	}
	else if ( ! offset && ch >= 38 && ch <= 127)
	{
	    A->g_lon = (ch - 38) + 10;		/* 10 - 99 degrees */
	}
	else if (offset && ch >= 108 && ch <= 117)
	{
	    A->g_lon = (ch - 108) + 100;		/* 100 - 109 degrees */
	}
	else if (offset && ch >= 38 && ch <= 107)
	{
	    A->g_lon = (ch - 38) + 110;		/* 110 - 179 degrees */
	}
	else 
	{
	   A->g_lon = G_UNKNOWN;
	   if ( ! A->g_quiet) {
	     text_color_set(DW_COLOR_ERROR);
	     dw_printf("Invalid character 0x%02x for MIC-E Longitude Degrees.\n", ch);
	   }
	}

/* Second character of information field is A->g_longitude minutes. */
/* These are all printable characters. */

/* 
 * More than once I've see the TH-D72A put <0x1a> here and flip between north and south.
 *
 * WB2OSZ>TRSW1R,WIDE1-1,WIDE2-2:`c0ol!O[/>=<0x0d>
 * N 42 37.1200, W 071 20.8300, 0 MPH, course 151
 *
 * WB2OSZ>TRS7QR,WIDE1-1,WIDE2-2:`v<0x1a>n<0x1c>"P[/>=<0x0d>
 * Invalid character 0x1a for MIC-E Longitude Minutes.
 * S 42 37.1200, Invalid Longitude, 0 MPH, course 252
 *
 * This was direct over the air with no opportunity for a digipeater
 * or anything else to corrupt the message.
 */

	if (A->g_lon != G_UNKNOWN) 
	{
	  ch = p->lon[1];

	  if (ch >= 88 && ch <= 97)
	  {
	    A->g_lon += (ch - 88) / 60.0;	/* 0 - 9 minutes*/
	  }
	  else if (ch >= 38 && ch <= 87)
	  {
    	    A->g_lon += ((ch - 38) + 10) / 60.0;	/* 10 - 59 minutes */
	  }
	  else {
	    A->g_lon = G_UNKNOWN;
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Invalid character 0x%02x for MIC-E Longitude Minutes.\n", ch);
	    }
	  }

/* Third character of information field is longitude hundredths of minutes. */
/* There are 100 possible values, from 0 to 99. */
/* Note that the range includes 4 unprintable control characters and DEL. */

	  if (A->g_lon != G_UNKNOWN) 
	  {
	    ch = p->lon[2];

	    if (ch >= 28 && ch <= 127) 
	    {
	      A->g_lon += ((ch - 28) + 0) / 6000.0;	/* 0 - 99 hundredths of minutes*/
	    }
	    else {
	      A->g_lon = G_UNKNOWN;
	      if ( ! A->g_quiet) { 
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf("Invalid character 0x%02x for MIC-E Longitude hundredths of Minutes.\n", ch);
	      }
	    }
	  }
	}

/* 6th character of destintation indicates east / west. */

/*
 * Example of apparently invalid encoding.  6th character missing.
 *
 * [0] KB1HOZ-9>TTRW5,KQ1L-2,WIDE1,KQ1L-8,UNCAN,WIDE2*:`aFo"]|k/]"4m}<0x0d>
 * Invalid character "Invalid MIC-E E/W encoding in 6th character of destination.
 * MIC-E, truck, Kenwood TM-D700, Off Duty
 * N 44 27.5000, E 069 42.8300, 76 MPH, course 196, alt 282 ft
 */

	if ((dest[5] >= '0' && dest[5] <= '9') || dest[5] == 'L') {
	  /* East */
	}
	else if (dest[5] >= 'P' && dest[5] <= 'Z') 
	{
	  /* West */
	  if (A->g_lon != G_UNKNOWN) {
	    A->g_lon = ( - A->g_lon);
	  }
	}
	else 
	{
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid MIC-E E/W encoding in 6th character of destination.\n");	  
	  }
	}

/* Symbol table and codes like everyone else. */

	A->g_symbol_table = p->sym_table_id;
	A->g_symbol_code = p->symbol_code;

	if (A->g_symbol_table != '/' && A->g_symbol_table != '\\' 
		&& ! isupper(A->g_symbol_table) && ! isdigit(A->g_symbol_table))
	{
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid symbol table code not one of / \\ A-Z 0-9\n");	
	  }
	  A->g_symbol_table = '/';
	}

/* Message type from two 3-bit codes. */

	if (std_msg == 0 && cust_msg == 0) {
	  strlcpy (A->g_mic_e_status, "Emergency", sizeof(A->g_mic_e_status));
	}
	else if (std_msg == 0 && cust_msg != 0) {
	  strlcpy (A->g_mic_e_status, cust_text[cust_msg], sizeof(A->g_mic_e_status));
	}
	else if (std_msg != 0 && cust_msg == 0) {
	  strlcpy (A->g_mic_e_status, std_text[std_msg], sizeof(A->g_mic_e_status));
	}
	else {
	  strlcpy (A->g_mic_e_status, "Unknown MIC-E Message Type", sizeof(A->g_mic_e_status));
	}

/* Speed and course from next 3 bytes. */

	n = ((p->speed_course[0] - 28) * 10) + ((p->speed_course[1] - 28) / 10);
	if (n >= 800) n -= 800;

	A->g_speed_mph = DW_KNOTS_TO_MPH(n);

	n = ((p->speed_course[1] - 28) % 10) * 100 + (p->speed_course[2] - 28);
	if (n >= 400) n -= 400;

	/* Result is 0 for unknown and 1 - 360 where 360 is north. */
	/* Convert to 0 - 360 and reserved value for unknown. */

	if (n == 0) 
	  A->g_course = G_UNKNOWN;
	else if (n == 360)
	  A->g_course = 0;
	else
	  A->g_course = n;


/* Now try to pick out manufacturer and other optional items. */
/* The telemetry field, in the original spec, is no longer used. */
  
	strlcpy (A->g_mfr, "Unknown manufacturer", sizeof(A->g_mfr));

	pfirst = info + sizeof(struct aprs_mic_e_s);
	plast = info + ilen - 1;

/* Carriage return character at the end is not mentioned in spec. */
/* Remove if found because it messes up extraction of manufacturer. */
/* Don't drop trailing space because that is used for Yaesu VX-8. */
/* As I recall, the IGate function trims trailing spaces.  */
/* That would be bad for this particular model. Maybe I'm mistaken? */


	if (*plast == '\r') plast--;

#define isT(c) ((c) == ' ' || (c) == '>' || (c) == ']' || (c) == '`' || (c) == '\'')


	if (isT(*pfirst)) {
	
	  if (*pfirst == ' ') { strlcpy (A->g_mfr, "Original MIC-E", sizeof(A->g_mfr)); pfirst++; }

	  else if (*pfirst == '>' && *plast == '=') { strlcpy (A->g_mfr, "Kenwood TH-D72", sizeof(A->g_mfr)); pfirst++; plast--; }
	  else if (*pfirst == '>') { strlcpy (A->g_mfr, "Kenwood TH-D7A", sizeof(A->g_mfr)); pfirst++; }

	  else if (*pfirst == ']' && *plast == '=') { strlcpy (A->g_mfr, "Kenwood TM-D710", sizeof(A->g_mfr)); pfirst++; plast--; }
	  else if (*pfirst == ']') { strlcpy (A->g_mfr, "Kenwood TM-D700", sizeof(A->g_mfr)); pfirst++; }

	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == ' ') { strlcpy (A->g_mfr, "Yaesu VX-8", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == '"') { strlcpy (A->g_mfr, "Yaesu FTM-350", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == '#') { strlcpy (A->g_mfr, "Yaesu VX-8G", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == '$') { strlcpy (A->g_mfr, "Yaesu FT1D", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == '%') { strlcpy (A->g_mfr, "Yaesu FTM-400DR", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == ')') { strlcpy (A->g_mfr, "Yaesu FTM-100D", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == '(') { strlcpy (A->g_mfr, "Yaesu FT2D", sizeof(A->g_mfr)); pfirst++; plast-=2; }

	  else if (*pfirst == '\'' && *(plast-1) == '|' && *plast == '3') { strlcpy (A->g_mfr, "Byonics TinyTrack3", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*pfirst == '\'' && *(plast-1) == '|' && *plast == '4') { strlcpy (A->g_mfr, "Byonics TinyTrack4", sizeof(A->g_mfr)); pfirst++; plast-=2; }

	  else if (*(plast-1) == '\\') { strlcpy (A->g_mfr, "Hamhud ?", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*(plast-1) == '/') { strlcpy (A->g_mfr, "Argent ?", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*(plast-1) == '^') { strlcpy (A->g_mfr, "HinzTec anyfrog", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*(plast-1) == '*') { strlcpy (A->g_mfr, "APOZxx www.KissOZ.dk Tracker. OZ1EKD and OZ7HVO", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*(plast-1) == '~') { strlcpy (A->g_mfr, "OTHER", sizeof(A->g_mfr)); pfirst++; plast-=2; }

	  // Should Original Mic-E and Kenwood be moved down to here?

	  else if (*pfirst == '`') { strlcpy (A->g_mfr, "Mic-Emsg", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	  else if (*pfirst == '\'') { strlcpy (A->g_mfr, "McTrackr", sizeof(A->g_mfr)); pfirst++; plast-=2; }
	}

/*
 * An optional altitude is next.
 * It is three base-91 digits followed by "}".
 * The TM-D710A might have encoding bug.  This was observed:
 *
 * KJ4ETP-9>SUUP9Q,KE4OTZ-3,WIDE1*,WIDE2-1,qAR,KI4HDU-2:`oV$n6:>/]"7&}162.475MHz <Knox,TN> clintserman@gmail=
 * N 35 50.9100, W 083 58.0800, 25 MPH, course 230, alt 945 ft, 162.475MHz
 *
 * KJ4ETP-9>SUUP6Y,GRNTOP-3*,WIDE2-1,qAR,KI4HDU-2:`oU~nT >/]<0x9a>xt}162.475MHz <Knox,TN> clintserman@gmail=
 * Invalid character in MIC-E altitude.  Must be in range of '!' to '{'.
 * N 35 50.6900, W 083 57.9800, 29 MPH, course 204, alt 3280843 ft, 162.475MHz
 *
 * KJ4ETP-9>SUUP6Y,N4NEQ-3,K4EGA-1,WIDE2*,qAS,N5CWH-1:`oU~nT >/]?xt}162.475MHz <Knox,TN> clintserman@gmail=
 * N 35 50.6900, W 083 57.9800, 29 MPH, course 204, alt 808497 ft, 162.475MHz
 *
 * KJ4ETP-9>SUUP2W,KE4OTZ-3,WIDE1*,WIDE2-1,qAR,KI4HDU-2:`oV2o"J>/]"7)}162.475MHz <Knox,TN> clintserman@gmail=
 * N 35 50.2700, W 083 58.2200, 35 MPH, course 246, alt 955 ft, 162.475MHz
 * 
 * Note the <0x9a> which is outside of the 7-bit ASCII range.  Clearly very wrong.
 */

	if (plast > pfirst && pfirst[3] == '}') {

	  A->g_altitude_ft = DW_METERS_TO_FEET((pfirst[0]-33)*91*91 + (pfirst[1]-33)*91 + (pfirst[2]-33) - 10000);

	  if ( ! isdigit91(pfirst[0]) || ! isdigit91(pfirst[1]) || ! isdigit91(pfirst[2])) 
	  {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Invalid character in MIC-E altitude.  Must be in range of '!' to '{'.\n");
	      dw_printf("Bogus altitude of %.0f changed to unknown.\n", A->g_altitude_ft);
	    }
	    A->g_altitude_ft = G_UNKNOWN;
	  }
	  
	  pfirst += 4;
	}

	process_comment (A, (char*)pfirst, (int)(plast - pfirst) + 1);

}


/*------------------------------------------------------------------
 *
 * Function:	aprs_message
 *
 * Purpose:	Decode "Message Format"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *		quiet	- supress error messages.
 *
 * Outputs:	??? TBD
 *
 * Description:	An APRS message is a text string with a specifed addressee.
 *
 *		It's a lot more complicated with different types of addressees
 *		and replies with acknowledgement or rejection.
 *
 *
 * Examples:	...
 *		
 *
 *------------------------------------------------------------------*/

static void aprs_message (decode_aprs_t *A, unsigned char *info, int ilen, int quiet) 
{

	struct aprs_message_s {
	  char dti;			/* : */
	  char addressee[9];
	  char colon;			/* : */
	  char message[73];		/* 0-67 characters for message */
					/* { followed by 1-5 characters for message number */

					/* If the first chracter is '?' it is a Directed Station Query. */
	} *p;

	char addressee[AX25_MAX_ADDR_LEN];
	int i;

	p = (struct aprs_message_s *)info;

	strlcpy (A->g_msg_type, "APRS Message", sizeof(A->g_msg_type));

	if (ilen < 11) {
	  if (! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Message must have a minimum of 11 characters for : addressee :\n");
	  }
	  return;
	}

	if (p->colon != ':') {
	  if (! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Message must begin with : addressee :\n");
	  }
	  return;
	}

	memset (addressee, 0, sizeof(addressee));
	memcpy (addressee, p->addressee, sizeof(p->addressee));	// copy exactly 9 bytes.

	/* Trim trailing spaces. */
	i = strlen(addressee) - 1;
	while (i >= 0 && addressee[i] == ' ') {
	  addressee[i--] = '\0';
	}

	strlcpy (A->g_addressee, addressee, sizeof(A->g_addressee));

/*
 * Special message formats contain telemetry metadata.
 * It applies to the addressee, not the sender.
 * Makes no sense to me that it would not apply to sender instead.
 * Wouldn't the sender be describing his own data?
 * 
 * I also don't understand the reasoning for putting this in a "message."
 * Telemetry data always starts with "#" after the "T" data type indicator.
 * Why not use other characters after the "T" for metadata?
 */

	if (strncmp(p->message,"PARM.",5) == 0) {
	  snprintf (A->g_msg_type, sizeof(A->g_msg_type), "Telemetry Parameter Name Message for \"%s\"", addressee);
	  telemetry_name_message (addressee, p->message+5);
	}
	else if (strncmp(p->message,"UNIT.",5) == 0) {
	  snprintf (A->g_msg_type, sizeof(A->g_msg_type), "Telemetry Unit/Label Message for \"%s\"", addressee);
	  telemetry_unit_label_message (addressee, p->message+5);
	}
	else if (strncmp(p->message,"EQNS.",5) == 0) {
	  snprintf (A->g_msg_type, sizeof(A->g_msg_type), "Telemetry Equation Coefficents Message for \"%s\"", addressee);
	  telemetry_coefficents_message (addressee, p->message+5, quiet);
	}
	else if (strncmp(p->message,"BITS.",5) == 0) {
	  snprintf (A->g_msg_type, sizeof(A->g_msg_type), "Telemetry Bit Sense/Project Name Message for \"%s\"", addressee);
	  telemetry_bit_sense_message (addressee, p->message+5, quiet);
	}

/*
 * If first character of message is "?" it is a query directed toward a specific station.
 */

	else if (p->message[0] == '?') {

	  strlcpy (A->g_msg_type, "Directed Station Query", sizeof(A->g_msg_type));

	  aprs_directed_station_query (A, addressee, p->message+1, quiet);
	}
	else {
	  snprintf (A->g_msg_type, sizeof(A->g_msg_type), "APRS Message for \"%s\"", addressee);

	  /* No location so don't use  process_comment () */

	  strlcpy (A->g_comment, p->message, sizeof(A->g_comment));
	}

}



/*------------------------------------------------------------------
 *
 * Function:	aprs_object
 *
 * Purpose:	Decode "Object Report Format"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	A->g_object_name, A->g_lat, A->g_lon, A->g_symbol_table, A->g_symbol_code, A->g_speed_mph, A->g_course, A->g_altitude_ft.
 *
 * Description:	Message has a 9 character object name which could be quite different than
 *		the source station.
 *
 *		This can also be a weather report when the symbol id is '_'.
 *
 * Examples:	;WA2PNU   *050457z4051.72N/07325.53W]BBS & FlexNet 145.070 MHz
 *
 *		;ActonEOC *070352z4229.20N/07125.95WoFire, EMS, Police, Heli-pad, Dial 911
 *
 *		;IRLPC494@*012112zI9*n*<ONV0   446325-146IDLE<CR>
 *
 *------------------------------------------------------------------*/

static void aprs_object (decode_aprs_t *A, unsigned char *info, int ilen) 
{

	struct aprs_object_s {
	  char dti;			/* ; */
	  char name[9];
	  char live_killed;		/* * for live or _ for killed */
	  char time_stamp[7];
	  position_t pos;
	  char comment[43]; 		/* First 7 bytes could be data extension. */
	} *p;

	struct aprs_compressed_object_s {
	  char dti;			/* ; */
	  char name[9];
	  char live_killed;		/* * for live or _ for killed */
	  char time_stamp[7];
	  compressed_position_t cpos;
	  char comment[40]; 		/* No data extension in this case. */
	} *q;


	time_t ts = 0;
	int i;


	p = (struct aprs_object_s *)info;
	q = (struct aprs_compressed_object_s *)info;

	//assert (sizeof(A->g_name) > sizeof(p->name));

	memset (A->g_name, 0, sizeof(A->g_name));
	memcpy (A->g_name, p->name, sizeof(p->name));	// copy exactly 9 bytes.

	/* Trim trailing spaces. */
	i = strlen(A->g_name) - 1;
	while (i >= 0 && A->g_name[i] == ' ') {
	  A->g_name[i--] = '\0';
	}

 	if (p->live_killed == '*')
	  strlcpy (A->g_msg_type, "Object", sizeof(A->g_msg_type));
	else if (p->live_killed == '_')
	  strlcpy (A->g_msg_type, "Killed Object", sizeof(A->g_msg_type));
	else
	  strlcpy (A->g_msg_type, "Object - invalid live/killed", sizeof(A->g_msg_type));

	ts = get_timestamp (A, p->time_stamp);

	if (isdigit((unsigned char)(p->pos.lat[0]))) 	/* Human-readable location. */
        {
	  decode_position (A, &(p->pos));

	  if (A->g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, we expect 7 byte "data extension" */
	    /* for the wind direction and speed. */

	    strlcpy (A->g_msg_type, "Weather Report with Object", sizeof(A->g_msg_type));
	    weather_data (A, p->comment, TRUE);
	  } 
	  else {
	    /* Regular object. */

	    data_extension_comment (A, p->comment);
	  }
	}
	else					/* Compressed location. */
	{
	  decode_compressed_position (A, &(q->cpos));

	  if (A->g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* The spec doesn't explicitly mention the combination */
	    /* of weather report and object with compressed */
	    /* position. */

	    strlcpy (A->g_msg_type, "Weather Report with Object", sizeof(A->g_msg_type));
	    weather_data (A, q->comment, FALSE);
	  } 
	  else {
	    /* Regular position report. */

	    process_comment (A, q->comment, -1);
	  }
	}

	(void)(ts);

} /* end aprs_object */


/*------------------------------------------------------------------
 *
 * Function:	aprs_item
 *
 * Purpose:	Decode "Item Report Format"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	A->g_object_name, A->g_lat, A->g_lon, A->g_symbol_table, A->g_symbol_code, A->g_speed_mph, A->g_course, A->g_altitude_ft.
 *
 * Description:	An "item" is very much like an "object" except 
 *
 *		-- It doesn't have a time.
 *		-- Name is a VARIABLE length 3 to 9 instead of fixed 9.
 *		-- "live" indicator is ! rather than *
 *
 * Examples:	
 *
 *------------------------------------------------------------------*/

static void aprs_item (decode_aprs_t *A, unsigned char *info, int ilen) 
{

	struct aprs_item_s {
	  char dti;			/* ) */
	  char name[9];			/* Actually variable length 3 - 9 bytes. */
					/* DON'T refer to the rest of this structure; */
					/* the offsets will be wrong! */

	  char live_killed;		/* ! for live or _ for killed */
	  position_t pos;
	  char comment[43]; 		/* First 7 bytes could be data extension. */
	} *p;

	struct aprs_compressed_item_s {
	  char dti;			/* ) */
	  char name[9];			/* Actually variable length 3 - 9 bytes. */
					/* DON'T refer to the rest of this structure; */
					/* the offsets will be wrong! */

	  char live_killed;		/* ! for live or _ for killed */
	  compressed_position_t cpos;
	  char comment[40]; 		/* No data extension in this case. */
	} *q;


	int i;
	char *ppos;


	p = (struct aprs_item_s *)info;
	q = (struct aprs_compressed_item_s *)info;
	(void)(q);

	memset (A->g_name, 0, sizeof(A->g_name));
	i = 0;
	while (i < 9 && p->name[i] != '!' && p->name[i] != '_') {
	  A->g_name[i] = p->name[i];
	  i++;
	  A->g_name[i] = '\0';
	}

	if (p->name[i] == '!')
	  strlcpy (A->g_msg_type, "Item", sizeof(A->g_msg_type));
	else if (p->name[i] == '_')
	  strlcpy (A->g_msg_type, "Killed Item", sizeof(A->g_msg_type));
	else {
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Item name too long or not followed by ! or _.\n");
	  }
	  strlcpy (A->g_msg_type, "Object - invalid live/killed", sizeof(A->g_msg_type));
	}

	ppos = p->name + i + 1;
 
	if (isdigit(*ppos)) 		/* Human-readable location. */
        {
	  decode_position (A, (position_t*) ppos);

	  data_extension_comment (A, ppos + sizeof(position_t));
	}
	else					/* Compressed location. */
	{
	  decode_compressed_position (A, (compressed_position_t*)ppos);

	  process_comment (A, ppos + sizeof(compressed_position_t), -1);
	}

}


/*------------------------------------------------------------------
 *
 * Function:	aprs_station_capabilities
 *
 * Purpose:	Decode "Station Capabilities"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	???
 *
 * Description:	Each capability is a TOKEN or TOKEN=VALUE pair.
 *
 *
 * Example:	<IGATE,MSG_CNT=3,LOC_CNT=49<CR>
 *		
 * Bugs:	Not implemented yet.  Treat whole thing as comment.	
 *
 *------------------------------------------------------------------*/

static void aprs_station_capabilities (decode_aprs_t *A, char *info, int ilen) 
{

	strlcpy (A->g_msg_type, "Station Capabilities", sizeof(A->g_msg_type));

	// 	process_comment() not applicable here because it 
	//	extracts information found in certain formats.

	strlcpy (A->g_comment, info+1, sizeof(A->g_comment));

} /* end aprs_station_capabilities */




/*------------------------------------------------------------------
 *
 * Function:	aprs_status_report
 *
 * Purpose:	Decode "Status Report"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	???
 *
 * Description:	There are 3 different formats:
 *
 *		(1)	'>'
 *			7 char - timestamp, DHM z format
 *			0-55 char - status text
 *
 *		(3)	'>'
 *			4 or 6 char - Maidenhead Locator
 *			2 char - symbol table & code
 *			' ' character
 *			0-53 char - status text	
 *
 *		(2)	'>'
 *			0-62 char - status text
 *
 *		
 *		In all cases, Beam heading and ERP can be at the
 *		very end by using '^' and two other characters.
 *		
 *
 * Examples from specification:	
 *		
 *
 *		>Net Control Center without timestamp.
 *		>092345zNet Control Center with timestamp.
 *		>IO91SX/G
 *		>IO91/G
 *		>IO91SX/- My house 		(Note the space at the start of the status text).
 *		>IO91SX/- ^B7 			Meteor Scatter beam heading = 110 degrees, ERP = 490 watts.
 *	
 *------------------------------------------------------------------*/

static void aprs_status_report (decode_aprs_t *A, char *info, int ilen) 
{
	struct aprs_status_time_s {
	  char dti;			/* > */
	  char ztime[7];		/* Time stamp ddhhmmz */
	  char comment[55]; 		
	} *pt;

	struct aprs_status_m4_s {
	  char dti;			/* > */
	  char mhead4[4];		/* 4 character Maidenhead locator. */
	  char sym_table_id;
	  char symbol_code;
	  char space;			/* Should be space after symbol code. */
	  char comment[54]; 		
	} *pm4;

	struct aprs_status_m6_s {
	  char dti;			/* > */
	  char mhead6[6];		/* 6 character Maidenhead locator. */
	  char sym_table_id;
	  char symbol_code;
	  char space;			/* Should be space after symbol code. */
	  char comment[54]; 		
	} *pm6;

	struct aprs_status_s {
	  char dti;			/* > */
	  char comment[62]; 		
	} *ps;


	strlcpy (A->g_msg_type, "Status Report", sizeof(A->g_msg_type));

	pt = (struct aprs_status_time_s *)info;
	pm4 = (struct aprs_status_m4_s *)info;
	pm6 = (struct aprs_status_m6_s *)info;
	ps = (struct aprs_status_s *)info;

/*
 * Do we have format with time?
 */
	if (isdigit(pt->ztime[0]) &&
	    isdigit(pt->ztime[1]) &&
	    isdigit(pt->ztime[2]) &&
	    isdigit(pt->ztime[3]) &&
	    isdigit(pt->ztime[4]) &&
	    isdigit(pt->ztime[5]) &&
	    pt->ztime[6] == 'z') {

	  // 	process_comment() not applicable here because it 
	  //	extracts information found in certain formats.

	  strlcpy (A->g_comment, pt->comment, sizeof(A->g_comment));
	}

/*
 * Do we have format with 6 character Maidenhead locator?
 */
	else if (get_maidenhead (A, pm6->mhead6) == 6) {

	  memset (A->g_maidenhead, 0, sizeof(A->g_maidenhead));
	  memcpy (A->g_maidenhead, pm6->mhead6, sizeof(pm6->mhead6));

	  A->g_symbol_table = pm6->sym_table_id;
	  A->g_symbol_code = pm6->symbol_code;

	  if (A->g_symbol_table != '/' && A->g_symbol_table != '\\' 
		&& ! isupper(A->g_symbol_table) && ! isdigit(A->g_symbol_table))
	  {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Invalid symbol table code '%c' not one of / \\ A-Z 0-9\n", A->g_symbol_table);	
	    }
	    A->g_symbol_table = '/';
	  }

	  if (pm6->space != ' ' && pm6->space != '\0') {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Error: Found '%c' instead of space required after symbol code.\n", pm6->space);
	    }	
	  }

	  // 	process_comment() not applicable here because it 
	  //	extracts information found in certain formats.

	  strlcpy (A->g_comment, pm6->comment, sizeof(A->g_comment));
	}

/*
 * Do we have format with 4 character Maidenhead locator?
 */
	else if (get_maidenhead (A, pm4->mhead4) == 4) {

	  memset (A->g_maidenhead, 0, sizeof(A->g_maidenhead));
	  memcpy (A->g_maidenhead, pm4->mhead4, sizeof(pm4->mhead4));

	  A->g_symbol_table = pm4->sym_table_id;
	  A->g_symbol_code = pm4->symbol_code;

	  if (A->g_symbol_table != '/' && A->g_symbol_table != '\\' 
		&& ! isupper(A->g_symbol_table) && ! isdigit(A->g_symbol_table))
	  {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Invalid symbol table code '%c' not one of / \\ A-Z 0-9\n", A->g_symbol_table);	
	    }
	    A->g_symbol_table = '/';
	  }

	  if (pm4->space != ' ' && pm4->space != '\0') {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Error: Found '%c' instead of space required after symbol code.\n", pm4->space);
	    }
	  }

	  // 	process_comment() not applicable here because it 
	  //	extracts information found in certain formats.

	  strlcpy (A->g_comment, pm4->comment, sizeof(A->g_comment));
	}

/*
 * Whole thing is status text.
 */
	else {
	  strlcpy (A->g_comment, ps->comment, sizeof(A->g_comment));
	}


/*
 * Last 3 characters can represent beam heading and ERP.
 */

	if (strlen(A->g_comment) >= 3) {
	  char *hp = A->g_comment + strlen(A->g_comment) - 3;
	
	  if (*hp == '^') {

	    char h = hp[1];
	    char p = hp[2];
	    int beam = -1;
	    int erp = -1;

	    if (h >= '0' && h <= '9') {
	      beam = (h - '0') * 10;
	    }
	    else if (h >= 'A' && h <= 'Z') {
	      beam = (h - 'A') * 10 + 100;
	    }

	    if (p >= '1' && p <= 'K') {
	      erp = (p - '0') * (p - '0') * 10;
	    }

	// TODO (low):  put result somewhere.
	// could use A->g_directivity and need new variable for erp.

	    *hp = '\0';

	    (void)(beam);
	    (void)(erp);
	  }
	}

} /* end aprs_status_report */


/*------------------------------------------------------------------
 *
 * Function:	aprs_general_query
 *
 * Purpose:	Decode "General Query" for all stations.
 *
 * Inputs:	info 	- Pointer to Information field.  First character should be "?".
 *		ilen 	- Information field length.
 *		quiet	- suppress error messages.
 *
 * Outputs:	A	- Decoded packet structure
 *				A->g_query_type
 *				A->g_query_lat		(optional)
 *				A->g_query_lon		(optional)
 *				A->g_query_radius	(optional)
 *
 * Description:	Formats are:
 *	
 *			?query?
 *			?query?lat,long,radius
 *
 *		'query' is one of APRS, IGATE, WX, ...
 *		optional footprint, in degrees and miles radius, means only
 *			those in the specified circle should respond.
 *
 * Examples from specification, Chapter 15:		
 *
 *		?APRS?
 *		?APRS? 34.02,-117.15,0200
 *		?IGATE?
 *	
 *------------------------------------------------------------------*/

static void aprs_general_query (decode_aprs_t *A, char *info, int ilen, int quiet) 
{
	char *q2;
	char *p;
	char *tok;
	char stemp[256];		
	double lat, lon;
	float radius;

	strlcpy (A->g_msg_type, "General Query", sizeof(A->g_msg_type));

/*
 * First make a copy because we will modify it while parsing it.
 */

	strlcpy (stemp, info, sizeof(stemp));

/*
 * There should be another "?" after the query type.
 */
	q2 = strchr(stemp+1, '?');
	if (q2 == NULL) {
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("General Query must have ? after the query type.\n");
	  }
	  return; 
	}

	*q2 = '\0';
	strlcpy (A->g_query_type, stemp+1, sizeof(A->g_query_type));

// TODO: remove debug

	text_color_set(DW_COLOR_DEBUG);
	dw_printf("DEBUG: General Query type = \"%s\"\n", A->g_query_type);

	p = q2 + 1;
	if (strlen(p) == 0) {
	  return;
	}

/*
 * Try to extract footprint.
 * Spec says positive coordinate would be preceded by space
 * and radius must be exactly 4 digits.  We are more forgiving. 
 */
	tok = strsep(&p, ",");
	if (tok != NULL) {
	  lat = atof(tok);
	  tok = strsep(&p, ",");
	  if (tok != NULL) {
	    lon = atof(tok);
	    tok = strsep(&p, ",");
	    if (tok != NULL) {
	      radius = atof(tok);

	      if (lat < -90 || lat > 90) {
	        if ( ! A->g_quiet) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf("Invalid latitude for General Query footprint.\n");
	        }
	        return; 
	      }

	      if (lon < -180 || lon > 180) {
	        if ( ! A->g_quiet) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf("Invalid longitude for General Query footprint.\n");
	        }
	        return; 
	      }

	      if (radius <= 0 || radius > 9999) {
	        if ( ! A->g_quiet) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf("Invalid radius for General Query footprint.\n");
	        }
	        return; 
	      }

	      A->g_footprint_lat = lat;
	      A->g_footprint_lon = lon;
	      A->g_footprint_radius = radius;
	    }
	    else {
	      if ( ! A->g_quiet) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf("Can't get radius for General Query footprint.\n");
	      }
	      return;
	    }
	  }
	  else {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Can't get longitude for General Query footprint.\n");
	    }
	    return;
	  }
	}
	else {
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Can't get latitude for General Query footprint.\n");
	  }
	  return;
	}
	
// TODO: remove debug

	text_color_set(DW_COLOR_DEBUG);
	dw_printf("DEBUG: General Query footprint = %.6f %.6f %.2f\n", lat, lon, radius);


} /* end aprs_general_query */



/*------------------------------------------------------------------
 *
 * Function:	aprs_directed_station_query
 *
 * Purpose:	Decode "Directed Station Query" aimed at specific station.
 *		This is actually a special format of the more general "message."
 *
 * Inputs:	addressee	- To whom it is directed.
 *				  Redundant because it is already in A->addressee.
 *
 *		query	 	- What's left over after ":addressee:?" in info part.
 *
 *		quiet		- suppress error messages.
 *
 * Outputs:	A	- Decoded packet structure
 *				A->g_query_type
 *				A->g_query_callsign	(optional)
 *
 * Description:	The caller has already removed the :addressee:? part so we are left 
 *		with a query type of exactly 5 characters and optional "callsign 
 *		of heard station."
 *	
 * Examples from specification, Chapter 15.   Our "query" argument.	
 *
 *		:KH2Z     :?APRSD		APRSD
 *		:KH2Z     :?APRSHVN0QBF     	APRSHVN0QBF
 *		:KH2Z     :?APRST		APRST
 *		:KH2Z     :?PING?		PING?
 *	
 *		"PING?" contains "?" only to pad it out to exactly 5 characters.
 *
 *------------------------------------------------------------------*/

static void aprs_directed_station_query (decode_aprs_t *A, char *addressee, char *query, int quiet)
{
	//char query_type[20];		/* Does the query type always need to be exactly 5 characters? */
					/* If not, how would we know where the extra optional information starts? */

	//char callsign[AX25_MAX_ADDR_LEN];

	//if (strlen(query) < 5) ...


}  /* end aprs_directed_station_query */



/*------------------------------------------------------------------
 *
 * Function:	aprs_Telemetry
 *
 * Purpose:	Decode "Telemetry"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *		quiet	- suppress error messages.
 *
 * Outputs:	A->g_telemetry
 *		A->g_comment
 *
 * Description:	TBD.
 *
 * Examples from specification:	
 *		
 *
 *		TBD
 *	
 *------------------------------------------------------------------*/

static void aprs_telemetry (decode_aprs_t *A, char *info, int ilen, int quiet) 
{

	strlcpy (A->g_msg_type, "Telemetry", sizeof(A->g_msg_type));

	telemetry_data_original (A->g_src, info, quiet, A->g_telemetry, sizeof(A->g_telemetry), A->g_comment, sizeof(A->g_comment));


} /* end aprs_telemetry */


/*------------------------------------------------------------------
 *
 * Function:	aprs_raw_touch_tone
 *
 * Purpose:	Decode raw touch tone datA->
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Description:	Touch tone data is converted to a packet format
 *		so it can be conveyed to an application for processing.
 *
 * 		This is not part of the APRS standard.	
 *		
 *------------------------------------------------------------------*/

static void aprs_raw_touch_tone (decode_aprs_t *A, char *info, int ilen) 
{

	strlcpy (A->g_msg_type, "Raw Touch Tone Data", sizeof(A->g_msg_type));

	/* Just copy the info field without the message type. */

	if (*info == '{') 
	  strlcpy (A->g_comment, info+3, sizeof(A->g_comment));
	else
	  strlcpy (A->g_comment, info+1, sizeof(A->g_comment));


} /* end aprs_raw_touch_tone */



/*------------------------------------------------------------------
 *
 * Function:	aprs_morse_code
 *
 * Purpose:	Convey message in packet format to be transmitted as 
 *		Morse Code.
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Description:	This is not part of the APRS standard.	
 *		
 *------------------------------------------------------------------*/

static void aprs_morse_code (decode_aprs_t *A, char *info, int ilen) 
{

	strlcpy (A->g_msg_type, "Morse Code Data", sizeof(A->g_msg_type));

	/* Just copy the info field without the message type. */

	if (*info == '{') 
	  strlcpy (A->g_comment, info+3, sizeof(A->g_comment));
	else
	  strlcpy (A->g_comment, info+1, sizeof(A->g_comment));


} /* end aprs_morse_code */


/*------------------------------------------------------------------
 *
 * Function:	aprs_ll_pos_time
 *
 * Purpose:	Decode weather report without a position.
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	A->g_symbol_table, A->g_symbol_code.
 *
 * Description:	Type identifier '_' is a weather report without a position.
 *
 *------------------------------------------------------------------*/



static void aprs_positionless_weather_report (decode_aprs_t *A, unsigned char *info, int ilen) 
{

	struct aprs_positionless_weather_s {
	  char dti;			/* _ */
	  char time_stamp[8];		/* MDHM format */
	  char comment[99]; 		
	} *p;


	strlcpy (A->g_msg_type, "Positionless Weather Report", sizeof(A->g_msg_type));

	//time_t ts = 0;


	p = (struct aprs_positionless_weather_s *)info;
	
	// not yet implemented for 8 character format // ts = get_timestamp (A, p->time_stamp);

	weather_data (A, p->comment, FALSE);
}


/*------------------------------------------------------------------
 *
 * Function:	weather_data
 *
 * Purpose:	Decode weather data in position or object report.
 *
 * Inputs:	info 	- Pointer to first byte after location
 *			  and symbol code.
 *
 *		wind_prefix 	- Expecting leading wind info
 *				  for human-readable location.
 *				  (Currently ignored.  We are very
 *				  forgiving in what is accepted.)
 * TODO: call this context instead and have 3 enumerated values.
 *
 * Global In:	A->g_course	- Wind info for compressed location.
 *		A->g_speed_mph
 *
 * Outputs:	A->g_weather
 *
 * Description:	Extract weather details and format into a comment.
 *
 *		For human-readable locations, we expect wind direction
 *		and speed in a format like this:  999/999.
 *		For compressed location, this has already been 
 * 		processed and put in A->g_course and A->g_speed_mph.
 *		Otherwise, for positionless weather data, the 
 *		wind is in the form c999s999.
 *
 * References:	APRS Weather specification comments.
 *		http://aprs.org/aprs11/spec-wx.txt
 *
 *		Weather updates to the spec.
 *		http://aprs.org/aprs12/weather-new.txt
 *
 * Examples:
 *	
 *	_10090556c220s004g005t077r000p000P000h50b09900wRSW
 *	!4903.50N/07201.75W_220/004g005t077r000p000P000h50b09900wRSW
 *	!4903.50N/07201.75W_220/004g005t077r000p000P000h50b.....wRSW
 *	@092345z4903.50N/07201.75W_220/004g005t-07r000p000P000h50b09900wRSW
 *	=/5L!!<*e7_7P[g005t077r000p000P000h50b09900wRSW
 *	@092345z/5L!!<*e7_7P[g005t077r000p000P000h50b09900wRSW
 *	;BRENDA   *092345z4903.50N/07201.75W_220/004g005b0990
 *
 *------------------------------------------------------------------*/

static int getwdata (char **wpp, char ch, int dlen, float *val) 
{
	char stemp[8];	// larger than maximum dlen.
	int i;


	//dw_printf("debug: getwdata (wp=%p, ch=%c, dlen=%d)\n", *wpp, ch, dlen);

	*val = G_UNKNOWN;

	assert (dlen >= 2 && dlen <= 6);

	if (**wpp != ch) {
	  /* Not specified element identifier. */
	  return (0);	
	}
	
	if (strncmp((*wpp)+1, "......", dlen) == 0 || strncmp((*wpp)+1, "      ", dlen) == 0) {
	  /* Field present, unknown value */
	  *wpp += 1 + dlen;
	  return (1); 
	}

	/* Data field can contain digits, decimal point, leading negative. */

	for (i=1; i<=dlen; i++) {
	  if ( ! isdigit((*wpp)[i]) && (*wpp)[i] != '.' && (*wpp)[i] != '-' ) {
	    return(0);
	  }
	} 

	memset (stemp, 0, sizeof(stemp));
	memcpy (stemp, (*wpp)+1, dlen);
	*val = atof(stemp);

	//dw_printf("debug: getwdata returning %f\n", *val);

	*wpp += 1 + dlen;
	return (1); 
}	

static void weather_data (decode_aprs_t *A, char *wdata, int wind_prefix) 
{
	int n;
	float fval;
	char *wp = wdata;
	int keep_going;

	
	if (wp[3] == '/')
	{
	  if (sscanf (wp, "%3d", &n))
	  {
	    // Data Extension format.
	    // Fine point:  Officially, should be values of 001-360.
	    // "000" or "..." or "   " means unknown. 
	    // In practice we see do see "000" here.
	    A->g_course = n;
	  }
	  if (sscanf (wp+4, "%3d", &n))
	  {
	    A->g_speed_mph = DW_KNOTS_TO_MPH(n);  /* yes, in knots */
	  }
	  wp += 7;
	}
	else if ( A->g_speed_mph == G_UNKNOWN) {

	  if ( ! getwdata (&wp, 'c', 3, &A->g_course)) {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Didn't find wind direction in form c999.\n");
	    }
	  }
	  if ( ! getwdata (&wp, 's', 3, &A->g_speed_mph)) {	/* MPH here */
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Didn't find wind speed in form s999.\n");
	    }
	  }
	}

// At this point, we should have the wind direction and speed
// from one of three methods.

	if (A->g_speed_mph != G_UNKNOWN) {

	  snprintf (A->g_weather, sizeof(A->g_weather), "wind %.1f mph", A->g_speed_mph);
	  if (A->g_course != G_UNKNOWN) {
	    char ctemp[40];
	    snprintf (ctemp, sizeof(ctemp), ", direction %.0f", A->g_course);
	    strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	  }
	}

	/* We don't want this to show up on the location line. */
	A->g_speed_mph = G_UNKNOWN;
	A->g_course = G_UNKNOWN;

/*
 * After the mandatory wind direction and speed (in 1 of 3 formats), the
 * next two must be in fixed positions:
 * - gust (peak in mph last 5 minutes)
 * - temperature, degrees F, can be negative e.g. -01
 */
	if (getwdata (&wp, 'g', 3, &fval)) {
	  if (fval != G_UNKNOWN) {
	    char ctemp[40];
	    snprintf (ctemp, sizeof(ctemp), ", gust %.0f", fval);
	    strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	  }
	}
	else {
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Didn't find wind gust in form g999.\n");
	  }
	}

	if (getwdata (&wp, 't', 3, &fval)) {
	  if (fval != G_UNKNOWN) {
	    char ctemp[40];
	    snprintf (ctemp, sizeof(ctemp), ", temperature %.0f", fval);
	    strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	  }
	}
	else {
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Didn't find temperature in form t999.\n");
	  }
	}

/*
 * Now pick out other optional fields in any order.
 */
	keep_going = 1;
	while (keep_going) {

	  if (getwdata (&wp, 'r', 3, &fval)) {	

	/* r = rainfall, 1/100 inch, last hour */

	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      snprintf (ctemp, sizeof(ctemp), ", rain %.2f in last hour", fval / 100.);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 'p', 3, &fval)) {	

	/* p = rainfall, 1/100 inch, last 24 hours */

	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      snprintf (ctemp, sizeof(ctemp), ", rain %.2f in last 24 hours", fval / 100.);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 'P', 3, &fval)) {	

	/* P = rainfall, 1/100 inch, since midnight */

	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      snprintf (ctemp, sizeof(ctemp), ", rain %.2f since midnight", fval / 100.);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 'h', 2, &fval)) {	

	/* h = humidity %, 00 means 100%  */

	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      if (fval == 0) fval = 100;
	      snprintf (ctemp, sizeof(ctemp), ", humidity %.0f", fval);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 'b', 5, &fval)) {	

	/* b = barometric presure (tenths millibars / tenths of hPascal)  */
	/* Here, display as inches of mercury. */

	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      fval = DW_MBAR_TO_INHG(fval * 0.1);
	      snprintf (ctemp, sizeof(ctemp), ", barometer %.2f", fval);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 'L', 3, &fval)) {	

	/* L = Luminosity, watts/ sq meter, 000-999  */
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      snprintf (ctemp, sizeof(ctemp), ", %.0f watts/m^2", fval);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 'l', 3, &fval)) {	

	/* l = Luminosity, watts/ sq meter, 1000-1999  */
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      snprintf (ctemp, sizeof(ctemp), ", %.0f watts/m^2", fval + 1000);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 's', 3, &fval)) {	

	/* s = Snowfall in last 24 hours, inches  */
	/* Data can have decimal point so we don't have to worry about scaling. */
	/* 's' is also used by wind speed but that must be in a fixed */
	/* position in the message so there is no confusion. */
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      snprintf (ctemp, sizeof(ctemp), ", %.1f snow in 24 hours", fval);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 's', 3, &fval)) {	

	/* # = Raw rain counter  */
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      snprintf (ctemp, sizeof(ctemp), ", raw rain counter %.f", fval);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }
	  else if (getwdata (&wp, 'X', 3, &fval)) {	

	/* X = Nuclear Radiation.  */
	/* Encoded as two significant digits and order of magnitude */
	/* like resistor color code. */

// TODO: decode this properly
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[40];
	      snprintf (ctemp, sizeof(ctemp), ", nuclear Radiation %.f", fval);
	      strlcat (A->g_weather, ctemp, sizeof(A->g_weather));
	    }
	  }

// TODO: add new flood level, battery voltage, etc.

	  else {
	    keep_going = 0;
	  }
	}

/*
 * We should be left over with:
 * - one character for software.
 * - two to four characters for weather station type.
 * Examples: tU2k, wRSW
 *
 * But few people follow the protocol spec here.  Instead more often we see things like:
 *  sunny/WX
 *  / {UIV32N}
 */

	strlcat (A->g_weather, ", \"", sizeof(A->g_weather));
	strlcat (A->g_weather, wp, sizeof(A->g_weather));
/*
 * Drop any CR / LF character at the end.
 */
	n = strlen(A->g_weather);
	if (n >= 1 && A->g_weather[n-1] == '\n') {
	  A->g_weather[n-1] = '\0';
	}

	n = strlen(A->g_weather);
	if (n >= 1 && A->g_weather[n-1] == '\r') {
	  A->g_weather[n-1] = '\0';
	}

	strlcat (A->g_weather, "\"", sizeof(A->g_weather));

	return;

} /* end weather_data */


/*------------------------------------------------------------------
 *
 * Function:	aprs_ultimeter
 *
 * Purpose:	Decode Peet Brothers ULTIMETER Weather Station Info.
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	A->g_weather
 *
 * Description:	http://www.peetbros.com/shop/custom.aspx?recid=7 
 *
 * 		There are two different data formats in use.
 *		One begins with $ULTW and is called "Packet Mode."  Example:
 *
 *		$ULTW009400DC00E21B8027730008890200010309001E02100000004C<CR><LF>
 *
 *		The other begins with !! and is called "logging mode."  Example:
 *
 *		!!000000A600B50000----------------001C01D500000017<CR><LF>
 *
 *
 * Bugs:	Implementation is incomplete.
 *		The example shown in the APRS protocol spec has a couple "----"
 *		fields in the $ULTW message.  This should be rewritten to handle
 *		each field separately to deal with missing pieces.
 *
 *------------------------------------------------------------------*/

static void aprs_ultimeter (decode_aprs_t *A, char *info, int ilen) 
{

				// Header = $ULTW 
				// Data Fields 
	short h_windpeak;	// 1. Wind Speed Peak over last 5 min. (0.1 kph) 
	short h_wdir;		// 2. Wind Direction of Wind Speed Peak (0-255) 
	short h_otemp;		// 3. Current Outdoor Temp (0.1 deg F) 
	short h_totrain;	// 4. Rain Long Term Total (0.01 in.) 
	short h_baro;		// 5. Current Barometer (0.1 mbar) 
	short h_barodelta;	// 6. Barometer Delta Value(0.1 mbar) 
	short h_barocorrl;	// 7. Barometer Corr. Factor(LSW) 
	short h_barocorrm;	// 8. Barometer Corr. Factor(MSW) 
	short h_ohumid;		// 9. Current Outdoor Humidity (0.1%) 
	short h_date;		// 10. Date (day of year) 
	short h_time;		// 11. Time (minute of day) 
	short h_raintoday;	// 12. Today's Rain Total (0.01 inches)* 
	short h_windave;	// 13. 5 Minute Wind Speed Average (0.1kph)* 
				// Carriage Return & Line Feed
				// *Some instruments may not include field 13, some may 
				// not include 12 or 13. 
				// Total size: 44, 48 or 52 characters (hex digits) + 
				// header, carriage return and line feed. 

	int n;

	strlcpy (A->g_msg_type, "Ultimeter", sizeof(A->g_msg_type));

	if (*info == '$')
 	{
	  n = sscanf (info+5, "%4hx%4hx%4hx%4hx%4hx%4hx%4hx%4hx%4hx%4hx%4hx%4hx%4hx",
			&h_windpeak,		
			&h_wdir,		
			&h_otemp,
			&h_totrain,		
			&h_baro,		
			&h_barodelta,	
			&h_barocorrl,	
			&h_barocorrm,	
			&h_ohumid,		 
			&h_date,		
			&h_time,		
			&h_raintoday,	 	// not on some models.
			&h_windave);		// not on some models.

	  if (n >= 11 && n <= 13) {

	    float windpeak, wdir, otemp, baro, ohumid;

	    windpeak = DW_KM_TO_MILES(h_windpeak * 0.1);
	    wdir = (h_wdir & 0xff) * 360. / 256.;
	    otemp = h_otemp * 0.1;
	    baro = DW_MBAR_TO_INHG(h_baro * 0.1);
	    ohumid = h_ohumid * 0.1;
	  
	    snprintf (A->g_weather, sizeof(A->g_weather), "wind %.1f mph, direction %.0f, temperature %.1f, barometer %.2f, humidity %.0f",
			windpeak, wdir, otemp, baro, ohumid);
	  }
	}

	
		// Header = !! 
		// Data Fields 
		// 1. Wind Speed (0.1 kph) 
		// 2. Wind Direction (0-255) 
		// 3. Outdoor Temp (0.1 deg F) 
		// 4. Rain* Long Term Total (0.01 inches)  
		// 5. Barometer (0.1 mbar) 	[ can be ---- ]
		// 6. Indoor Temp (0.1 deg F) 	[ can be ---- ]
		// 7. Outdoor Humidity (0.1%) 	[ can be ---- ]
		// 8. Indoor Humidity (0.1%) 	[ can be ---- ]
		// 9. Date (day of year) 
		// 10. Time (minute of day) 
		// 11. Today's Rain Total (0.01 inches)* 
		// 12. 1 Minute Wind Speed Average (0.1kph)* 
		// Carriage Return & Line Feed 
		//
		// *Some instruments may not include field 12, some may not include 11 or 12. 
		// Total size: 40, 44 or 48 characters (hex digits) + header, carriage return and line feed

	if (*info == '!')
 	{
	  n = sscanf (info+2, "%4hx%4hx%4hx%4hx",
			&h_windpeak,		
			&h_wdir,		
			&h_otemp,
			&h_totrain);

	  if (n == 4) {

	    float windpeak, wdir, otemp;

	    windpeak = DW_KM_TO_MILES(h_windpeak * 0.1);
	    wdir = (h_wdir & 0xff) * 360. / 256.;
	    otemp = h_otemp * 0.1;
	  
	    snprintf (A->g_weather, sizeof(A->g_weather), "wind %.1f mph, direction %.0f, temperature %.1f\n",
			windpeak, wdir, otemp);
	  }

	}

} /* end aprs_ultimeter */


/*------------------------------------------------------------------
 *
 * Function:	third_party_header
 *
 * Purpose:	Decode packet from a third party network.
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	A->g_comment
 *
 * Description:	
 *
 *------------------------------------------------------------------*/

static void third_party_header (decode_aprs_t *A, char *info, int ilen) 
{

	strlcpy (A->g_msg_type, "Third Party Header", sizeof(A->g_msg_type));

	/* more later? */

} /* end third_party_header */



/*------------------------------------------------------------------
 *
 * Function:	decode_position
 *
 * Purpose:	Decode the position & symbol information common to many message formats.
 *
 * Inputs:	ppos 	- Pointer to position & symbol fields.
 *
 * Returns:	A->g_lat
 *		A->g_lon
 *		A->g_symbol_table
 *		A->g_symbol_code
 *
 * Description:	This provides resolution of about 60 feet.
 *		This can be improved by using !DAO! in the comment.
 *
 *------------------------------------------------------------------*/


static void decode_position (decode_aprs_t *A, position_t *ppos)
{

	  A->g_lat = get_latitude_8 (ppos->lat, A->g_quiet);
	  A->g_lon = get_longitude_9 (ppos->lon, A->g_quiet);

	  A->g_symbol_table = ppos->sym_table_id;
	  A->g_symbol_code = ppos->symbol_code;
}

/*------------------------------------------------------------------
 *
 * Function:	decode_compressed_position
 *
 * Purpose:	Decode the compressed position & symbol information common to many message formats.
 *
 * Inputs:	ppos 	- Pointer to compressed position & symbol fields.
 *
 * Returns:	A->g_lat
 *		A->g_lon
 *		A->g_symbol_table
 *		A->g_symbol_code
 *
 *		One of the following:
 *			A->g_course & A->g_speeed
 *			A->g_altitude_ft
 *			A->g_range
 *
 * Description:	The compressed position provides resolution of around ???
 *		This also includes course/speed or altitude.
 *
 *		It contains 13 bytes of the format:
 *
 *			symbol table	/, \, or overlay A-Z, a-j is mapped into 0-9
 *
 *			yyyy		Latitude, base 91.
 * 
 *			xxxx		Longitude, base 91.
 *
 *			symbol code
 *
 *			cs		Course/Speed or altitude.
 *
 *			t		Various "type" info.
 *
 *------------------------------------------------------------------*/


static void decode_compressed_position (decode_aprs_t *A, compressed_position_t *pcpos)
{
	if (isdigit91(pcpos->y[0]) && isdigit91(pcpos->y[1]) && isdigit91(pcpos->y[2]) && isdigit91(pcpos->y[3]))
	{
	  A->g_lat = 90 - ((pcpos->y[0]-33)*91*91*91 + (pcpos->y[1]-33)*91*91 + (pcpos->y[2]-33)*91 + (pcpos->y[3]-33)) / 380926.0;
	}
	else
 	{
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in compressed latitude.  Must be in range of '!' to '{'.\n");
	  }
	  A->g_lat = G_UNKNOWN;
	}
	  
	if (isdigit91(pcpos->x[0]) && isdigit91(pcpos->x[1]) && isdigit91(pcpos->x[2]) && isdigit91(pcpos->x[3]))
	{
	  A->g_lon = -180 + ((pcpos->x[0]-33)*91*91*91 + (pcpos->x[1]-33)*91*91 + (pcpos->x[2]-33)*91 + (pcpos->x[3]-33)) / 190463.0;
	}
	else 
	{
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in compressed longitude.  Must be in range of '!' to '{'.\n");
	  }
	  A->g_lon = G_UNKNOWN;
	}

	if (pcpos->sym_table_id == '/' || pcpos->sym_table_id == '\\' || isupper((int)(pcpos->sym_table_id))) {
	  /* primary or alternate or alternate with upper case overlay. */
	  A->g_symbol_table = pcpos->sym_table_id;
   	}
	else if (pcpos->sym_table_id >= 'a' && pcpos->sym_table_id <= 'j') {
	  /* Lower case a-j are used to represent overlay characters 0-9 */
	  /* because a digit here would mean normal (non-compressed) location. */
	  A->g_symbol_table = pcpos->sym_table_id - 'a' + '0';
	}
	else {
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid symbol table id for compressed position.\n");
	  }
	  A->g_symbol_table = '/';
	}

	A->g_symbol_code = pcpos->symbol_code;

	if (pcpos->c == ' ') {
	  ; /* ignore other two bytes */
	}
	else if (((pcpos->t - 33) & 0x18) == 0x10) {
	  A->g_altitude_ft = pow(1.002, (pcpos->c - 33) * 91 + pcpos->s - 33);
	}
	else if (pcpos->c == '{')
	{
	  A->g_range = 2.0 * pow(1.08, pcpos->s - 33);
	}
	else if (pcpos->c >= '!' && pcpos->c <= 'z')
	{
	  /* For a weather station, this is wind information. */
	  A->g_course = (pcpos->c - 33) * 4;
	  A->g_speed_mph = DW_KNOTS_TO_MPH(pow(1.08, pcpos->s - 33) - 1.0);
	}

}


/*------------------------------------------------------------------
 *
 * Function:	get_latitude_8
 *
 * Purpose:	Convert 8 byte latitude encoding to degrees.
 *
 * Inputs:	plat 	- Pointer to first byte.
 *
 * Returns:	Double precision value in degrees.  Negative for South.
 *
 * Description:	Latitude is expressed as a fixed 8-character field, in degrees 
 *		and decimal minutes (to two decimal places), followed by the 
 *		letter N for north or S for south.
 *		The protocol spec specifies upper case but I've seen lower
 *		case so this will accept either one.
 *		Latitude degrees are in the range 00 to 90. Latitude minutes 
 *		are expressed as whole minutes and hundredths of a minute, 
 *		separated by a decimal point.
 *		For example:
 *		4903.50N is 49 degrees 3 minutes 30 seconds north.
 *		In generic format examples, the latitude is shown as the 8-character 
 *		string ddmm.hhN (i.e. degrees, minutes and hundredths of a minute north).
 *
 * Bug:		We don't properly deal with position ambiguity where trailing
 *		digits might be replaced by spaces.  We simply treat them like zeros.	
 *
 * Errors:	Return G_UNKNOWN for any type of error.
 *
 *		Should probably print an error message.
 *
 *------------------------------------------------------------------*/

double get_latitude_8 (char *p, int quiet)
{
	struct lat_s {
	  unsigned char deg[2];
	  unsigned char minn[2];
	  char dot;
	  unsigned char hmin[2];
	  char ns;
	} *plat;

	double result = 0;
	
	plat = (void *)p;

	if (isdigit(plat->deg[0]))
	  result += ((plat->deg[0]) - '0') * 10;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in latitude.  Found '%c' when expecting 0-9 for tens of degrees.\n", plat->deg[0]);
	  }
	  return (G_UNKNOWN);
	}

	if (isdigit(plat->deg[1]))
	  result += ((plat->deg[1]) - '0') * 1;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in latitude.  Found '%c' when expecting 0-9 for degrees.\n", plat->deg[1]);
	  }
	  return (G_UNKNOWN);
	}

	if (plat->minn[0] >= '0' || plat->minn[0] <= '5')
	  result += ((plat->minn[0]) - '0') * (10. / 60.);
	else if (plat->minn[0] == ' ')
	  ;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in latitude.  Found '%c' when expecting 0-5 for tens of minutes.\n", plat->minn[0]);
	  }
	  return (G_UNKNOWN);
	}

	if (isdigit(plat->minn[1]))
	  result += ((plat->minn[1]) - '0') * (1. / 60.);
	else if (plat->minn[1] == ' ')
	  ;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in latitude.  Found '%c' when expecting 0-9 for minutes.\n", plat->minn[1]);
	  }
	  return (G_UNKNOWN);
	}

	if (plat->dot != '.') {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Unexpected character \"%c\" found where period expected in latitude.\n", plat->dot);
	  }
	  return (G_UNKNOWN);
	} 

	if (isdigit(plat->hmin[0]))
	  result += ((plat->hmin[0]) - '0') * (0.1 / 60.);
	else if (plat->hmin[0] == ' ')
	  ;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in latitude.  Found '%c' when expecting 0-9 for tenths of minutes.\n", plat->hmin[0]);
	  }
	  return (G_UNKNOWN);
	}

	if (isdigit(plat->hmin[1]))
	  result += ((plat->hmin[1]) - '0') * (0.01 / 60.);
	else if (plat->hmin[1] == ' ')
	  ;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in latitude.  Found '%c' when expecting 0-9 for hundredths of minutes.\n", plat->hmin[1]);
	  }
	  return (G_UNKNOWN);
	}

// The spec requires upper case for hemisphere.  Accept lower case but warn.

	if (plat->ns == 'N') {
	  return (result);
        }
        else if (plat->ns == 'n') {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Warning: Lower case n found for latitude hemisphere.  Specification requires upper case N or S.\n");
	  }	  
	  return (result);
	}
	else if (plat->ns == 'S') {
	  return ( - result);
	}
	else if (plat->ns == 's') {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Warning: Lower case s found for latitude hemisphere.  Specification requires upper case N or S.\n");	
	  }  
	  return ( - result);
	}
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Error: '%c' found for latitude hemisphere.  Specification requires upper case N or s.\n", plat->ns);	 
	  } 
	  return (G_UNKNOWN);	
	}	
}


/*------------------------------------------------------------------
 *
 * Function:	get_longitude_9
 *
 * Purpose:	Convert 9 byte longitude encoding to degrees.
 *
 * Inputs:	plat 	- Pointer to first byte.
 *
 * Returns:	Double precision value in degrees.  Negative for West.
 *
 * Description:	Longitude is expressed as a fixed 9-character field, in degrees and 
 *		decimal minutes (to two decimal places), followed by the letter E 
 *		for east or W for west.
 *		Longitude degrees are in the range 000 to 180. Longitude minutes are
 *		expressed as whole minutes and hundredths of a minute, separated by a
 *		decimal point.
 *		For example:
 *		07201.75W is 72 degrees 1 minute 45 seconds west.
 *		In generic format examples, the longitude is shown as the 9-character 
 *		string dddmm.hhW (i.e. degrees, minutes and hundredths of a minute west).
 *
 * Bug:		We don't properly deal with position ambiguity where trailing
 *		digits might be replaced by spaces.  We simply treat them like zeros.	
 *
 * Errors:	Return G_UNKNOWN for any type of error.
 *
 * Example:	
 *
 *------------------------------------------------------------------*/


double get_longitude_9 (char *p, int quiet)
{
	struct lat_s {
	  unsigned char deg[3];
	  unsigned char minn[2];
	  char dot;
	  unsigned char hmin[2];
	  char ew;
	} *plon;

	double result = 0;
	
	plon = (void *)p;

	if (plon->deg[0] == '0' || plon->deg[0] == '1')
	  result += ((plon->deg[0]) - '0') * 100;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in longitude.  Found '%c' when expecting 0 or 1 for hundreds of degrees.\n", plon->deg[0]);
	  }
	  return (G_UNKNOWN);
	}

	if (isdigit(plon->deg[1]))
	  result += ((plon->deg[1]) - '0') * 10;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in longitude.  Found '%c' when expecting 0-9 for tens of degrees.\n", plon->deg[1]);
	  }
	  return (G_UNKNOWN);
	}

	if (isdigit(plon->deg[2]))
	  result += ((plon->deg[2]) - '0') * 1;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in longitude.  Found '%c' when expecting 0-9 for degrees.\n", plon->deg[2]);
	  }
	  return (G_UNKNOWN);
	}

	if (plon->minn[0] >= '0' || plon->minn[0] <= '5')
	  result += ((plon->minn[0]) - '0') * (10. / 60.);
	else if (plon->minn[0] == ' ')
	  ;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in longitude.  Found '%c' when expecting 0-5 for tens of minutes.\n", plon->minn[0]);
	  }
	  return (G_UNKNOWN);
	}

	if (isdigit(plon->minn[1]))
	  result += ((plon->minn[1]) - '0') * (1. / 60.);
	else if (plon->minn[1] == ' ')
	  ;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in longitude.  Found '%c' when expecting 0-9 for minutes.\n", plon->minn[1]);
	  }
	  return (G_UNKNOWN);
	}

	if (plon->dot != '.') {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Unexpected character \"%c\" found where period expected in longitude.\n", plon->dot);
	  }
	  return (G_UNKNOWN);
	} 

	if (isdigit(plon->hmin[0]))
	  result += ((plon->hmin[0]) - '0') * (0.1 / 60.);
	else if (plon->hmin[0] == ' ')
	  ;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in longitude.  Found '%c' when expecting 0-9 for tenths of minutes.\n", plon->hmin[0]);
	  }
	  return (G_UNKNOWN);
	}

	if (isdigit(plon->hmin[1]))
	  result += ((plon->hmin[1]) - '0') * (0.01 / 60.);
	else if (plon->hmin[1] == ' ')
	  ;
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in longitude.  Found '%c' when expecting 0-9 for hundredths of minutes.\n", plon->hmin[1]);
	  }
	  return (G_UNKNOWN);
	}

// The spec requires upper case for hemisphere.  Accept lower case but warn.

	if (plon->ew == 'E') {
	  return (result);
        }
        else if (plon->ew == 'e') {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Warning: Lower case e found for longitude hemisphere.  Specification requires upper case E or W.\n");
	  }	  
	  return (result);
	}
	else if (plon->ew == 'W') {
	  return ( - result);
	}
	else if (plon->ew == 'w') {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Warning: Lower case w found for longitude hemisphere.  Specification requires upper case E or W.\n");
	  }	  
	  return ( - result);
	}
	else {
	  if ( ! quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Error: '%c' found for longitude hemisphere.  Specification requires upper case E or W.\n", plon->ew);	
	  }  
	  return (G_UNKNOWN);	
	}		
}


/*------------------------------------------------------------------
 *
 * Function:	get_timestamp
 *
 * Purpose:	Convert 7 byte timestamp to unix time value.
 *
 * Inputs:	p 	- Pointer to first byte.
 *
 * Returns:	time_t data type. (UTC)
 *
 * Description:	
 *
 *		Day/Hours/Minutes (DHM) format is a fixed 7-character field, consisting of
 *		a 6-digit day/time group followed by a single time indicator character (z or
 *		/). The day/time group consists of a two-digit day-of-the-month (01-31) and
 *		a four-digit time in hours and minutes.
 *		Times can be expressed in zulu (UTC/GMT) or local time. For example:
 *
 *		  092345z is 2345 hours zulu time on the 9th day of the month.
 *		  092345/ is 2345 hours local time on the 9th day of the month.
 *
 *		It is recommended that future APRS implementations only transmit zulu
 *		format on the air.
 *
 *		Note: The time in Status Reports may only be in zulu format.
 *
 *		Hours/Minutes/Seconds (HMS) format is a fixed 7-character field,
 *		consisting of a 6-digit time in hours, minutes and seconds, followed by the h
 *		time-indicator character. For example:
 *
 *		  234517h is 23 hours 45 minutes and 17 seconds zulu.
 *
 *		Note: This format may not be used in Status Reports.
 *
 *		Month/Day/Hours/Minutes (MDHM) format is a fixed 8-character field,
 *		consisting of the month (01-12) and day-of-the-month (01-31), followed by
 *		the time in hours and minutes zulu. For example:
 *
 *		  10092345 is 23 hours 45 minutes zulu on October 9th.
 *
 *		This format is only used in reports from stand-alone "positionless" weather
 *		stations (i.e. reports that do not contain station position information).
 *
 *
 * Bugs:	Local time not implemented yet.
 *		8 character form not implemented yet.
 *
 *		Boundary conditions are not handled properly.
 *		For example, suppose it is 00:00:03 on January 1.
 *		We receive a timestamp of 23:59:58 (which was December 31).
 *		If we simply replace the time, and leave the current date alone,
 *		the result is about a day into the future.
 *
 *
 * Example:	
 *
 *------------------------------------------------------------------*/


time_t get_timestamp (decode_aprs_t *A, char *p)
{
	struct dhm_s {
	  char day[2];
	  char hours[2];
	  char minutes[2];
	  char tic;		/* Time indicator character. */
				/* z = UTC. */
				/* / = local - not implemented yet. */
	} *pdhm;

	struct hms_s {
	  char hours[2];
	  char minutes[2];
	  char seconds[2];
	  char tic;		/* Time indicator character. */
				/* h = UTC. */
	} *phms;

	struct tm *ptm;

	time_t ts;

	ts = time(NULL);
	ptm = gmtime(&ts);

	pdhm = (void *)p;
	phms = (void *)p;

	if (pdhm->tic == 'z' || pdhm->tic == '/')   /* Wrong! */
	{
	  int j;

	  j = (pdhm->day[0] - '0') * 10 + pdhm->day[1] - '0';
	  //text_color_set(DW_COLOR_DECODED);
	  //dw_printf("Changing day from %d to %d\n", ptm->tm_mday, j);
	  ptm->tm_mday = j;

	  j = (pdhm->hours[0] - '0') * 10 + pdhm->hours[1] - '0';
	  //dw_printf("Changing hours from %d to %d\n", ptm->tm_hour, j);
	  ptm->tm_hour = j;

	  j = (pdhm->minutes[0] - '0') * 10 + pdhm->minutes[1] - '0';
	  //dw_printf("Changing minutes from %d to %d\n", ptm->tm_min, j);
	  ptm->tm_min = j;

	} 
	else if (phms->tic == 'h') 
	{
	  int j;

	  j = (phms->hours[0] - '0') * 10 + phms->hours[1] - '0';
	  //text_color_set(DW_COLOR_DECODED);
	  //dw_printf("Changing hours from %d to %d\n", ptm->tm_hour, j);
	  ptm->tm_hour = j;

	  j = (phms->minutes[0] - '0') * 10 + phms->minutes[1] - '0';
	  //dw_printf("Changing minutes from %d to %d\n", ptm->tm_min, j);
	  ptm->tm_min = j;

	  j = (phms->seconds[0] - '0') * 10 + phms->seconds[1] - '0';
	  //dw_printf("%sChanging seconds from %d to %d\n", ptm->tm_sec, j);
	  ptm->tm_sec = j;
	} 
	
	return (mktime(ptm));
}




/*------------------------------------------------------------------
 *
 * Function:	get_maidenhead
 *
 * Purpose:	See if we have a maidenhead locator.
 *
 * Inputs:	p 	- Pointer to first byte.
 *
 * Returns:	0 = not found.
 *		4 = possible 4 character locator found.
 *		6 = possible 6 character locator found.
 *
 *		It is not stored anywhere or processed.
 *
 * Description:	
 *
 *		The maidenhead locator system is sometimes used as a more compact, 
 *		and less precise, alternative to numeric latitude and longitude.
 *
 *		It is composed of:
 *			a pair of letters in range A to R.
 *			a pair of digits in range of 0 to 9.
 *			a pair of letters in range of A to X.
 *
 * 		The APRS spec says that all letters must be transmitted in upper case.
 *
 *
 * Examples from APRS spec:	
 *
 *		IO91SX
 *		IO91
 *
 *
 *------------------------------------------------------------------*/


int get_maidenhead (decode_aprs_t *A, char *p)
{

	if (toupper(p[0]) >= 'A' && toupper(p[0]) <= 'R' &&
	    toupper(p[1]) >= 'A' && toupper(p[1]) <= 'R' &&
	    isdigit(p[2]) && isdigit(p[3])) {

	  /* We have 4 characters matching the rule. */

	  if (islower(p[0]) || islower(p[1])) {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Warning: Lower case letter in Maidenhead locator.  Specification requires upper case.\n");
	    }	  
	  }

	  if (toupper(p[4]) >= 'A' && toupper(p[4]) <= 'X' &&
	      toupper(p[5]) >= 'A' && toupper(p[5]) <= 'X') {

	    /* We have 6 characters matching the rule. */

	    if (islower(p[4]) || islower(p[5])) {
	      if ( ! A->g_quiet) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf("Warning: Lower case letter in Maidenhead locator.  Specification requires upper case.\n");	
	      }	  
	    }
	  
	    return 6;
	  }
	
	  return 4;
	}

	return 0;
}



/*------------------------------------------------------------------
 *
 * Function:	data_extension_comment
 *
 * Purpose:	A fixed length 7-byte field may follow APRS position datA->
 *
 * Inputs:	pdext	- Pointer to optional data extension and comment.
 *
 * Returns:	true if a data extension was found.
 *
 * Outputs:	One or more of the following, depending the data found:
 *	
 *			A->g_course
 *			A->g_speed_mph
 *			A->g_power 
 *			A->g_height 
 *			A->g_gain 
 *			A->g_directivity 
 *			A->g_range
 *
 *		Anything left over will be put in 
 *
 *			A->g_comment			
 *
 * Description:	
 *
 *
 *
 *------------------------------------------------------------------*/

const char *dir[9] = { "omni", "NE", "E", "SE", "S", "SW", "W", "NW", "N" };

static int data_extension_comment (decode_aprs_t *A, char *pdext)
{
	int n;

	if (strlen(pdext) < 7) {
	  strlcpy (A->g_comment, pdext, sizeof(A->g_comment));
	  return 0;
	}

/* Tyy/Cxx - Area object descriptor. */

	if (pdext[0] == 'T' &&
		pdext[3] == '/' &&
	 	pdext[4] == 'C')
	{
	  /* not decoded at this time */
	  process_comment (A, pdext+7, -1);
	  return 1;
	}

/* CSE/SPD */
/* For a weather station (symbol code _) this is wind. */
/* For others, it would be course and speed. */

	if (pdext[3] == '/')
	{
	  if (sscanf (pdext, "%3d", &n))
	  {
	    A->g_course = n;
	  }
	  if (sscanf (pdext+4, "%3d", &n))
	  {
	    A->g_speed_mph = DW_KNOTS_TO_MPH(n);
	  }

	  /* Bearing and Number/Range/Quality? */

	  if (pdext[7] == '/' && pdext[11] == '/') 
	  {
	    process_comment (A, pdext + 7 + 8, -1);
	  }
	  else {
	    process_comment (A, pdext+7, -1);
	  }
	  return 1;
	}

/* check for Station power, height, gain. */

	if (strncmp(pdext, "PHG", 3) == 0)
	{
	  A->g_power = (pdext[3] - '0') * (pdext[3] - '0');
	  A->g_height = (1 << (pdext[4] - '0')) * 10;
	  A->g_gain = pdext[5] - '0';
	  if (pdext[6] >= '0' && pdext[6] <= '8') {
	    strlcpy (A->g_directivity, dir[pdext[6]-'0'], sizeof(A->g_directivity));
	  }

	  process_comment (A, pdext+7, -1);
	  return 1;
	}

/* check for precalculated radio range. */

	if (strncmp(pdext, "RNG", 3) == 0)
	{
	  if (sscanf (pdext+3, "%4d", &n))
	  {
	    A->g_range = n;
	  }
	  process_comment (A, pdext+7, -1);
	  return 1;
	}

/* DF signal strength,  */

	if (strncmp(pdext, "DFS", 3) == 0)
	{
	  //A->g_strength = pdext[3] - '0';
	  A->g_height = (1 << (pdext[4] - '0')) * 10;
	  A->g_gain = pdext[5] - '0';
	  if (pdext[6] >= '0' && pdext[6] <= '8') {
	    strlcpy (A->g_directivity, dir[pdext[6]-'0'], sizeof(A->g_directivity));
	  }

	  process_comment (A, pdext+7, -1);
	  return 1;
	}

	process_comment (A, pdext, -1);
	return 0;
}


/*------------------------------------------------------------------
 *
 * Function:	decode_tocall
 *
 * Purpose:	Extract application from the destination.
 *
 * Inputs:	dest	- Destination address.
 *			Don't care if SSID is present or not.
 *
 * Outputs:	A->g_mfr
 *
 * Description:	For maximum flexibility, we will read the
 *		data file at run time rather than compiling it in.
 *
 *		For the most recent version, download from:
 *
 *		http://www.aprs.org/aprs11/tocalls.txt
 *
 *		Windows version:  File must be in current working directory.
 *
 *		Linux version: Search order is current working directory
 *			then /usr/share/direwolf directory.
 *
 *------------------------------------------------------------------*/

#define MAX_TOCALLS 150

static struct tocalls_s {
	unsigned char len;
	char prefix[7];
	char *description;
} tocalls[MAX_TOCALLS];

static int num_tocalls = 0;

// Make sure the array is null terminated.
static const char *search_locations[] = {
	(const char *) "tocalls.txt",
#ifndef __WIN32__
	(const char *) "/usr/share/direwolf/tocalls.txt",
	(const char *) "/usr/local/share/direwolf/tocalls.txt",
#endif
	(const char *) NULL
};

static int tocall_cmp (const void *px, const void *py)
{
	const struct tocalls_s *x = (struct tocalls_s *)px;
	const struct tocalls_s *y = (struct tocalls_s *)py;

	if (x->len != y->len) return (y->len - x->len);
	return (strcmp(x->prefix, y->prefix));
}

static void decode_tocall (decode_aprs_t *A, char *dest)
{
	FILE *fp = 0;
	int n = 0;
	static int first_time = 1;
	char stuff[100];
	char *p = NULL;
	char *r = NULL;

	//dw_printf("debug: decode_tocall(\"%s\")\n", dest);

/*
 * Extract the calls and descriptions from the file.
 *
 * Use only lines with exactly these formats:
 *
 *       APN          Network nodes, digis, etc
 *	      APWWxx  APRSISCE win32 version
 *	|     |       |
 *	00000000001111111111      	
 *	01234567890123456789...
 *
 * Matching will be with only leading upper case and digits.
 */

// TODO:  Look for this in multiple locations.
// For example, if application was installed in /usr/local/bin,
// we might want to put this in /usr/local/share/aprs

// If search strategy changes, be sure to keep symbols_init in sync.

	if (first_time) {

	  n = 0;
	  fp = NULL;
	  do {
	    if(search_locations[n] == NULL) break;
	    fp = fopen(search_locations[n++], "r");
	  } while (fp == NULL);

	  if (fp != NULL) {

	    while (fgets(stuff, sizeof(stuff), fp) != NULL && num_tocalls < MAX_TOCALLS) {
	      
	      p = stuff + strlen(stuff) - 1;
	      while (p >= stuff && (*p == '\r' || *p == '\n')) {
	        *p-- = '\0';
	      }

	      // dw_printf("debug: %s\n", stuff);

	      if (stuff[0] == ' ' && 
		  stuff[4] == ' ' &&
		  stuff[5] == ' ' &&
		  stuff[6] == 'A' && 
		  stuff[7] == 'P' && 
		  stuff[12] == ' ' &&
		  stuff[13] == ' ' ) {

	        p = stuff + 6;
	        r = tocalls[num_tocalls].prefix;
	        while (isupper((int)(*p)) || isdigit((int)(*p))) {
	          *r++ = *p++;
	        }
	        *r = '\0';
	        if (strlen(tocalls[num_tocalls].prefix) > 2) {
	          tocalls[num_tocalls].description = strdup(stuff+14);
		  tocalls[num_tocalls].len = strlen(tocalls[num_tocalls].prefix);
	          // dw_printf("debug: %d '%s' -> '%s'\n", tocalls[num_tocalls].len, tocalls[num_tocalls].prefix, tocalls[num_tocalls].description);

	          num_tocalls++;
	        }
	      }
	      else if (stuff[0] == ' ' && 
		  stuff[1] == 'A' && 
		  stuff[2] == 'P' && 
		  isupper((int)(stuff[3])) &&
		  stuff[4] == ' ' &&
		  stuff[5] == ' ' &&
		  stuff[6] == ' ' &&
		  stuff[12] == ' ' &&
		  stuff[13] == ' ' ) {

	        p = stuff + 1;
	        r = tocalls[num_tocalls].prefix;
	        while (isupper((int)(*p)) || isdigit((int)(*p))) {
	          *r++ = *p++;
	        }
	        *r = '\0';
	        if (strlen(tocalls[num_tocalls].prefix) > 2) {
	          tocalls[num_tocalls].description = strdup(stuff+14);
		  tocalls[num_tocalls].len = strlen(tocalls[num_tocalls].prefix);
	          // dw_printf("debug: %d '%s' -> '%s'\n", tocalls[num_tocalls].len, tocalls[num_tocalls].prefix, tocalls[num_tocalls].description);

	          num_tocalls++;
	        }
	      }
	    }
	    fclose(fp);

/*
 * Sort by decreasing length so the search will go
 * from most specific to least specific.
 * Example:  APY350 or APY008 would match those specific
 * models before getting to the more generic APY.
 */

#if defined(__WIN32__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__APPLE__)
	    qsort (tocalls, num_tocalls, sizeof(struct tocalls_s), tocall_cmp);
#else
	    qsort (tocalls, num_tocalls, sizeof(struct tocalls_s), (__compar_fn_t)tocall_cmp);
#endif
	  }
	  else {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Warning: Could not open 'tocalls.txt'.\n");
	      dw_printf("System types in the destination field will not be decoded.\n");
	    }
	  }

	
	  first_time = 0;
	}


	for (n=0; n<num_tocalls; n++) {
	  if (strncmp(dest, tocalls[n].prefix, tocalls[n].len) == 0) {
	    strlcpy (A->g_mfr, tocalls[n].description, sizeof(A->g_mfr));
	    return;
	  }
	}

} /* end decode_tocall */ 



/*------------------------------------------------------------------
 *
 * Function:	substr_se
 *
 * Purpose:	Extract substring given start and end+1 offset.
 *
 * Inputs:	src		- Source string
 *
 *		start		- Start offset.
 *		
 *		endp1		- End offset+1 for ease of use with regexec result.
 *
 * Outputs:	dest		- Destination for substring.
 *
 *------------------------------------------------------------------*/

// TODO: potential for buffer overflow here.

static void substr_se (char *dest, const char *src, int start, int endp1)
{
	int len = endp1 - start;

	if (start < 0 || endp1 < 0 || len <= 0) {
	  dest[0] = '\0';
	  return;
	}
	memcpy (dest, src + start, len);
	dest[len] = '\0';

} /* end substr_se */
	


/*------------------------------------------------------------------
 *
 * Function:	process_comment
 *
 * Purpose:	Extract optional items from the comment.
 *
 * Inputs:	pstart		- Pointer to start of left over information field.
 *
 *		clen		- Length of comment or -1 to take it all.
 *
 * Outputs:	A->g_telemetry	- Base 91 telemetry |ss1122|
 *		A->g_altitude_ft	- from /A=123456
 *		A->g_lat	- Might be adjusted from !DAO!
 *		A->g_lon	- Might be adjusted from !DAO!
 *		A->g_aprstt_loc	- Private extension to !DAO!
 *		A->g_freq
 *		A->g_tone
 *		A->g_offset
 *		A->g_comment	- Anything left over after extracting above.
 *
 * Description:	After processing fixed and possible optional parts
 *		of the message, everything left over is a comment.
 *
 *		Except!!!
 *
 *		There are could be some other pieces of data, with 
 *		particular formats, buried in there.
 *		Pull out those special items and put everything 
 *		else into A->g_comment.
 *
 * References:	http://www.aprs.org/info/freqspec.txt
 *
 *			999.999MHz T100 +060	Voice frequency.
 *		
 *		http://www.aprs.org/datum.txt
 *
 *			!DAO!			APRS precision and Datum option.
 *
 *		Protocol reference, end of chaper 6.
 *
 *			/A=123456		Altitude
 *
 *
 *------------------------------------------------------------------*/

/* CTCSS tones in various formats to avoid conversions every time. */

#define NUM_CTCSS 50

static const int i_ctcss[NUM_CTCSS] = {
         67,  69,  71,  74,  77,  79,  82,  85,  88,  91,
         94,  97, 100, 103, 107, 110, 114, 118, 123, 127,
        131, 136, 141, 146, 151, 156, 159, 162, 165, 167,
        171, 173, 177, 179, 183, 186, 189, 192, 196, 199,
        203, 206, 210, 218, 225, 229, 233, 241, 250, 254 };

static const float f_ctcss[NUM_CTCSS] = {
         67.0,  69.3,  71.9,  74.4,  77.0,  79.7,  82.5,  85.4,  88.5,  91.5,
         94.8,  97.4, 100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3,
        131.8, 136.5, 141.3, 146.2, 151.4, 156.7, 159.8, 162.2, 165.5, 167.9,
        171.3, 173.8, 177.3, 179.9, 183.5, 186.2, 189.9, 192.8, 196.6, 199.5,
        203.5, 206.5, 210.7, 218.1, 225.7, 229.1, 233.6, 241.8, 250.3, 254.1 };

static const char * s_ctcss[NUM_CTCSS] = {
         "67.0",  "69.3",  "71.9",  "74.4",  "77.0",  "79.7",  "82.5",  "85.4",  "88.5",  "91.5",
         "94.8",  "97.4", "100.0", "103.5", "107.2", "110.9", "114.8", "118.8", "123.0", "127.3",
        "131.8", "136.5", "141.3", "146.2", "151.4", "156.7", "159.8", "162.2", "165.5", "167.9",
        "171.3", "173.8", "177.3", "179.9", "183.5", "186.2", "189.9", "192.8", "196.6", "199.5",
        "203.5", "206.5", "210.7", "218.1", "225.7", "229.1", "233.6", "241.8", "250.3", "254.1" };


#define sign(x) (((x)>=0)?1:(-1))

static void process_comment (decode_aprs_t *A, char *pstart, int clen)
{
	static int first_time = 1;
	static regex_t std_freq_re;	/* Frequency in standard format. */
	static regex_t std_tone_re;	/* Tone in standard format. */
	static regex_t std_toff_re;	/* Explicitly no tone. */
	static regex_t std_dcs_re;	/* Digital codes squelch in standard format. */
	static regex_t std_offset_re;	/* Xmit freq offset in standard format. */
	static regex_t std_range_re;	/* Range in standard format. */

	static regex_t dao_re;		/* DAO */
	static regex_t alt_re;		/* /A= altitude */

	static regex_t bad_freq_re;	/* Likely frequency, not standard format */
	static regex_t bad_tone_re;	/* Likely tone, not standard format */

	static regex_t base91_tel_re;	/* Base 91 compressed telemetry data. */

	int e;
	char emsg[100];
#define MAXMATCH 4
	regmatch_t match[MAXMATCH];
	char temp[sizeof(A->g_comment)];
	int keep_going;


/*
 * No sense in recompiling the patterns and freeing every time.
 */	
	if (first_time) 
	{
/*
 * Frequency must be at the at the beginning.
 * Others can be anywhere in the comment.
 */
		
	  //e = regcomp (&freq_re, "^[0-9A-O][0-9][0-9]\\.[0-9][0-9][0-9 ]MHz( [TCDtcd][0-9][0-9][0-9]| Toff)?( [+-][0-9][0-9][0-9])?", REG_EXTENDED);

	  // Freq optionally preceded by space or /.
	  // Third fractional digit can be space instead.
	  // "MHz" should be exactly that capitalization.  
	  // Print warning later it not.

	  e = regcomp (&std_freq_re, "^[/ ]?([0-9A-O][0-9][0-9]\\.[0-9][0-9][0-9 ])([Mm][Hh][Zz])", REG_EXTENDED);
	  if (e) {
	    regerror (e, &std_freq_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  // If no tone, we might gobble up / after any data extension,
	  // We could also have a space but it's not required.
	  // I don't understand the difference between T and C so treat the same for now.
	  // We can also have "off" instead of number to explicitly mean none.

	  e = regcomp (&std_tone_re, "^[/ ]?([TtCc][012][0-9][0-9])", REG_EXTENDED);
	  if (e) {
	    regerror (e, &std_tone_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&std_toff_re, "^[/ ]?[TtCc][Oo][Ff][Ff]", REG_EXTENDED);
	  if (e) {
	    regerror (e, &std_toff_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&std_dcs_re, "^[/ ]?[Dd]([0-7][0-7][0-7])", REG_EXTENDED);
	  if (e) {
	    regerror (e, &std_dcs_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }
	  e = regcomp (&std_offset_re, "^[/ ]?([+-][0-9][0-9][0-9])", REG_EXTENDED);
	  if (e) {
	    regerror (e, &std_offset_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&std_range_re, "^[/ ]?[Rr]([0-9][0-9])([mk])", REG_EXTENDED);
	  if (e) {
	    regerror (e, &std_range_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&dao_re, "!([A-Z][0-9 ][0-9 ]|[a-z][!-{ ][!-{ ]|T[0-9 B][0-9 ])!", REG_EXTENDED);
	  if (e) {
	    regerror (e, &dao_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&alt_re, "/A=[0-9][0-9][0-9][0-9][0-9][0-9]", REG_EXTENDED);
	  if (e) {
	    regerror (e, &alt_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&bad_freq_re, "[0-9][0-9][0-9]\\.[0-9][0-9][0-9]?", REG_EXTENDED);
	  if (e) {
	    regerror (e, &bad_freq_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&bad_tone_re, "(^|[^0-9.])([6789][0-9]\\.[0-9]|[12][0-9][0-9]\\.[0-9]|67|77|100|123)($|[^0-9.])", REG_EXTENDED);
	  if (e) {
	    regerror (e, &bad_tone_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

// TODO:  Would like to restrict to even length something like this:  ([!-{][!-{]){2,7}

	  e = regcomp (&base91_tel_re, "\\|([!-{]{4,14})\\|", REG_EXTENDED);
	  if (e) {
	    regerror (e, &base91_tel_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  first_time = 0;
	}

/*
 * If clen is >= 0, take only specified number of characters.
 * Otherwise, take it all.
 */
	if (clen < 0) {
	  clen = strlen(pstart);
	}

/*
 * Watch out for buffer overflow.
 * KG6AZZ reports that there is a local digipeater that seems to 
 * malfunction ocassionally.  It corrupts the packet, as it is
 * digipeated, causing the comment to be hundreds of characters long.
 */

	if (clen > sizeof(A->g_comment) - 1) {
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Comment is extremely long, %d characters.\n", clen);
	    dw_printf("Please report this, along with surrounding lines, so we can find the cause.\n");
	  }
	  clen = sizeof(A->g_comment) - 1;
	}

	if (clen > 0) {
	  memcpy (A->g_comment, pstart, (size_t)clen);
	  A->g_comment[clen] = '\0';
	}
	else {
	  A->g_comment[0] = '\0';
	}


/*
 * Look for frequency in the standard format at start of comment.
 * If that fails, try to obtain from object name.
 */

	if (regexec (&std_freq_re, A->g_comment, MAXMATCH, match, 0) == 0) 
	{
	  char sftemp[30];
	  char smtemp[10];

          //dw_printf("matches= %d - %d, %d - %d, %d - %d\n", (int)(match[0].rm_so), (int)(match[0].rm_eo), 
	  //						    (int)(match[1].rm_so), (int)(match[1].rm_eo),
	  //						    (int)(match[2].rm_so), (int)(match[2].rm_eo) );

	  substr_se (sftemp, A->g_comment, match[1].rm_so, match[1].rm_eo);
	  substr_se (smtemp, A->g_comment, match[2].rm_so, match[2].rm_eo);
	
	  switch (sftemp[0]) {
	    case 'A': A->g_freq =  1200 + atof(sftemp+1); break;
	    case 'B': A->g_freq =  2300 + atof(sftemp+1); break;
	    case 'C': A->g_freq =  2400 + atof(sftemp+1); break;
	    case 'D': A->g_freq =  3400 + atof(sftemp+1); break;
	    case 'E': A->g_freq =  5600 + atof(sftemp+1); break;
	    case 'F': A->g_freq =  5700 + atof(sftemp+1); break;
	    case 'G': A->g_freq =  5800 + atof(sftemp+1); break;
	    case 'H': A->g_freq = 10100 + atof(sftemp+1); break;
	    case 'I': A->g_freq = 10200 + atof(sftemp+1); break;
	    case 'J': A->g_freq = 10300 + atof(sftemp+1); break;
	    case 'K': A->g_freq = 10400 + atof(sftemp+1); break;
	    case 'L': A->g_freq = 10500 + atof(sftemp+1); break;
	    case 'M': A->g_freq = 24000 + atof(sftemp+1); break;
	    case 'N': A->g_freq = 24100 + atof(sftemp+1); break;
	    case 'O': A->g_freq = 24200 + atof(sftemp+1); break;
	    default:  A->g_freq =         atof(sftemp);   break;
	  }

	  if (strncmp(smtemp, "MHz", 3) != 0) {
	    if ( ! A->g_quiet) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Warning: \"%s\" has non-standard capitalization and might not be recognized by some systems.\n", smtemp);
	      dw_printf("For best compatibility, it should be exactly like this: \"MHz\"  (upper,upper,lower case)\n");
	    }
	  }

	  strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));
	  strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment));
	}
	else if (strlen(A->g_name) > 0) {

	  // Try to extract sensible number from object/item name.

	  double x = atof (A->g_name);

	  if ((x >= 144 && x <= 148) ||
	      (x >= 222 && x <= 225) ||
	      (x >= 420 && x <= 450) ||
	      (x >= 902 && x <= 928)) { 
	    A->g_freq = x;
	  }
	}

/*
 * Next, look for tone, DCS code, and range.
 * Examples always have them in same order but it's not clear
 * whether any order is allowed after possible frequency.
 *
 * TODO: Convert integer tone to original value for display.
 * TODO: samples in zfreq-test3.txt
 */

	keep_going = 1;
	while (keep_going) {

	  if (regexec (&std_tone_re, A->g_comment, MAXMATCH, match, 0) == 0) {

	    char sttemp[10];	/* includes leading letter */
	    int f;
	    int i;

	    substr_se (sttemp, A->g_comment, match[1].rm_so, match[1].rm_eo);

	    // Try to convert from integer to proper value.

	    f = atoi(sttemp+1);
	    for (i = 0; i < NUM_CTCSS; i++) {
	      if (f == i_ctcss[i]) {
	        A->g_tone = f_ctcss[i];
	        break;
	      }
	    }
	    if (A->g_tone == G_UNKNOWN) {
	      if ( ! A->g_quiet) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf("Bad CTCSS/PL specification: \"%s\"\n", sttemp);
	        dw_printf("Integer does not correspond to standard tone.\n");
	      }
	    }

	    strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));
	    strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment));
	  }
	  else if (regexec (&std_toff_re, A->g_comment, MAXMATCH, match, 0) == 0) {

	    dw_printf ("NO tone\n");
	    A->g_tone = 0;

	    strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));
	    strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment));
	  }
	  else if (regexec (&std_dcs_re, A->g_comment, MAXMATCH, match, 0) == 0) {

	    char sttemp[10];	/* three octal digits */

	    substr_se (sttemp, A->g_comment, match[1].rm_so, match[1].rm_eo);

	    A->g_dcs = strtoul (sttemp, NULL, 8);

	    strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));
	    strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment)-match[0].rm_so);
	  }
	  else if (regexec (&std_offset_re, A->g_comment, MAXMATCH, match, 0) == 0) {

	    char sttemp[10];	/* includes leading sign */

	    substr_se (sttemp, A->g_comment, match[1].rm_so, match[1].rm_eo);

	    A->g_offset = 10 * atoi(sttemp);

	    strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));
	    strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment)-match[0].rm_so);
	  }
	  else if (regexec (&std_range_re, A->g_comment, MAXMATCH, match, 0) == 0) {

	    char sttemp[10];	/* should be two digits */
	    char sutemp[10];	/* m for miles or k for km */

	    substr_se (sttemp, A->g_comment, match[1].rm_so, match[1].rm_eo);
	    substr_se (sutemp, A->g_comment, match[2].rm_so, match[2].rm_eo);

	    if (strcmp(sutemp, "m") == 0) {
	      A->g_range = atoi(sttemp);
	    }
	    else {
	      A->g_range = DW_KM_TO_MILES(atoi(sttemp));
	    }

	    strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));
	    strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment)-match[0].rm_so);
	  }
	  else {
	    keep_going = 0;
	  }
	}

/*
 * Telemetry data, in base 91 compressed format appears as 2 to 7 pairs
 * of base 91 digits, surrounded by | at start and end.
 */


	if (regexec (&base91_tel_re, A->g_comment, MAXMATCH, match, 0) == 0) 
	{

	  char tdata[30];	/* Should be 4 to 14 characters. */

          //dw_printf("compressed telemetry start=%d, end=%d\n", (int)(match[0].rm_so), (int)(match[0].rm_eo));

	  substr_se (tdata, A->g_comment, match[1].rm_so, match[1].rm_eo);

          //dw_printf("compressed telemetry data = \"%s\"\n", tdata);

	  telemetry_data_base91 (A->g_src, tdata, A->g_telemetry, sizeof(A->g_telemetry));

	  strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));
	  strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment)-match[0].rm_so);
	}


/*
 * Latitude and Longitude in the form DD MM.HH has a resolution of about 60 feet.
 * The !DAO! option allows another digit or almost two for greater resolution.
 *
 * This would not make sense to use this with a compressed location which
 * already has much greater resolution.
 *
 * It surprized me to see this in a MIC-E message.
 * MIC-E has resolution of .01 minute so it would make sense to have it as an option.
 */

	if (regexec (&dao_re, A->g_comment, MAXMATCH, match, 0) == 0) 
	{

	  int d = A->g_comment[match[0].rm_so+1];
	  int a = A->g_comment[match[0].rm_so+2];
	  int o = A->g_comment[match[0].rm_so+3];

          //dw_printf("start=%d, end=%d\n", (int)(match[0].rm_so), (int)(match[0].rm_eo));


/*
 * Private extension for APRStt
 */

	  if (d == 'T') {

	    if (a == ' ' && o == ' ') {
	      snprintf (A->g_aprstt_loc, sizeof(A->g_aprstt_loc), "APRStt corral location");
	    }
	    else if (isdigit(a) && o == ' ') {
	      snprintf (A->g_aprstt_loc, sizeof(A->g_aprstt_loc), "APRStt location %c of 10", a);
	    }
	    else if (isdigit(a) && isdigit(o)) {
	      snprintf (A->g_aprstt_loc, sizeof(A->g_aprstt_loc), "APRStt location %c%c of 100", a, o);
	    }
	    else if (a == 'B' && isdigit(o)) {
	      snprintf (A->g_aprstt_loc, sizeof(A->g_aprstt_loc), "APRStt location %c%c...", a, o);
	    }

	  }
	  else if (isupper(d)) 
	  {
/*
 * This adds one extra digit to each.  Dao adds extra digit like:
 *
 *		Lat:	 DD MM.HHa
 *		Lon:	DDD HH.HHo
 */
 	    if (isdigit(a)) {
	      A->g_lat += (a - '0') / 60000.0 * sign(A->g_lat);
	    }
 	    if (isdigit(o)) {
	      A->g_lon += (o - '0') / 60000.0 * sign(A->g_lon);
	    }
	  }
	  else if (islower(d)) 
	  {
/*
 * This adds almost two extra digits to each like this:
 *
 *		Lat:	 DD MM.HHxx
 *		Lon:	DDD HH.HHxx
 *
 * The original character range '!' to '{' is first converted
 * to an integer in range of 0 to 90.  It is multiplied by 1.1
 * to stretch the numeric range to be 0 to 99.
 */

/* 
 * The spec appears to be wrong.  It says '}' is the maximum value when it should be '{'. 
 */


 	    if (isdigit91(a)) {
	      A->g_lat += (a - B91_MIN) * 1.1 / 600000.0 * sign(A->g_lat);
	    }
 	    if (isdigit91(o)) {
	      A->g_lon += (o - B91_MIN) * 1.1 / 600000.0 * sign(A->g_lon);
	    }
	  }

	  strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));
	  strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment)-match[0].rm_so);
	}

/*
 * Altitude in feet.  /A=123456
 */

	if (regexec (&alt_re, A->g_comment, MAXMATCH, match, 0) == 0) 
	{

          //dw_printf("start=%d, end=%d\n", (int)(match[0].rm_so), (int)(match[0].rm_eo));

	  strlcpy (temp, A->g_comment + match[0].rm_eo, sizeof(temp));

	  A->g_comment[match[0].rm_eo] = '\0';
          A->g_altitude_ft = atoi(A->g_comment + match[0].rm_so + 3);

	  strlcpy (A->g_comment + match[0].rm_so, temp, sizeof(A->g_comment)-match[0].rm_so);
	}

	//dw_printf("Final comment='%s'\n", A->g_comment);

/*
 * Finally look for something that looks like frequency or CTCSS tone
 * in the remaining comment.  Point this out and suggest the 
 * standardized format.
 * Don't complain if we have already found a valid value.
 */
	if (A->g_freq == G_UNKNOWN && regexec (&bad_freq_re, A->g_comment, MAXMATCH, match, 0) == 0) 
	{
	  char bad[30];
	  char good[30];
	  double x;

	  substr_se (bad, A->g_comment, match[0].rm_so, match[0].rm_eo);
	  x = atof(bad);

	  if ((x >= 144 && x <= 148) ||
	      (x >= 222 && x <= 225) ||
	      (x >= 420 && x <= 450) ||
	      (x >= 902 && x <= 928)) { 

	    if ( ! A->g_quiet) {
	      snprintf (good, sizeof(good), "%07.3fMHz", x);
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("\"%s\" in comment looks like a frequency in non-standard format.\n", bad);
	      dw_printf("For most systems to recognize it, use exactly this form \"%s\" at beginning of comment.\n", good);
	    }
	    if (A->g_freq == G_UNKNOWN) {
	      A->g_freq = x;
	    }
	  }
	}

	if (A->g_tone == G_UNKNOWN && regexec (&bad_tone_re, A->g_comment, MAXMATCH, match, 0) == 0) 
	{
	  char bad1[30];	/* original 99.9 or 999.9 format or one of 67 77 100 123 */
	  char bad2[30];	/* 99.9 or 999.9 format.  ".0" appended for special cases. */
	  char good[30];
	  int i;

	  substr_se (bad1, A->g_comment, match[2].rm_so, match[2].rm_eo);
	  strlcpy (bad2, bad1, sizeof(bad2));
	  if (strcmp(bad2, "67") == 0 || strcmp(bad2, "77") == 0 || strcmp(bad2, "100") == 0 || strcmp(bad2, "123") == 0) {
	    strlcat (bad2, ".0", sizeof(bad2));
	  }

// TODO:  Why wasn't freq/PL recognized here?
// Should we recognize some cases of single decimal place as frequency?

//DECODED[194] N8VIM audio level = 27   [NONE]
//[0] N8VIM>BEACON,WIDE2-2:!4240.85N/07133.99W_PHG72604/ Pepperell, MA-> WX. 442.9+ PL100<0x0d>
//Didn't find wind direction in form c999.
//Didn't find wind speed in form s999.
//Didn't find wind gust in form g999.
//Didn't find temperature in form t999.
//Weather Report, WEATHER Station (blue)
//N 42 40.8500, W 071 33.9900
//, "PHG72604/ Pepperell, MA-> WX. 442.9+ PL100"


	  for (i = 0; i < NUM_CTCSS; i++) {
	    if (strcmp (s_ctcss[i], bad2) == 0) {

	      if ( ! A->g_quiet) {
                snprintf (good, sizeof(good), "T%03d", i_ctcss[i]);
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf("\"%s\" in comment looks like it might be a CTCSS tone in non-standard format.\n", bad1);
	        dw_printf("For most systems to recognize it, use exactly this form \"%s\" at near beginning of comment, after any frequency.\n", good);
	      }
	      if (A->g_tone == G_UNKNOWN) {
	        A->g_tone = atof(bad2);
	      }
	      break;
	    }
	  }
	}

	if ((A->g_offset == 6000 || A->g_offset == -6000) && A->g_freq >= 144 && A->g_freq <= 148) {
	  if ( ! A->g_quiet) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("A transmit offset of 6 MHz on the 2 meter band doesn't seem right.\n");
	    dw_printf("Each unit is 10 kHz so you should probably be using \"-060\" or \"+060\"\n");
	  }
	}

/*
 * TODO: samples in zfreq-test4.txt
 */

}

/* end process_comment */





/*------------------------------------------------------------------
 *
 * Function:	main
 *
 * Purpose:	Main program for standalone test program.
 *
 * Inputs:	stdin for raw data to decode.
 *		This is in the usual display format either from
 *		a TNC, findu.com, aprs.fi, etc.  e.g.
 *
 *		N1EDF-9>T2QT8Y,W1CLA-1,WIDE1*,WIDE2-2,00000:`bSbl!Mv/`"4%}_ <0x0d>
 *
 *		WB2OSZ-1>APN383,qAR,N1EDU-2:!4237.14NS07120.83W#PHG7130Chelmsford, MA
 *
 *
 * Outputs:	stdout
 *
 * Description:	Compile like this to make a standalone test program.
 *
 *		gcc -o decode_aprs -DDECAMAIN decode_aprs.c ax25_pad.c ...
 *
 *		./decode_aprs < decode_aprs.txt
 *
 *		aprs.fi precedes raw data with a time stamp which you
 *		would need to remove first.
 *
 *		cut -c26-999 tmp/kj4etp-9.txt | decode_aprs.exe
 *
 *
 * Restriction:	MIC-E message type can be problematic because it
 *		it can use unprintable characters in the information field.
 *
 *		Dire Wolf and aprs.fi print it in hexadecimal.  Example:
 *
 *		KB1KTR-8>TR3U6T,KB1KTR-9*,WB2OSZ-1*,WIDE2*,qAR,W1XM:`c1<0x1f>l!t>/>"4^}
 *		                                                       ^^^^^^
 *		                                                       ||||||
 *		What does findu.com do in this case?
 *
 *		ax25_from_text recognizes this representation so it can be used
 *		to decode raw data later.
 *
 * TODO:	To make it more useful,
 *			- Remove any leading timestamp.
 *			- Remove any "qA*" and following from the path.
 *
 *------------------------------------------------------------------*/

#if DECAMAIN

/* Stub for stand-alone decoder. */

void nmea_send_waypoint (char *wname_in, double dlat, double dlong, char symtab, char symbol,
                 float alt, float course, float speed, char *comment)
{
	return;
}



int main (int argc, char *argv[]) 
{
	char stuff[300];
	char *p;	
	packet_t pp;

#if __WIN32__

// Select UTF-8 code page for console output.
// http://msdn.microsoft.com/en-us/library/windows/desktop/ms686036(v=vs.85).aspx
// This is the default I see for windows terminal:  
// >chcp
// Active code page: 437

	//Restore on exit? oldcp = GetConsoleOutputCP();
	SetConsoleOutputCP(CP_UTF8);

#else

/*
 * Default on Raspian & Ubuntu Linux is fine.  Don't know about others.
 *
 * Should we look at LANG environment variable and issue a warning
 * if it doesn't look something like  en_US.UTF-8 ?
 */

#endif	
	if (argc >= 2) {
	  if (freopen (argv[1], "r", stdin) == NULL) {
	    fprintf(stderr, "Can't open %s for read.\n", argv[1]);
	    exit(1);
	  }
	}

	text_color_init(1);
	text_color_set(DW_COLOR_INFO);

	while (fgets(stuff, sizeof(stuff), stdin) != NULL) 
        {
	  p = stuff + strlen(stuff) - 1;
	  while (p >= stuff && (*p == '\r' || *p == '\n')) {
	    *p-- = '\0';
	  }

	  if (strlen(stuff) == 0 || stuff[0] == '#') 
          {
	    /* comment or blank line */
	    text_color_set(DW_COLOR_INFO);
	    dw_printf("%s\n", stuff);
	    continue;
          }
  	  else 
	  {
	    /* Try to process it. */

	    text_color_set(DW_COLOR_REC);
	    dw_printf("\n%s\n", stuff);	    
	 
	    pp = ax25_from_text(stuff, 1);
	    if (pp != NULL) 
            {
	      decode_aprs_t A;

	      // log directory option someday?
	      decode_aprs (&A, pp, 0);

	      //Print it all out in human readable format.

	      decode_aprs_print (&A);

	      /*
	       * Perform validity check on each address.
	       * This should print an error message if any issues.
	       */
	      (void)ax25_check_addresses(pp);

	      // Send to log file?

	      // if (logdir != NULL && *logdir != '\0') {
	      //   log_write (&A, pp, logdir);
	      // }

	      ax25_delete (pp);
	    }
	    else 
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("\n%s\n", "ERROR - Could not parse input!\n");
    	    }
	  }
	}
	return (0);
}

#endif /* DECAMAIN */

/* end decode_aprs.c */
