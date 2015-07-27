//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2012,2013,2014  John Langner, WB2OSZ
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
 *
 *
 * Assumptions:	ax25_from_frame() has been called to 
 *		separate the header and information.
 *
 *
 *------------------------------------------------------------------*/

#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>	/* for atof */
#include <string.h>	/* for strtok */
#if __WIN32__
char *strsep(char **stringp, const char *delim);
#endif
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

#define TRUE 1
#define FALSE 0


#define METERS_TO_FEET(x) ((x) * 3.2808399)
#define KNOTS_TO_MPH(x) ((x) * 1.15077945)
#define KM_TO_MILES(x) ((x) * 0.621371192)
#define MBAR_TO_INHG(x) ((x) * 0.0295333727)


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


static void print_decoded (void);

static void aprs_ll_pos (unsigned char *, int);
static void aprs_ll_pos_time (unsigned char *, int);
static void aprs_raw_nmea (unsigned char *, int);
static void aprs_mic_e (packet_t, unsigned char *, int);
//static void aprs_compressed_pos (unsigned char *, int);
static void aprs_message (unsigned char *, int);
static void aprs_object (unsigned char *, int);
static void aprs_item (unsigned char *, int);
static void aprs_station_capabilities (char *, int);
static void aprs_status_report (char *, int);
static void aprs_telemetry (char *, int);
static void aprs_raw_touch_tone (char *, int);
static void aprs_morse_code (char *, int);
static void aprs_positionless_weather_report (unsigned char *, int);
static void weather_data (char *wdata, int wind_prefix);
static void aprs_ultimeter (char *, int);
static void third_party_header (char *, int);


static void decode_position (position_t *ppos);
static void decode_compressed_position (compressed_position_t *ppos);

static double get_latitude_8 (char *p);
static double get_longitude_9 (char *p);

static double get_latitude_nmea (char *pstr, char *phemi);
static double get_longitude_nmea (char *pstr, char *phemi);

static time_t get_timestamp (char *p);
static int get_maidenhead (char *p);

static int data_extension_comment (char *pdext);
static void decode_tocall (char *dest);
//static void get_symbol (char dti, char *src, char *dest);
static void process_comment (char *pstart, int clen);


/*
 * Information extracted from the message.
 */

/* for unknown values. */

//#define G_UNKNOWN -999999


static char g_msg_type[30];		/* Message type. */

static char g_symbol_table;		/* The Symbol Table Identifier character selects one */
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


static char g_symbol_code;		/* Where the Symbol Table Identifier is 0-9 or A-Z (or a-j */
					/* with compressed position data only), the symbol comes from */
					/* the Alternate Symbol Table, and is overlaid with the */
					/* identifier (as a single digit or a capital letter). */

static double g_lat, g_lon;		/* Location, degrees.  Negative for South or West. */
					/* Set to G_UNKNOWN if missing or error. */

static char g_maidenhead[9];		/* 4 or 6 (or 8?) character maidenhead locator. */

static char g_name[20];			/* Object or item name. */

static float g_speed;			/* Speed in MPH.  */

static float g_course;			/* 0 = North, 90 = East, etc. */
	
static int g_power;			/* Transmitter power in watts. */

static int g_height;			/* Antenna height above average terrain, feet. */

static int g_gain;			/* Antenna gain in dB. */

static char g_directivity[10];		/* Direction of max signal strength */

static float g_range;			/* Precomputed radio range in miles. */

static float g_altitude;		/* Feet above median sea level.  */

static char g_mfr[80];			/* Manufacturer or application. */

static char g_mic_e_status[30];		/* MIC-E message. */

static char g_freq[40];			/* Frequency, tone, xmit offset */

static char g_comment[256];		/* Comment. */

/*------------------------------------------------------------------
 *
 * Function:	decode_aprs
 *
 * Purpose:	Optionally print packet then decode it.
 *
 * Inputs:	src	- Source Station.
 *
 *			  The SSID is used as a last resort for the
 *			  displayed symbol if not specified in any other way.
 *
 *		dest	- Destination Station.
 *
 *			  Certain destinations (GPSxxx, SPCxxx, SYMxxx) can
 *			  be used to specify the display symbol.
 *			  For the MIC-E format (used by Kenwood D7, D700), the
 *			  "destination" is really the latitude.
 *
 *		pinfo 	- pointer to information field.
 *		info_len - length of the information field.
 *
 * Outputs:	Variables above:
 *
 *			g_symbol_table, g_symbol_code,
 *			g_lat, g_lon, 
 *			g_speed, g_course, g_altitude,
 *			g_comment
 *			... and others...
 *
 *		Other functions are then called to retrieve the information.
 *
 * Bug:		This is not thread-safe because it uses static data and strtok.
 *
 *------------------------------------------------------------------*/

void decode_aprs (packet_t pp)
{
	//int naddr;
	//int err;
	char src[AX25_MAX_ADDR_LEN], dest[AX25_MAX_ADDR_LEN];
	//char *p;
	//int ssid;
	unsigned char *pinfo;
	int info_len;


  	info_len = ax25_get_info (pp, &pinfo);

	sprintf (g_msg_type, "Unknown message type %c", *pinfo);

	g_symbol_table = '/';
	g_symbol_code = ' ';		/* What should we have for default? */

	g_lat = G_UNKNOWN;
	g_lon = G_UNKNOWN;
	strcpy (g_maidenhead, "");

	strcpy (g_name, "");
	g_speed = G_UNKNOWN;
	g_course = G_UNKNOWN;

	g_power = G_UNKNOWN;
	g_height = G_UNKNOWN;
	g_gain = G_UNKNOWN;
	strcpy (g_directivity, "");

	g_range = G_UNKNOWN;
	g_altitude = G_UNKNOWN;
	strcpy(g_mfr, "");
	strcpy(g_mic_e_status, "");
	strcpy(g_freq, "");
	strcpy (g_comment, "");

/*
 * Extract source and destination including the SSID.
 */
	
	ax25_get_addr_with_ssid (pp, AX25_SOURCE, src);
	ax25_get_addr_with_ssid (pp, AX25_DESTINATION, dest);


	switch (*pinfo) {	/* "DTI" data type identifier. */

	    case '!':		/* Position without timestamp (no APRS messaging). */
				/* or Ultimeter 2000 WX Station */

	    case '=':		/* Position without timestamp (with APRS messaging). */

	      if (strncmp((char*)pinfo, "!!", 2) == 0)
	      {
		aprs_ultimeter ((char*)pinfo, info_len);
	      }
	      else
	      {	     
	        aprs_ll_pos (pinfo, info_len);
	      }
	      break;


	    //case '#':		/* Peet Bros U-II Weather station */
	    //case '*':		/* Peet Bros U-II Weather station */
	      //break;
		
	    case '$':		/* Raw GPS data or Ultimeter 2000 */
		
	      if (strncmp((char*)pinfo, "$ULTW", 5) == 0)
	      {
		aprs_ultimeter ((char*)pinfo, info_len);
	      }
	      else
	      {
	        aprs_raw_nmea (pinfo, info_len);
	      }
	      break;

	    case '\'':		/* Old Mic-E Data (but Current data for TM-D700) */
	    case '`':		/* Current Mic-E Data (not used in TM-D700) */

	      aprs_mic_e (pp, pinfo, info_len);
	      break;

	    case ')':		/* Item. */

	      aprs_item (pinfo, info_len);
	      break;
		
	    case '/':		/* Position with timestamp (no APRS messaging) */
	    case '@':		/* Position with timestamp (with APRS messaging) */

	      aprs_ll_pos_time (pinfo, info_len);
	      break;


	    case ':':		/* Message */

	      aprs_message (pinfo, info_len);
	      break;

	    case ';':		/* Object */

	      aprs_object (pinfo, info_len);
	      break;

	    case '<':		/* Station Capabilities */

	      aprs_station_capabilities ((char*)pinfo, info_len);
	      break;

	    case '>':		/* Status Report */

	      aprs_status_report ((char*)pinfo, info_len);
	      break;

	    //case '?':		/* Query */
	      //break;
		
	    case 'T':		/* Telemetry */
	      aprs_telemetry ((char*)pinfo, info_len);
	      break;

	    case '_':		/* Positionless Weather Report */

	      aprs_positionless_weather_report (pinfo, info_len);
	      break;

	    case '{':		/* user defined data */
				/* http://www.aprs.org/aprs11/expfmts.txt */

	      if (strncmp((char*)pinfo, "{tt", 3) == 0) {
	        aprs_raw_touch_tone (pinfo, info_len);
	      }
	      else if (strncmp((char*)pinfo, "{mc", 3) == 0) {
	        aprs_morse_code ((char*)pinfo, info_len);
	      }
	      else {
	        //aprs_user_defined (pinfo, info_len);
	      }
	      break;

	    case 't':		/* Raw touch tone data - NOT PART OF STANDARD */
				/* Used to convey raw touch tone sequences to */
				/* to an application that might want to interpret them. */
				/* Might move into user defined data, above. */

	      aprs_raw_touch_tone ((char*)pinfo, info_len);
	      break;

	    case 'm':		/* Morse Code data - NOT PART OF STANDARD */
				/* Used by APRStt gateway to put audible responses */
				/* into the transmit queue.  Could potentially find */
				/* other uses such as CW ID for station. */
				/* Might move into user defined data, above. */

	      aprs_morse_code ((char*)pinfo, info_len);
	      break;

	    case '}':		/* third party header */

	      third_party_header ((char*)pinfo, info_len);
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

	if (g_symbol_table == ' ' || g_symbol_code == ' ') {

	  symbols_from_dest_or_src (*pinfo, src, dest, &g_symbol_table, &g_symbol_code);
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
	    decode_tocall (dest);
	    break;
	}
	
/*
 * Print it all out in human readable format.
 */
	print_decoded ();
}


static void print_decoded (void) {

	char stemp[200];
	char tmp2[2];
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
	strcpy (stemp, g_msg_type);

	if (strlen(g_name) > 0) {
	  strcat (stemp, ", \"");
	  strcat (stemp, g_name);
	  strcat (stemp, "\"");
	}

	symbols_get_description (g_symbol_table, g_symbol_code, symbol_description);	
	strcat (stemp, ", ");
	strcat (stemp, symbol_description);

	if (strlen(g_mfr) > 0) {
	  strcat (stemp, ", ");
	  strcat (stemp, g_mfr);
	}

	if (strlen(g_mic_e_status) > 0) {
	  strcat (stemp, ", ");
	  strcat (stemp, g_mic_e_status);
	}


	if (g_power > 0) {
	  char phg[100];

	  sprintf (phg, ", %d W height=%d %ddBi %s", g_power, g_height, g_gain, g_directivity);
	  strcat (stemp, phg);
	}

	if (g_range > 0) {
	  char rng[100];

	  sprintf (rng, ", range=%.1f", g_range);
	  strcat (stemp, rng);
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
 *
 * Bug: This does not check for invalid values.
 */

	if (strlen(g_maidenhead) > 0) {
	  dw_printf("Grid square = %s, ", g_maidenhead);

	  if (g_lat == G_UNKNOWN && g_lon == G_UNKNOWN) {
	
	    g_lon = (toupper(g_maidenhead[0]) - 'A') * 20 - 180;
	    g_lat = (toupper(g_maidenhead[1]) - 'A') * 10 - 90;

	    g_lon += (g_maidenhead[2] - '0') * 2;
	    g_lat += (g_maidenhead[3] - '0');

	    if (strlen(g_maidenhead) >=6) {
	      g_lon += (toupper(g_maidenhead[4]) - 'A') * 5.0 / 60.0;
	      g_lat += (toupper(g_maidenhead[5]) - 'A') * 2.5 / 60.0;

	      g_lon += 2.5 / 60.0;	/* Move from corner to center of square */
	      g_lat += 1.25 / 60.0;
	    }
	    else {
	      g_lon += 1.0;	/* Move from corner to center of square */
	      g_lat += 0.5;
	    }
	  }
	}

	strcpy (stemp, "");

	if (g_lat != G_UNKNOWN || g_lon != G_UNKNOWN) {

// Have location but it is posible one part is invalid.

	  if (g_lat != G_UNKNOWN) {
  
	    if (g_lat >= 0) {
	      absll = g_lat;
	      news = 'N';
	    }
	    else {
	      absll = - g_lat;
	      news = 'S';
	    }
	    deg = (int) absll;
	    min = (absll - deg) * 60.0;
	    sprintf (s_lat, "%c %02d%s%07.4f", news, deg, CH_DEGREE, min);
	  }
	  else {
	    strcpy (s_lat, "Invalid Latitude");
	  }

	  if (g_lon != G_UNKNOWN) {

	    if (g_lon >= 0) {
	      absll = g_lon;
	      news = 'E';
	    }
	    else {
	      absll = - g_lon;
	      news = 'W';
	    }
	    deg = (int) absll;
	    min = (absll - deg) * 60.0;
	    sprintf (s_lon, "%c %03d%s%07.4f", news, deg, CH_DEGREE, min);
	  }
	  else {
	    strcpy (s_lon, "Invalid Longitude");
	  }	

	  sprintf (stemp, "%s, %s", s_lat, s_lon);
	}

	if (g_speed != G_UNKNOWN) {
	  char spd[20];

	  if (strlen(stemp) > 0) strcat (stemp, ", ");
	  sprintf (spd, "%.0f MPH", g_speed);
	  strcat (stemp, spd);
	};

	if (g_course != G_UNKNOWN) {
	  char cse[20];

	  if (strlen(stemp) > 0) strcat (stemp, ", ");
	  sprintf (cse, "course %.0f", g_course);
	  strcat (stemp, cse);
	};

	if (g_altitude != G_UNKNOWN) {
	  char alt[20];

	  if (strlen(stemp) > 0) strcat (stemp, ", ");
	  sprintf (alt, "alt %.0f ft", g_altitude);
	  strcat (stemp, alt);
	};

	if (strlen(g_freq) > 0) {
	  strcat (stemp, ", ");
	  strcat (stemp, g_freq);
	}


	if (strlen (stemp) > 0) {
	  text_color_set(DW_COLOR_DECODED);
	  dw_printf("%s\n", stemp);
	}


/*
 * Third line has:
 * - comment or weather
 *
 * Non-printable characters are changed to safe hexadecimal representations.
 * For example, carriage return is displayed as <0x0d>.
 *
 * Drop annoying trailing CR LF.  Anyone who cares can see it in the raw data.
 */

	n = strlen(g_comment);
	if (n >= 1 && g_comment[n-1] == '\n') {
	  g_comment[n-1] = '\0';
	  n--;
	}
	if (n >= 1 && g_comment[n-1] == '\r') {
	  g_comment[n-1] = '\0';
	  n--;
	}
	if (n > 0) {
	  int j;

	  ax25_safe_print (g_comment, -1, 0);
	  dw_printf("\n");

/*
 * Point out incorrect attempts a degree symbol.
 * 0xb0 is degree in ISO Latin1.
 * To be part of a valid UTF-8 sequence, it would need to be preceded by 11xxxxxx or 10xxxxxx.
 * 0xf8 is degree in Microsoft code page 437.
 * To be part of a valid UTF-8 sequence, it would need to be followed by 10xxxxxx.
 */
	  for (j=0; j<n; j++) {
	    if ((unsigned)g_comment[j] == (char)0xb0 &&  (j == 0 || ! (g_comment[j-1] & 0x80))) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Character code 0xb0 is probably an attempt at a degree symbol.\n");
	      dw_printf("The correct encoding is 0xc2 0xb0 in UTF-8.\n");
	    }	    	
	  }
	  for (j=0; j<n; j++) {
	    if ((unsigned)g_comment[j] == (char)0xf8 && (j == n-1 || (g_comment[j+1] & 0xc0) != 0xc0)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Character code 0xf8 is probably an attempt at a degree symbol.\n");
	      dw_printf("The correct encoding is 0xc2 0xb0 in UTF-8.\n");	    	
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
 * Outputs:	g_lat, g_lon, g_symbol_table, g_symbol_code, g_speed, g_course, g_altitude.
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

static void aprs_ll_pos (unsigned char *info, int ilen) 
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


	strcpy (g_msg_type, "Position");

	p = (struct aprs_ll_pos_s *)info;
	q = (struct aprs_compressed_pos_s *)info;
	
	if (isdigit((unsigned char)(p->pos.lat[0]))) 	/* Human-readable location. */
        {
	  decode_position (&(p->pos));

	  if (g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, we expect 7 byte "data extension" */
	    /* for the wind direction and speed. */

	    strcpy (g_msg_type, "Weather Report");
	    weather_data (p->comment, TRUE);
	  } 
	  else {
	    /* Regular position report. */

	    data_extension_comment (p->comment);
	  }
	}
	else					/* Compressed location. */
	{
	  decode_compressed_position (&(q->cpos));

	  if (g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, the wind direction and speed are in the */
	    /* compressed data so we don't expect a 7 byte "data */
	    /* extension" for them. */

	    strcpy (g_msg_type, "Weather Report");
	    weather_data (q->comment, FALSE);
	  } 
	  else {
	    /* Regular position report. */

	    process_comment (q->comment, -1);
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
 * Outputs:	g_lat, g_lon, g_symbol_table, g_symbol_code, g_speed, g_course, g_altitude.
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



static void aprs_ll_pos_time (unsigned char *info, int ilen) 
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


	strcpy (g_msg_type, "Position with time");

	time_t ts = 0;


	p = (struct aprs_ll_pos_time_s *)info;
	q = (struct aprs_compressed_pos_time_s *)info;
	

	if (isdigit((unsigned char)(p->pos.lat[0]))) 		/* Human-readable location. */
        {
	  ts = get_timestamp (p->time_stamp);
	  decode_position (&(p->pos));

	  if (g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, we expect 7 byte "data extension" */
	    /* for the wind direction and speed. */

	    strcpy (g_msg_type, "Weather Report");
	    weather_data (p->comment, TRUE);
	  } 
	  else {
	    /* Regular position report. */

	    data_extension_comment (p->comment);
	  }
	}
	else					/* Compressed location. */
	{
	  ts = get_timestamp (p->time_stamp);

	  decode_compressed_position (&(q->cpos));

	  if (g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, the wind direction and speed are in the */
	    /* compressed data so we don't expect a 7 byte "data */
	    /* extension" for them. */

	    strcpy (g_msg_type, "Weather Report");
	    weather_data (q->comment, FALSE);
	  } 
	  else {
	    /* Regular position report. */

	    process_comment (q->comment, -1);
	  }
	}

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
 * Outputs:	??? TBD
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
 * Examples:	$GPGGA,102705,5157.9762,N,00029.3256,W,1,04,2.0,75.7,M,47.6,M,,*62
 *		$GPGLL,2554.459,N,08020.187,W,154027.281,A
 *		$GPRMC,063909,A,3349.4302,N,11700.3721,W,43.022,89.3,291099,13.6,E*52
 *		$GPVTG,318.7,T,,M,35.1,N,65.0,K*69
 *
 *
 *------------------------------------------------------------------*/

static void nmea_checksum (char *sent)
{
        char *p;
        char *next;
        unsigned char cs;


// Do we have valid checksum?

        cs = 0;
        for (p = sent+1; *p != '*' && *p != '\0'; p++) {
          cs ^= *p;
        }

        p = strchr (sent, '*');
        if (p == NULL) {
	  text_color_set (DW_COLOR_INFO);
          dw_printf("Missing GPS checksum.\n");
          return;
        }
        if (cs != strtoul(p+1, NULL, 16)) {
	  text_color_set (DW_COLOR_ERROR);
          dw_printf("GPS checksum error. Expected %02x but found %s.\n", cs, p+1);
          return;
        }
        *p = '\0';      // Remove the checksum.
}

static void aprs_raw_nmea (unsigned char *info, int ilen) 
{
	char stemp[256];
	char *ptype;
	char *next;


	strcpy (g_msg_type, "Raw NMEA");

	strncpy (stemp, (char *)info, ilen);
	stemp[ilen] = '\0';
	nmea_checksum (stemp);

	next = stemp;
	ptype = strsep(&next, ",");

	if (strcmp(ptype, "$GPGGA") == 0) 
	{
	  char *ptime;			/* Time, hhmmss[.sss] */
	  char *plat;			/* Latitude */
	  char *pns;			/* North/South */
	  char *plon;			/* Longitude */
	  char *pew;			/* East/West */
	  char *pquality;		/* Fix Quality: 0=invalid, 1=GPS, 2=DGPS */
	  char *pnsat;			/* Number of satellites. */
	  char *phdop;			/* Horizontal dilution of precision. */
	  char *paltitude;		/* Altitude, meters above mean sea level. */
   	  char *pm;			/* "M" = meters */
					/* Various other stuff... */


	  ptime = strsep(&next, ",");	
	  plat = strsep(&next, ",");
	  pns = strsep(&next, ",");
	  plon = strsep(&next, ",");
	  pew = strsep(&next, ",");
	  pquality = strsep(&next, ",");
	  pnsat = strsep(&next, ",");
	  phdop = strsep(&next, ",");
	  paltitude = strsep(&next, ",");
	  pm = strsep(&next, ",");

	  /* Process time??? */

	  if (plat != NULL && strlen(plat) > 0) {
	    g_lat = get_latitude_nmea(plat, pns);
	  }
	  if (plon != NULL && strlen(plon) > 0) {
	    g_lon = get_longitude_nmea(plon, pew);
	  }
	  if (paltitude != NULL && strlen(paltitude) > 0) {
	    g_altitude = METERS_TO_FEET(atof(paltitude));
	  }
	}
	else if (strcmp(ptype, "$GPGLL") == 0)
	{
	  char *plat;		/* Latitude */
	  char *pns;		/* North/South */
	  char *plon;		/* Longitude */
	  char *pew;		/* East/West */
				/* optional Time hhmmss[.sss] */
				/* optional 'A' for data valid */

	  plat = strsep(&next, ",");
	  pns = strsep(&next, ",");
	  plon = strsep(&next, ",");
	  pew = strsep(&next, ",");

	  if (plat != NULL && strlen(plat) > 0) {
	    g_lat = get_latitude_nmea(plat, pns);
	  }
	  if (plon != NULL && strlen(plon) > 0) {
	    g_lon = get_longitude_nmea(plon, pew);
	  }

	}
	else if (strcmp(ptype, "$GPRMC") == 0)
	{
	  //char *ptime, *pstatus, *plat, *pns, *plon, *pew, *pspeed, *ptrack, *pdate;

	  char *ptime;			/* Time, hhmmss[.sss] */
	  char *pstatus;		/* Status, A=Active (valid position), V=Void */
	  char *plat;			/* Latitude */
	  char *pns;			/* North/South */
	  char *plon;			/* Longitude */
	  char *pew;			/* East/West */
	  char *pknots;			/* Speed over ground, knots. */
	  char *pcourse;		/* True course, degrees. */
	  char *pdate;			/* Date, ddmmyy */
					/* Magnetic variation */
					/* In version 3.00, mode is added: A D E N (see below) */
					/* Checksum */

	  ptime = strsep(&next, ",");
	  pstatus = strsep(&next, ",");	
	  plat = strsep(&next, ",");
	  pns = strsep(&next, ",");
	  plon = strsep(&next, ",");
	  pew = strsep(&next, ",");
	  pknots = strsep(&next, ",");
	  pcourse = strsep(&next, ",");
	  pdate = strsep(&next, ",");	

	  /* process time ??? date ??? */

	  if (plat != NULL && strlen(plat) > 0) {
	    g_lat = get_latitude_nmea(plat, pns);
	  }
	  if (plon != NULL && strlen(plon) > 0) {
	    g_lon = get_longitude_nmea(plon, pew);
	  }
	  if (pknots != NULL && strlen(pknots) > 0) {
	    g_speed = KNOTS_TO_MPH(atof(pknots));
	  }
	  if (pcourse != NULL && strlen(pcourse) > 0) {
	    g_course = atof(pcourse);
	  }
	}
	else if (strcmp(ptype, "$GPVTG") == 0)
	{

	  /* Speed and direction but NO location! */

	  char *ptcourse;		/* True course, degrees. */
	  char *pt;			/* "T" */
	  char *pmcourse;		/* Magnetic course, degrees. */
	  char *pm;			/* "M" */
	  char *pknots;			/* Ground speed, knots. */
	  char *pn;			/* "N" = Knots */
	  char *pkmh;			/* Ground speed, km/hr */
	  char *pk;			/* "K" = Kilometers per hour */
	  char *pmode;			/* New in NMEA 0183 version 3.0 */
					/* Mode: A=Autonomous, D=Differential, */
	
	  ptcourse = strsep(&next, ",");
	  pt = strsep(&next, ",");
	  pmcourse = strsep(&next, ",");
	  pm = strsep(&next, ",");
	  pknots = strsep(&next, ",");
	  pn = strsep(&next, ",");
	  pkmh = strsep(&next, ",");
	  pk = strsep(&next, ",");
	  pmode	 = strsep(&next, ",");	

	  if (pknots != NULL && strlen(pknots) > 0) {
	    g_speed = KNOTS_TO_MPH(atof(pknots));
	  }
	  if (ptcourse != NULL && strlen(ptcourse) > 0) {
	    g_course = atof(ptcourse);
	  }

	}
	else if (strcmp(ptype, "$GPWPL") == 0)
	{
	  //char *plat, *pns, *plon, *pew, *pident;

	  char *plat;			/* Latitude */
	  char *pns;			/* North/South */
	  char *plon;			/* Longitude */
	  char *pew;			/* East/West */
	  char *pident;			/* Identifier for Waypoint.  rules??? */
					/* checksum */

	  plat = strsep(&next, ",");
	  pns = strsep(&next, ",");
	  plon = strsep(&next, ",");
	  pew = strsep(&next, ",");
	  pident = strsep(&next, ",");

	  if (plat != NULL && strlen(plat) > 0) {
	    g_lat = get_latitude_nmea(plat, pns);
	  }
	  if (plon != NULL && strlen(plon) > 0) {
	    g_lon = get_longitude_nmea(plon, pew);
	  }

	  /* do something with identifier? */

	}
}



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
 *		Destination Address Field — 
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

static int mic_e_digit (char c, int mask, int *std_msg, int *cust_msg)
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

	text_color_set(DW_COLOR_ERROR);
	dw_printf("Invalid character \"%c\" in MIC-E destination/latitude.\n", c);

	return (0);
}


static void aprs_mic_e (packet_t pp, unsigned char *info, int ilen) 
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

	strcpy (g_msg_type, "MIC-E");

	p = (struct aprs_mic_e_s *)info;

/* Destination is really latitude of form ddmmhh. */
/* Message codes are buried in the first 3 digits. */

	ax25_get_addr_with_ssid (pp, AX25_DESTINATION, dest);

	g_lat = mic_e_digit(dest[0], 4, &std_msg, &cust_msg) * 10 + 
		mic_e_digit(dest[1], 2, &std_msg, &cust_msg) +
		(mic_e_digit(dest[2], 1, &std_msg, &cust_msg) * 1000 + 
		 mic_e_digit(dest[3], 0, &std_msg, &cust_msg) * 100 + 
		 mic_e_digit(dest[4], 0, &std_msg, &cust_msg) * 10 + 
		 mic_e_digit(dest[5], 0, &std_msg, &cust_msg)) / 6000.0;


/* 4th character of desination indicates north / south. */

	if ((dest[3] >= '0' && dest[3] <= '9') || dest[3] == 'L') {
	  /* South */
	  g_lat = ( - g_lat);
	}
	else if (dest[3] >= 'P' && dest[3] <= 'Z') 
	{
	  /* North */
	}
	else 
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid MIC-E N/S encoding in 4th character of destination.\n");	  
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
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid MIC-E Longitude Offset in 5th character of destination.\n");
	}

/* First character of information field is longitude in degrees. */
/* It is possible for the unprintable DEL character to occur here. */

/* 5th character of desination indicates longitude offset of +100. */
/* Not quite that simple :-( */

	ch = p->lon[0];

	if (offset && ch >= 118 && ch <= 127) 
	{
	    g_lon = ch - 118;			/* 0 - 9 degrees */
	}
	else if ( ! offset && ch >= 38 && ch <= 127)
	{
	    g_lon = (ch - 38) + 10;		/* 10 - 99 degrees */
	}
	else if (offset && ch >= 108 && ch <= 117)
	{
	    g_lon = (ch - 108) + 100;		/* 100 - 109 degrees */
	}
	else if (offset && ch >= 38 && ch <= 107)
	{
	    g_lon = (ch - 38) + 110;		/* 110 - 179 degrees */
	}
	else 
	{
	    g_lon = G_UNKNOWN;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character 0x%02x for MIC-E Longitude Degrees.\n", ch);
	}

/* Second character of information field is g_longitude minutes. */
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

	if (g_lon != G_UNKNOWN) 
	{
	  ch = p->lon[1];

	  if (ch >= 88 && ch <= 97)
	  {
	    g_lon += (ch - 88) / 60.0;	/* 0 - 9 minutes*/
	  }
	  else if (ch >= 38 && ch <= 87)
	  {
    	    g_lon += ((ch - 38) + 10) / 60.0;	/* 10 - 59 minutes */
	  }
	  else {
	    g_lon = G_UNKNOWN;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character 0x%02x for MIC-E Longitude Minutes.\n", ch);
	  }

/* Third character of information field is longitude hundredths of minutes. */
/* There are 100 possible values, from 0 to 99. */
/* Note that the range includes 4 unprintable control characters and DEL. */

	  if (g_lon != G_UNKNOWN) 
	  {
	    ch = p->lon[2];

	    if (ch >= 28 && ch <= 127) 
	    {
	      g_lon += ((ch - 28) + 0) / 6000.0;	/* 0 - 99 hundredths of minutes*/
	    }
	    else {
	      g_lon = G_UNKNOWN;
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Invalid character 0x%02x for MIC-E Longitude hundredths of Minutes.\n", ch);
	    }
	  }
	}

/* 6th character of destintation indicates east / west. */

	if ((dest[5] >= '0' && dest[5] <= '9') || dest[5] == 'L') {
	  /* East */
	}
	else if (dest[5] >= 'P' && dest[5] <= 'Z') 
	{
	  /* West */
	  if (g_lon != G_UNKNOWN) {
	    g_lon = ( - g_lon);
	  }
	}
	else 
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid MIC-E E/W encoding in 6th character of destination.\n");	  
	}

/* Symbol table and codes like everyone else. */

	g_symbol_table = p->sym_table_id;
	g_symbol_code = p->symbol_code;

	if (g_symbol_table != '/' && g_symbol_table != '\\' 
		&& ! isupper(g_symbol_table) && ! isdigit(g_symbol_table))
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid symbol table code not one of / \\ A-Z 0-9\n");	
	  g_symbol_table = '/';
	}

/* Message type from two 3-bit codes. */

	if (std_msg == 0 && cust_msg == 0) {
	  strcpy (g_mic_e_status, "Emergency");
	}
	else if (std_msg == 0 && cust_msg != 0) {
	  strcpy (g_mic_e_status, cust_text[cust_msg]);
	}
	else if (std_msg != 0 && cust_msg == 0) {
	  strcpy (g_mic_e_status, std_text[std_msg]);
	}
	else {
	  strcpy (g_mic_e_status, "Unknown MIC-E Message Type");
	}

/* Speed and course from next 3 bytes. */

	n = ((p->speed_course[0] - 28) * 10) + ((p->speed_course[1] - 28) / 10);
	if (n >= 800) n -= 800;

	g_speed = KNOTS_TO_MPH(n); 

	n = ((p->speed_course[1] - 28) % 10) * 100 + (p->speed_course[2] - 28);
	if (n >= 400) n -= 400;

	/* Result is 0 for unknown and 1 - 360 where 360 is north. */
	/* Convert to 0 - 360 and reserved value for unknown. */

	if (n == 0) 
	  g_course = G_UNKNOWN;
	else if (n == 360)
	  g_course = 0;
	else
	  g_course = n;


/* Now try to pick out manufacturer and other optional items. */
/* The telemetry field, in the original spec, is no longer used. */
  
	pfirst = info + sizeof(struct aprs_mic_e_s);
	plast = info + ilen - 1;

/* Carriage return character at the end is not mentioned in spec. */
/* Remove if found because it messes up extraction of manufacturer. */

	if (*plast == '\r') plast--;

	if (*pfirst == ' ' || *pfirst == '>' || *pfirst == ']' || *pfirst == '`' || *pfirst == '\'') {
	
	  if (*pfirst == ' ') { strcpy (g_mfr, "Original MIC-E"); pfirst++; }

	  else if (*pfirst == '>' && *plast == '=') { strcpy (g_mfr, "Kenwood TH-D72"); pfirst++; plast--; }
	  else if (*pfirst == '>') { strcpy (g_mfr, "Kenwood TH-D7A"); pfirst++; }

	  else if (*pfirst == ']' && *plast == '=') { strcpy (g_mfr, "Kenwood TM-D710"); pfirst++; plast--; }
	  else if (*pfirst == ']') { strcpy (g_mfr, "Kenwood TM-D700"); pfirst++; }

	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == ' ') { strcpy (g_mfr, "Yaesu VX-8"); pfirst++; plast-=2; }
	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == '"') { strcpy (g_mfr, "Yaesu FTM-350"); pfirst++; plast-=2; }
	  else if (*pfirst == '`' && *(plast-1) == '_' && *plast == '#') { strcpy (g_mfr, "Yaesu VX-8G"); pfirst++; plast-=2; }
	  else if (*pfirst == '\'' && *(plast-1) == '|' && *plast == '3') { strcpy (g_mfr, "Byonics TinyTrack3"); pfirst++; plast-=2; }
	  else if (*pfirst == '\'' && *(plast-1) == '|' && *plast == '4') { strcpy (g_mfr, "Byonics TinyTrack4"); pfirst++; plast-=2; }

	  else if (*(plast-1) == '\\') { strcpy (g_mfr, "Hamhud ?"); pfirst++; plast-=2; }
	  else if (*(plast-1) == '/') { strcpy (g_mfr, "Argent ?"); pfirst++; plast-=2; }
	  else if (*(plast-1) == '^') { strcpy (g_mfr, "HinzTec anyfrog"); pfirst++; plast-=2; }
	  else if (*(plast-1) == '~') { strcpy (g_mfr, "OTHER"); pfirst++; plast-=2; }

	  else if (*pfirst == '`') { strcpy (g_mfr, "Mic-Emsg"); pfirst++; plast-=2; }
	  else if (*pfirst == '\'') { strcpy (g_mfr, "McTrackr"); pfirst++; plast-=2; }
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

	  g_altitude = METERS_TO_FEET((pfirst[0]-33)*91*91 + (pfirst[1]-33)*91 + (pfirst[2]-33) - 10000);

	  if (pfirst[0] < '!' || pfirst[0] > '{' ||
	      pfirst[1] < '!' || pfirst[1] > '{' ||
	      pfirst[2] < '!' || pfirst[2] > '{' ) 
	  {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid character in MIC-E altitude.  Must be in range of '!' to '{'.\n");
	    dw_printf("Bogus altitude of %.0f changed to unknown.\n", g_altitude);
	    g_altitude = G_UNKNOWN;
	  }
	  
	  pfirst += 4;
	}

	process_comment ((char*)pfirst, (int)(plast - pfirst) + 1);

}


/*------------------------------------------------------------------
 *
 * Function:	aprs_message
 *
 * Purpose:	Decode "Message Format"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	??? TBD
 *
 * Description:	An APRS message is a text string with a specifed addressee.
 *
 *		It's a lot more complicated with different types of addressees
 *		and replies with acknowledgement or rejection.
 *
 *		Displaying and logging these messages could be useful.
 *
 * Examples:	
 *		
 *
 *------------------------------------------------------------------*/

static void aprs_message (unsigned char *info, int ilen) 
{

	struct aprs_message_s {
	  char dti;			/* : */
	  char addressee[9];
	  char colon;			/* : */
	  char message[73];		/* 0-67 characters for message */
					/* { followed by 1-5 characters for message number */
	} *p;


	p = (struct aprs_message_s *)info;

	sprintf (g_msg_type, "APRS Message for \"%9.9s\"", p->addressee);

	/* No location so don't use  process_comment () */

	strcpy (g_comment, p->message);

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
 * Outputs:	g_object_name, g_lat, g_lon, g_symbol_table, g_symbol_code, g_speed, g_course, g_altitude.
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

static void aprs_object (unsigned char *info, int ilen) 
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

	strncpy (g_name, p->name, 9);
	g_name[9] = '\0';
	i = strlen(g_name) - 1;
	while (i >= 0 && g_name[i] == ' ') {
	  g_name[i--] = '\0';
	}

	if (p->live_killed == '*')
	  strcpy (g_msg_type, "Object");
	else if (p->live_killed == '_')
	  strcpy (g_msg_type, "Killed Object");
	else
	  strcpy (g_msg_type, "Object - invalid live/killed");

	ts = get_timestamp (p->time_stamp);

	if (isdigit((unsigned char)(p->pos.lat[0]))) 	/* Human-readable location. */
        {
	  decode_position (&(p->pos));

	  if (g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* In this case, we expect 7 byte "data extension" */
	    /* for the wind direction and speed. */

	    strcpy (g_msg_type, "Weather Report with Object");
	    weather_data (p->comment, TRUE);
	  } 
	  else {
	    /* Regular object. */

	    data_extension_comment (p->comment);
	  }
	}
	else					/* Compressed location. */
	{
	  decode_compressed_position (&(q->cpos));

	  if (g_symbol_code == '_') {
	    /* Symbol code indidates it is a weather report. */
	    /* The spec doesn't explicitly mention the combination */
	    /* of weather report and object with compressed */
	    /* position. */

	    strcpy (g_msg_type, "Weather Report with Object");
	    weather_data (q->comment, FALSE);
	  } 
	  else {
	    /* Regular position report. */

	    process_comment (q->comment, -1);
	  }
	}

}


/*------------------------------------------------------------------
 *
 * Function:	aprs_item
 *
 * Purpose:	Decode "Item Report Format"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	g_object_name, g_lat, g_lon, g_symbol_table, g_symbol_code, g_speed, g_course, g_altitude.
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

static void aprs_item (unsigned char *info, int ilen) 
{

	struct aprs_item_s {
	  char dti;			/* ) */
	  char name[9];			/* Actually variable length 3 - 9 bytes. */
	  char live_killed;		/* ! for live or _ for killed */
	  position_t pos;
	  char comment[43]; 		/* First 7 bytes could be data extension. */
	} *p;

	struct aprs_compressed_item_s {
	  char dti;			/* ) */
	  char name[9];			/* Actually variable length 3 - 9 bytes. */
	  char live_killed;		/* ! for live or _ for killed */
	  compressed_position_t cpos;
	  char comment[40]; 		/* No data extension in this case. */
	} *q;


	time_t ts = 0;
	int i;
	char *ppos;


	p = (struct aprs_item_s *)info;
	q = (struct aprs_compressed_item_s *)info;

	i = 0;
	while (i < 9 && p->name[i] != '!' && p->name[i] != '_') {
	  g_name[i] = p->name[i];
	  i++;
	  g_name[i] = '\0';
	}

	if (p->name[i] == '!')
	  strcpy (g_msg_type, "Item");
	else if (p->name[i] == '_')
	  strcpy (g_msg_type, "Killed Item");
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Item name too long or not followed by ! or _.\n");
	  strcpy (g_msg_type, "Object - invalid live/killed");
	}

	ppos = p->name + i + 1;
 
	if (isdigit(*ppos)) 		/* Human-readable location. */
        {
	  decode_position ((position_t*) ppos);

	  data_extension_comment (ppos + sizeof(position_t));
	}
	else					/* Compressed location. */
	{
	  decode_compressed_position ((compressed_position_t*)ppos);

	  process_comment (ppos + sizeof(compressed_position_t), -1);
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

static void aprs_station_capabilities (char *info, int ilen) 
{

	strcpy (g_msg_type, "Station Capabilities");

	// 	Is process_comment() applicable?

	strcpy (g_comment, info+1);
}




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

static void aprs_status_report (char *info, int ilen) 
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


	strcpy (g_msg_type, "Status Report");

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

	  strcpy (g_comment, pt->comment);
	}

/*
 * Do we have format with 6 character Maidenhead locator?
 */
	else if (get_maidenhead (pm6->mhead6) == 6) {

	  strncpy (g_maidenhead, pm6->mhead6, 6);
	  g_maidenhead[6] = '\0';

	  g_symbol_table = pm6->sym_table_id;
	  g_symbol_code = pm6->symbol_code;

	  if (g_symbol_table != '/' && g_symbol_table != '\\' 
		&& ! isupper(g_symbol_table) && ! isdigit(g_symbol_table))
	  {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid symbol table code '%c' not one of / \\ A-Z 0-9\n", g_symbol_table);	
	    g_symbol_table = '/';
	  }

	  if (pm6->space != ' ' && pm6->space != '\0') {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Error: Found '%c' instead of space required after symbol code.\n", pm6->space);	
	  }

	  strcpy (g_comment, pm6->comment);
	}

/*
 * Do we have format with 4 character Maidenhead locator?
 */
	else if (get_maidenhead (pm4->mhead4) == 4) {

	  strncpy (g_maidenhead, pm4->mhead4, 4);
	  g_maidenhead[4] = '\0';

	  g_symbol_table = pm4->sym_table_id;
	  g_symbol_code = pm4->symbol_code;

	  if (g_symbol_table != '/' && g_symbol_table != '\\' 
		&& ! isupper(g_symbol_table) && ! isdigit(g_symbol_table))
	  {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Invalid symbol table code '%c' not one of / \\ A-Z 0-9\n", g_symbol_table);	
	    g_symbol_table = '/';
	  }

	  if (pm4->space != ' ' && pm4->space != '\0') {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Error: Found '%c' instead of space required after symbol code.\n", pm4->space);	
	  }

	  strcpy (g_comment, pm4->comment);
	}

/*
 * Whole thing is status text.
 */
	else {
	  strcpy (g_comment, ps->comment);
	}


/*
 * Last 3 characters can represent beam heading and ERP.
 */

	if (strlen(g_comment) >= 3) {
	  char *hp = g_comment + strlen(g_comment) - 3;
	
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

	// TODO:  put result somewhere.
	// could use g_directivity and need new variable for erp.

	    *hp = '\0';
	  }
	}
}


/*------------------------------------------------------------------
 *
 * Function:	aprs_Telemetry
 *
 * Purpose:	Decode "Telemetry"
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	???
 *
 * Description:	TBD.
 *
 * Examples from specification:	
 *		
 *
 *		TBD
 *	
 *------------------------------------------------------------------*/

static void aprs_telemetry (char *info, int ilen) 
{

	strcpy (g_msg_type, "Telemetry");

	/* It's pretty much human readable already. */
	/* Just copy the info field. */

	strcpy (g_comment, info);


} /* end aprs_telemetry */


/*------------------------------------------------------------------
 *
 * Function:	aprs_raw_touch_tone
 *
 * Purpose:	Decode raw touch tone data.
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

static void aprs_raw_touch_tone (char *info, int ilen) 
{

	strcpy (g_msg_type, "Raw Touch Tone Data");

	/* Just copy the info field without the message type. */

	if (*info == '{') 
	  strcpy (g_comment, info+3);
	else
	  strcpy (g_comment, info+1);


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

static void aprs_morse_code (char *info, int ilen) 
{

	strcpy (g_msg_type, "Morse Code Data");

	/* Just copy the info field without the message type. */

	if (*info == '{') 
	  strcpy (g_comment, info+3);
	else
	  strcpy (g_comment, info+1);


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
 * Outputs:	g_symbol_table, g_symbol_code.
 *
 * Description:	Type identifier '_' is a weather report without a position.
 *
 *------------------------------------------------------------------*/



static void aprs_positionless_weather_report (unsigned char *info, int ilen) 
{

	struct aprs_positionless_weather_s {
	  char dti;			/* _ */
	  char time_stamp[8];		/* MDHM format */
	  char comment[99]; 		
	} *p;


	strcpy (g_msg_type, "Positionless Weather Report");

	time_t ts = 0;


	p = (struct aprs_positionless_weather_s *)info;
	
	// not yet implemented for 8 character format // ts = get_timestamp (p->time_stamp);

	weather_data (p->comment, FALSE);
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
 * Global In:	g_course	- Wind info for compressed location.
 *		g_speed
 *
 * Outputs:	g_comment
 *
 * Description:	Extract weather details and format into a comment.
 *
 *		For human-readable locations, we expect wind direction
 *		and speed in a format like this:  999/999.
 *		For compressed location, this has already been 
 * 		processed and put in g_course and g_speed.
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
	char stemp[8];
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

	strncpy (stemp, (*wpp)+1, dlen);
	stemp[dlen] = '\0';
	*val = atof(stemp);

	//dw_printf("debug: getwdata returning %f\n", *val);

	*wpp += 1 + dlen;
	return (1); 
}	

static void weather_data (char *wdata, int wind_prefix) 
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
	    g_course = n;
	  }
	  if (sscanf (wp+4, "%3d", &n))
	  {
	    g_speed = KNOTS_TO_MPH(n);  /* yes, in knots */
	  }
	  wp += 7;
	}
	else if ( g_speed == G_UNKNOWN) {

	  if ( ! getwdata (&wp, 'c', 3, &g_course)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Didn't find wind direction in form c999.\n");
	  }
	  if ( ! getwdata (&wp, 's', 3, &g_speed)) {	/* MPH here */
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Didn't find wind speed in form s999.\n");
	  }
	}

// At this point, we should have the wind direction and speed
// from one of three methods.

	if (g_speed != G_UNKNOWN) {
	  char ctemp[30];

	  sprintf (g_comment, "wind %.1f mph", g_speed);
	  if (g_course != G_UNKNOWN) {
	    sprintf (ctemp, ", direction %.0f", g_course);
	    strcat (g_comment, ctemp);
	  }
	}

	/* We don't want this to show up on the location line. */
	g_speed = G_UNKNOWN;
	g_course = G_UNKNOWN;

/*
 * After the mandatory wind direction and speed (in 1 of 3 formats), the
 * next two must be in fixed positions:
 * - gust (peak in mph last 5 minutes)
 * - temperature, degrees F, can be negative e.g. -01
 */
	if (getwdata (&wp, 'g', 3, &fval)) {
	  if (fval != G_UNKNOWN) {
	    char ctemp[30];
	    sprintf (ctemp, ", gust %.0f", fval);
	    strcat (g_comment, ctemp);
	  }
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Didn't find wind gust in form g999.\n");
	}

	if (getwdata (&wp, 't', 3, &fval)) {
	  if (fval != G_UNKNOWN) {
	    char ctemp[30];
	    sprintf (ctemp, ", temperature %.0f", fval);
	    strcat (g_comment, ctemp);
	  }
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Didn't find temperature in form t999.\n");
	}

/*
 * Now pick out other optional fields in any order.
 */
	keep_going = 1;
	while (keep_going) {

	  if (getwdata (&wp, 'r', 3, &fval)) {	

	/* r = rainfall, 1/100 inch, last hour */

	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      sprintf (ctemp, ", rain %.2f in last hour", fval / 100.);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 'p', 3, &fval)) {	

	/* p = rainfall, 1/100 inch, last 24 hours */

	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      sprintf (ctemp, ", rain %.2f in last 24 hours", fval / 100.);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 'P', 3, &fval)) {	

	/* P = rainfall, 1/100 inch, since midnight */

	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      sprintf (ctemp, ", rain %.2f since midnight", fval / 100.);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 'h', 2, &fval)) {	

	/* h = humidity %, 00 means 100%  */

	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      if (fval == 0) fval = 100;
	      sprintf (ctemp, ", humidity %.0f", fval);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 'b', 5, &fval)) {	

	/* b = barometric presure (tenths millibars / tenths of hPascal)  */
	/* Here, display as inches of mercury. */

	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      fval = MBAR_TO_INHG(fval * 0.1);
	      sprintf (ctemp, ", barometer %.2f", fval);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 'L', 3, &fval)) {	

	/* L = Luminosity, watts/ sq meter, 000-999  */
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      sprintf (ctemp, ", %.0f watts/m^2", fval);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 'l', 3, &fval)) {	

	/* l = Luminosity, watts/ sq meter, 1000-1999  */
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      sprintf (ctemp, ", %.0f watts/m^2", fval + 1000);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 's', 3, &fval)) {	

	/* s = Snowfall in last 24 hours, inches  */
	/* Data can have decimal point so we don't have to worry about scaling. */
	/* 's' is also used by wind speed but that must be in a fixed */
	/* position in the message so there is no confusion. */
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      sprintf (ctemp, ", %.1f snow in 24 hours", fval);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 's', 3, &fval)) {	

	/* # = Raw rain counter  */
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      sprintf (ctemp, ", raw rain counter %.f", fval);
	      strcat (g_comment, ctemp);
	    }
	  }
	  else if (getwdata (&wp, 'X', 3, &fval)) {	

	/* X = Nuclear Radiation.  */
	/* Encoded as two significant digits and order of magnitude */
	/* like resistor color code. */

// TODO: decode this properly
	
	    if (fval != G_UNKNOWN) {
	      char ctemp[30];
	      sprintf (ctemp, ", nuclear Radiation %.f", fval);
	      strcat (g_comment, ctemp);
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

	strcat (g_comment, ", \"");
	strcat (g_comment, wp);
/*
 * Drop any CR / LF character at the end.
 */
	n = strlen(g_comment);
	if (n >= 1 && g_comment[n-1] == '\n') {
	  g_comment[n-1] = '\0';
	}

	n = strlen(g_comment);
	if (n >= 1 && g_comment[n-1] == '\r') {
	  g_comment[n-1] = '\0';
	}

	strcat (g_comment, "\"");

	return;
}


/*------------------------------------------------------------------
 *
 * Function:	aprs_ultimeter
 *
 * Purpose:	Decode Peet Brothers ULTIMETER Weather Station Info.
 *
 * Inputs:	info 	- Pointer to Information field.
 *		ilen 	- Information field length.
 *
 * Outputs:	g_comment
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

static void aprs_ultimeter (char *info, int ilen) 
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

	strcpy (g_msg_type, "Ultimeter");

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

	    windpeak = KM_TO_MILES(h_windpeak * 0.1);
	    wdir = (h_wdir & 0xff) * 360. / 256.;
	    otemp = h_otemp * 0.1;
	    baro = MBAR_TO_INHG(h_baro * 0.1);
	    ohumid = h_ohumid * 0.1;
	  
	    sprintf (g_comment, "wind %.1f mph, direction %.0f, temperature %.1f, barometer %.2f, humidity %.0f",
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

	    windpeak = KM_TO_MILES(h_windpeak * 0.1);
	    wdir = (h_wdir & 0xff) * 360. / 256.;
	    otemp = h_otemp * 0.1;
	  
	    sprintf (g_comment, "wind %.1f mph, direction %.0f, temperature %.1f\n",
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
 * Outputs:	g_comment
 *
 * Description:	
 *
 *------------------------------------------------------------------*/

static void third_party_header (char *info, int ilen) 
{
	int n;

	strcpy (g_msg_type, "Third Party Header");

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
 * Returns:	g_lat
 *		g_lon
 *		g_symbol_table
 *		g_symbol_code
 *
 * Description:	This provides resolution of about 60 feet.
 *		This can be improved by using !DAO! in the comment.
 *
 *------------------------------------------------------------------*/


static void decode_position (position_t *ppos)
{

	  g_lat = get_latitude_8 (ppos->lat);
	  g_lon = get_longitude_9 (ppos->lon);

	  g_symbol_table = ppos->sym_table_id;
	  g_symbol_code = ppos->symbol_code;
}

/*------------------------------------------------------------------
 *
 * Function:	decode_compressed_position
 *
 * Purpose:	Decode the compressed position & symbol information common to many message formats.
 *
 * Inputs:	ppos 	- Pointer to compressed position & symbol fields.
 *
 * Returns:	g_lat
 *		g_lon
 *		g_symbol_table
 *		g_symbol_code
 *
 *		One of the following:
 *			g_course & g_speeed
 *			g_altitude
 *			g_range
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


static void decode_compressed_position (compressed_position_t *pcpos)
{
	if (pcpos->y[0] >= '!' && pcpos->y[0] <= '{' &&
	    pcpos->y[1] >= '!' && pcpos->y[1] <= '{' &&
	    pcpos->y[2] >= '!' && pcpos->y[2] <= '{' &&
	    pcpos->y[3] >= '!' && pcpos->y[3] <= '{' ) 
	{
	  g_lat = 90 - ((pcpos->y[0]-33)*91*91*91 + (pcpos->y[1]-33)*91*91 + (pcpos->y[2]-33)*91 + (pcpos->y[3]-33)) / 380926.0;
	}
	else
 	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in compressed latitude.  Must be in range of '!' to '{'.\n");
	  g_lat = G_UNKNOWN;
	}
	  
	if (pcpos->x[0] >= '!' && pcpos->x[0] <= '{' &&
	    pcpos->x[1] >= '!' && pcpos->x[1] <= '{' &&
	    pcpos->x[2] >= '!' && pcpos->x[2] <= '{' &&
	    pcpos->x[3] >= '!' && pcpos->x[3] <= '{' ) 
	{
	  g_lon = -180 + ((pcpos->x[0]-33)*91*91*91 + (pcpos->x[1]-33)*91*91 + (pcpos->x[2]-33)*91 + (pcpos->x[3]-33)) / 190463.0;
	}
	else 
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in compressed longitude.  Must be in range of '!' to '{'.\n");
	  g_lon = G_UNKNOWN;
	}

	if (pcpos->sym_table_id == '/' || pcpos->sym_table_id == '\\' || isupper((int)(pcpos->sym_table_id))) {
	  /* primary or alternate or alternate with upper case overlay. */
	  g_symbol_table = pcpos->sym_table_id;
   	}
	else if (pcpos->sym_table_id >= 'a' && pcpos->sym_table_id <= 'j') {
	  /* Lower case a-j are used to represent overlay characters 0-9 */
	  /* because a digit here would mean normal (non-compressed) location. */
	  g_symbol_table = pcpos->sym_table_id - 'a' + '0';
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid symbol table id for compressed position.\n");
	  g_symbol_table = '/';
	}

	g_symbol_code = pcpos->symbol_code;

	if (pcpos->c == ' ') {
	  ; /* ignore other two bytes */
	}
	else if (((pcpos->t - 33) & 0x18) == 0x10) {
	  g_altitude = pow(1.002, (pcpos->c - 33) * 91 + pcpos->s - 33);
	}
	else if (pcpos->c == '{')
	{
	  g_range = 2.0 * pow(1.08, pcpos->s - 33);
	}
	else if (pcpos->c >= '!' && pcpos->c <= 'z')
	{
	  /* For a weather station, this is wind information. */
	  g_course = (pcpos->c - 33) * 4;
	  g_speed = KNOTS_TO_MPH(pow(1.08, pcpos->s - 33) - 1.0);
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

double get_latitude_8 (char *p)
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
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in latitude.  Expected 0-9 for tens of degrees.\n");
	  return (G_UNKNOWN);
	}

	if (isdigit(plat->deg[1]))
	  result += ((plat->deg[1]) - '0') * 1;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in latitude.  Expected 0-9 for degrees.\n");
	  return (G_UNKNOWN);
	}

	if (plat->minn[0] >= '0' || plat->minn[0] <= '5')
	  result += ((plat->minn[0]) - '0') * (10. / 60.);
	else if (plat->minn[0] == ' ')
	  ;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in latitude.  Expected 0-5 for tens of minutes.\n");
	  return (G_UNKNOWN);
	}

	if (isdigit(plat->minn[1]))
	  result += ((plat->minn[1]) - '0') * (1. / 60.);
	else if (plat->minn[1] == ' ')
	  ;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in latitude.  Expected 0-9 for minutes.\n");
	  return (G_UNKNOWN);
	}

	if (plat->dot != '.') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Unexpected character \"%c\" found where period expected in latitude.\n", plat->dot);
	  return (G_UNKNOWN);
	} 

	if (isdigit(plat->hmin[0]))
	  result += ((plat->hmin[0]) - '0') * (0.1 / 60.);
	else if (plat->hmin[0] == ' ')
	  ;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in latitude.  Expected 0-9 for tenths of minutes.\n");
	  return (G_UNKNOWN);
	}

	if (isdigit(plat->hmin[1]))
	  result += ((plat->hmin[1]) - '0') * (0.01 / 60.);
	else if (plat->hmin[1] == ' ')
	  ;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in latitude.  Expected 0-9 for hundredths of minutes.\n");
	  return (G_UNKNOWN);
	}

// The spec requires upper case for hemisphere.  Accept lower case but warn.

	if (plat->ns == 'N') {
	  return (result);
        }
        else if (plat->ns == 'n') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Warning: Lower case n found for latitude hemisphere.  Specification requires upper case N or S.\n");	  
	  return (result);
	}
	else if (plat->ns == 'S') {
	  return ( - result);
	}
	else if (plat->ns == 's') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Warning: Lower case s found for latitude hemisphere.  Specification requires upper case N or S.\n");	  
	  return ( - result);
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: '%c' found for latitude hemisphere.  Specification requires upper case N or s.\n", plat->ns);	  
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


double get_longitude_9 (char *p)
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
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in longitude.  Expected 0 or 1 for hundreds of degrees.\n");
	  return (G_UNKNOWN);
	}

	if (isdigit(plon->deg[1]))
	  result += ((plon->deg[1]) - '0') * 10;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in longitude.  Expected 0-9 for tens of degrees.\n");
	  return (G_UNKNOWN);
	}

	if (isdigit(plon->deg[2]))
	  result += ((plon->deg[2]) - '0') * 1;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in longitude.  Expected 0-9 for degrees.\n");
	  return (G_UNKNOWN);
	}

	if (plon->minn[0] >= '0' || plon->minn[0] <= '5')
	  result += ((plon->minn[0]) - '0') * (10. / 60.);
	else if (plon->minn[0] == ' ')
	  ;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in longitude.  Expected 0-5 for tens of minutes.\n");
	  return (G_UNKNOWN);
	}

	if (isdigit(plon->minn[1]))
	  result += ((plon->minn[1]) - '0') * (1. / 60.);
	else if (plon->minn[1] == ' ')
	  ;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in longitude.  Expected 0-9 for minutes.\n");
	  return (G_UNKNOWN);
	}

	if (plon->dot != '.') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Unexpected character \"%c\" found where period expected in longitude.\n", plon->dot);
	  return (G_UNKNOWN);
	} 

	if (isdigit(plon->hmin[0]))
	  result += ((plon->hmin[0]) - '0') * (0.1 / 60.);
	else if (plon->hmin[0] == ' ')
	  ;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in longitude.  Expected 0-9 for tenths of minutes.\n");
	  return (G_UNKNOWN);
	}

	if (isdigit(plon->hmin[1]))
	  result += ((plon->hmin[1]) - '0') * (0.01 / 60.);
	else if (plon->hmin[1] == ' ')
	  ;
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Invalid character in longitude.  Expected 0-9 for hundredths of minutes.\n");
	  return (G_UNKNOWN);
	}

// The spec requires upper case for hemisphere.  Accept lower case but warn.

	if (plon->ew == 'E') {
	  return (result);
        }
        else if (plon->ew == 'e') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Warning: Lower case e found for longitude hemisphere.  Specification requires upper case E or W.\n");	  
	  return (result);
	}
	else if (plon->ew == 'W') {
	  return ( - result);
	}
	else if (plon->ew == 'w') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Warning: Lower case w found for longitude hemisphere.  Specification requires upper case E or W.\n");	  
	  return ( - result);
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: '%c' found for longitude hemisphere.  Specification requires upper case E or W.\n", plon->ew);	  
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
 *		/). The day/time group consists of a two-digit day-of-the-month (01–31) and
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
 *		consisting of the month (01–12) and day-of-the-month (01–31), followed by
 *		the time in hours and minutes zulu. For example:
 *
 *		  10092345 is 23 hours 45 minutes zulu on October 9th.
 *
 *		This format is only used in reports from stand-alone “positionless” weather
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


time_t get_timestamp (char *p)
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


int get_maidenhead (char *p)
{

	if (toupper(p[0]) >= 'A' && toupper(p[0]) <= 'R' &&
	    toupper(p[1]) >= 'A' && toupper(p[1]) <= 'R' &&
	    isdigit(p[2]) && isdigit(p[3])) {

	  /* We have 4 characters matching the rule. */

	  if (islower(p[0]) || islower(p[1])) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Warning: Lower case letter in Maidenhead locator.  Specification requires upper case.\n");	  
	  }

	  if (toupper(p[4]) >= 'A' && toupper(p[4]) <= 'X' &&
	      toupper(p[5]) >= 'A' && toupper(p[5]) <= 'X') {

	    /* We have 6 characters matching the rule. */

	    if (islower(p[4]) || islower(p[5])) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Warning: Lower case letter in Maidenhead locator.  Specification requires upper case.\n");	  
	    }
	  
	    return 6;
	  }
	
	  return 4;
	}

	return 0;
}


/*------------------------------------------------------------------
 *
 * Function:	get_latitude_nmea
 *
 * Purpose:	Convert NMEA latitude encoding to degrees.
 *
 * Inputs:	pstr 	- Pointer to numeric string.
 *		phemi	- Pointer to following field.  Should be N or S.
 *
 * Returns:	Double precision value in degrees.  Negative for South.
 *
 * Description:	Latitude field has
 *			2 digits for degrees
 *			2 digits for minutes
 *			period
 *			Variable number of fractional digits for minutes.
 *			I've seen 2, 3, and 4 fractional digits.
 *
 *
 * Bugs:	Very little validation of data.
 *
 * Errors:	Return constant G_UNKNOWN for any type of error.
 *		Could we use special "NaN" code?
 *
 *------------------------------------------------------------------*/


static double get_latitude_nmea (char *pstr, char *phemi)
{

	double lat;

	if ( ! isdigit((unsigned char)(pstr[0]))) return (G_UNKNOWN);

	if (pstr[4] != '.') return (G_UNKNOWN);


	lat = (pstr[0] - '0') * 10 + (pstr[1] - '0') + atof(pstr+2) / 60.0;

	if (lat < 0 || lat > 90) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: Latitude not in range of 0 to 90.\n");	  
	}

	// Saw this one time:
	//	$GPRMC,000000,V,0000.0000,0,00000.0000,0,000,000,000000,,*01

	// If location is unknown, I think the hemisphere should be
	// an empty string.  TODO: Check on this.

	if (*phemi != 'N' && *phemi != 'S' && *phemi != '\0') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: Latitude hemisphere should be N or S.\n");	  
	}

	if (*phemi == 'S') lat = ( - lat);

	return (lat);
}




/*------------------------------------------------------------------
 *
 * Function:	get_longitude_nmea
 *
 * Purpose:	Convert NMEA longitude encoding to degrees.
 *
 * Inputs:	pstr 	- Pointer to numeric string.
 *		phemi	- Pointer to following field.  Should be E or W.
 *
 * Returns:	Double precision value in degrees.  Negative for West.
 *
 * Description:	Longitude field has
 *			3 digits for degrees
 *			2 digits for minutes
 *			period
 *			Variable number of fractional digits for minutes
 *
 *
 * Bugs:	Very little validation of data.
 *
 * Errors:	Return constant G_UNKNOWN for any type of error.
 *		Could we use special "NaN" code?
 *
 *------------------------------------------------------------------*/


static double get_longitude_nmea (char *pstr, char *phemi)
{
	double lon;

	if ( ! isdigit((unsigned char)(pstr[0]))) return (G_UNKNOWN);

	if (pstr[5] != '.') return (G_UNKNOWN);

	lon = (pstr[0] - '0') * 100 + (pstr[1] - '0') * 10 + (pstr[2] - '0') + atof(pstr+3) / 60.0;

	if (lon < 0 || lon > 180) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: Longitude not in range of 0 to 180.\n");	  
	}
	
	if (*phemi != 'E' && *phemi != 'W' && *phemi != '\0') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: Longitude hemisphere should be E or W.\n");	  
	}

	if (*phemi == 'W') lon = ( - lon);

	return (lon);
}


/*------------------------------------------------------------------
 *
 * Function:	data_extension_comment
 *
 * Purpose:	A fixed length 7-byte field may follow APRS position data.
 *
 * Inputs:	pdext	- Pointer to optional data extension and comment.
 *
 * Returns:	true if a data extension was found.
 *
 * Outputs:	One or more of the following, depending the data found:
 *	
 *			g_course
 *			g_speed
 *			g_power 
 *			g_height 
 *			g_gain 
 *			g_directivity 
 *			g_range
 *
 *		Anything left over will be put in 
 *
 *			g_comment			
 *
 * Description:	
 *
 *
 *
 *------------------------------------------------------------------*/

const char *dir[9] = { "omni", "NE", "E", "SE", "S", "SW", "W", "NW", "N" };

static int data_extension_comment (char *pdext)
{
	int n;

	if (strlen(pdext) < 7) {
	  strcpy (g_comment, pdext);
	  return 0;
	}

/* Tyy/Cxx - Area object descriptor. */

	if (pdext[0] == 'T' &&
		pdext[3] == '/' &&
	 	pdext[4] == 'C')
	{
	  /* not decoded at this time */
	  process_comment (pdext+7, -1);
	  return 1;
	}

/* CSE/SPD */
/* For a weather station (symbol code _) this is wind. */
/* For others, it would be course and speed. */

	if (pdext[3] == '/')
	{
	  if (sscanf (pdext, "%3d", &n))
	  {
	    g_course = n;
	  }
	  if (sscanf (pdext+4, "%3d", &n))
	  {
	    g_speed = KNOTS_TO_MPH(n);
	  }

	  /* Bearing and Number/Range/Quality? */

	  if (pdext[7] == '/' && pdext[11] == '/') 
	  {
	    process_comment (pdext + 7 + 8, -1);
	  }
	  else {
	    process_comment (pdext+7, -1);
	  }
	  return 1;
	}

/* check for Station power, height, gain. */

	if (strncmp(pdext, "PHG", 3) == 0)
	{
	  g_power = (pdext[3] - '0') * (pdext[3] - '0');
	  g_height = (1 << (pdext[4] - '0')) * 10;
	  g_gain = pdext[5] - '0';
	  if (pdext[6] >= '0' && pdext[6] <= '8') {
	    strcpy (g_directivity, dir[pdext[6]-'0']);
	  }

	  process_comment (pdext+7, -1);
	  return 1;
	}

/* check for precalculated radio range. */

	if (strncmp(pdext, "RNG", 3) == 0)
	{
	  if (sscanf (pdext+3, "%4d", &n))
	  {
	    g_range = n;
	  }
	  process_comment (pdext+7, -1);
	  return 1;
	}

/* DF signal strength,  */

	if (strncmp(pdext, "DFS", 3) == 0)
	{
	  //g_strength = pdext[3] - '0';
	  g_height = (1 << (pdext[4] - '0')) * 10;
	  g_gain = pdext[5] - '0';
	  if (pdext[6] >= '0' && pdext[6] <= '8') {
	    strcpy (g_directivity, dir[pdext[6]-'0']);
	  }

	  process_comment (pdext+7, -1);
	  return 1;
	}

	process_comment (pdext, -1);
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
 * Outputs:	g_mfr
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

static int tocall_cmp (const struct tocalls_s *x, const struct tocalls_s *y) 
{
	if (x->len != y->len) return (y->len - x->len);
	return (strcmp(x->prefix, y->prefix));
}

static void decode_tocall (char *dest)
{
	FILE *fp;
	int n;
	static int first_time = 1;
	char stuff[100];
	char *p;
	char *r;

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

	  fp = fopen("tocalls.txt", "r");
#ifndef __WIN32__
	  if (fp == NULL) {
	    fp = fopen("/usr/share/direwolf/tocalls.txt", "r");
	  }
#endif
	  if (fp != NULL) {

	    while (fgets(stuff, sizeof(stuff), fp) != NULL && num_tocalls < MAX_TOCALLS) {
	      
	      p = stuff + strlen(stuff) - 1;
	      while (p >= stuff && (*p == '\r' || *p == '\n')) {
	        *p-- = '\0';
	      }

	      // printf("debug: %s\n", stuff);

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

#if __WIN32__
	    qsort (tocalls, num_tocalls, sizeof(struct tocalls_s), tocall_cmp);
#else
	    qsort (tocalls, num_tocalls, sizeof(struct tocalls_s), (__compar_fn_t)tocall_cmp);
#endif
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Warning: Could not open 'tocalls.txt'.\n");
	    dw_printf("System types in the destination field will not be decoded.\n");
	  }

	
	  first_time = 0;
	}


	for (n=0; n<num_tocalls; n++) {
	  if (strncmp(dest, tocalls[n].prefix, tocalls[n].len) == 0) {
	    strncpy (g_mfr, tocalls[n].description, sizeof(g_mfr)-1);
	    g_mfr[sizeof(g_mfr)-1] = '\0';
	    return;
	  }
	}

} /* end decode_tocall */ 


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
 * Outputs:	g_comment
 *
 * Description:	After processing fixed and possible optional parts
 *		of the message, everything left over is a comment.
 *
 *		Except!!!
 *
 *		There are could be some other pieces of data, with 
 *		particular formats, buried in there.
 *		Pull out those special items and put everything 
 *		else into g_comment.
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

#define sign(x) (((x)>=0)?1:(-1))

static void process_comment (char *pstart, int clen)
{
	static int first_time = 1;
	static regex_t freq_re;	/* These must be static! */
	static regex_t dao_re;	/* These must be static! */
	static regex_t alt_re;	/* These must be static! */
	int e;
	char emsg[100];
#define MAXMATCH 1
	regmatch_t match[MAXMATCH];
	char temp[256];

/*
 * No sense in recompiling the patterns and freeing every time.
 */	
	if (first_time) 
	{
/*
 * Present, frequency must be at the at the beginning.
 * Others can be anywhere in the comment.
 */
		/* incomplete */
	  e = regcomp (&freq_re, "^[0-9A-O][0-9][0-9]\\.[0-9][0-9][0-9 ]MHz( [TCDtcd][0-9][0-9][0-9]| Toff)?( [+-][0-9][0-9][0-9])?", REG_EXTENDED);
	  if (e) {
	    regerror (e, &freq_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&dao_re, "!([A-Z][0-9 ][0-9 ]|[a-z][!-} ][!-} ])!", REG_EXTENDED);
	  if (e) {
	    regerror (e, &dao_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  e = regcomp (&alt_re, "/A=[0-9][0-9][0-9][0-9][0-9][0-9]", REG_EXTENDED);
	  if (e) {
	    regerror (e, &alt_re, emsg, sizeof(emsg));
	    dw_printf("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  first_time = 0;
	}

	if (clen >= 0) {
	  assert (clen < sizeof(g_comment));
	  memcpy (g_comment, pstart, (size_t)clen);
	  g_comment[clen] = '\0';
	}
	else {
	  strcpy (g_comment, pstart);
	}
	//dw_printf("\nInitial comment='%s'\n", g_comment);


/*
 * Frequency.
 * Just pull it out from comment. 
 * No futher interpretation at this time.
 */

	if (regexec (&freq_re, g_comment, MAXMATCH, match, 0) == 0) 
	{

          //dw_printf("start=%d, end=%d\n", (int)(match[0].rm_so), (int)(match[0].rm_eo));

	  strcpy (temp, g_comment + match[0].rm_eo);

	  g_comment[match[0].rm_eo] = '\0';
          strcpy (g_freq, g_comment + match[0].rm_so);

	  strcpy (g_comment + match[0].rm_so, temp);
	}

/*
 * Latitude and Longitude in the form DD MM.HH has a resolution of about 60 feet.
 * The !DAO! option allows another digit or [almost two] for greater resolution.
 */

	if (regexec (&dao_re, g_comment, MAXMATCH, match, 0) == 0) 
	{

	  int d = g_comment[match[0].rm_so+1];
	  int a = g_comment[match[0].rm_so+2];
	  int o = g_comment[match[0].rm_so+3];

          //dw_printf("start=%d, end=%d\n", (int)(match[0].rm_so), (int)(match[0].rm_eo));

	  if (isupper(d)) 
	  {
/*
 * This adds one extra digit to each.  Dao adds extra digit like:
 *
 *		Lat:	 DD MM.HHa
 *		Lon:	DDD HH.HHo
 */
 	    if (isdigit(a)) {
	      g_lat += (a - '0') / 60000.0 * sign(g_lat);
	    }
 	    if (isdigit(o)) {
	      g_lon += (o - '0') / 60000.0 * sign(g_lon);
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
 * The original character range '!' to '}' is first converted
 * to an integer in range of 0 to 90.  It is multiplied by 1.1
 * to stretch the numeric range to be 0 to 99.
 */
 	    if (a >= '!' && a <= '}') {
	      g_lat += (a - '!') * 1.1 / 600000.0 * sign(g_lat);
	    }
 	    if (o >= '!' && o <= '}') {
	      g_lon += (o - '!') * 1.1 / 600000.0 * sign(g_lon);
	    }
	  }

	  strcpy (temp, g_comment + match[0].rm_eo);
	  strcpy (g_comment + match[0].rm_so, temp);
	}

/*
 * Altitude in feet.  /A=123456
 */

	if (regexec (&alt_re, g_comment, MAXMATCH, match, 0) == 0) 
	{

          //dw_printf("start=%d, end=%d\n", (int)(match[0].rm_so), (int)(match[0].rm_eo));

	  strcpy (temp, g_comment + match[0].rm_eo);

	  g_comment[match[0].rm_eo] = '\0';
          g_altitude = atoi(g_comment + match[0].rm_so + 3);

	  strcpy (g_comment + match[0].rm_so, temp);
	}

	//dw_printf("Final comment='%s'\n", g_comment);

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
 *		gcc -o decode_aprs -DTEST decode_aprs.c ax25_pad.c	
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
 *
 *------------------------------------------------------------------*/

#if TEST


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
	      decode_aprs (pp);
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

#endif /* TEST */

/* end decode_aprs.c */
