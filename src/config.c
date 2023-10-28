//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2021  John Langner, WB2OSZ
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

#define CONFIG_C 1		// influences behavior of aprs_tt.h


// #define DEBUG 1

/*------------------------------------------------------------------
 *
 * Module:      config.c
 *
 * Purpose:   	Read configuration information from a file.
 *		
 * Description:	This started out as a simple little application with a few
 *		command line options.  Due to creeping featurism, it's now
 *		time to add a configuration file to specify options.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#if ENABLE_GPSD
#include <gps.h>		/* for DEFAULT_GPSD_PORT  (2947) */
#endif


#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "digipeater.h"
#include "cdigipeater.h"
#include "config.h"
#include "aprs_tt.h"
#include "igate.h"
#include "latlong.h"
#include "symbols.h"
#include "xmit.h"
#include "tt_text.h"
#include "ax25_link.h"

#if USE_CM108		// Current Linux or Windows only
#include "cm108.h"
#endif

// geotranz

#include "utm.h"
#include "mgrs.h"
#include "usng.h"
#include "error_string.h"

#define D2R(d) ((d) * M_PI / 180.)
#define R2D(r) ((r) * 180. / M_PI)



/* 
 * Conversions from various units to meters.
 * There is some disagreement about the exact values for some of these. 
 * Close enough for our purposes.
 * Parsec, light year, and angstrom are probably not useful.
 */

static const struct units_s {
	char *name;
	float meters;
} units[] = {
	{	"barleycorn",	0.008466667	},	
	{	"inch",		0.0254		},
	{	"in",		0.0254		},
	{	"hand",		0.1016		},	
	{	"shaku",	0.3030		},	
	{	"foot",		0.304801	},	
	{	"ft",		0.304801	},	
	{	"cubit",	0.4572		},	
	{	"megalithicyard", 0.8296	},	
	{	"my",		0.8296		},	
	{	"yard",		0.914402	},
	{	"yd",		0.914402	},
	{	"m",		1.		},	
	{	"meter",	1.		},	
	{	"metre",	1.		},	
	{	"ell",		1.143		},	
	{	"ken",		1.818		},	
	{	"hiro",		1.818		},	
	{	"fathom",	1.8288		},	
	{	"fath",		1.8288		},	
	{	"toise",	1.949		},
	{	"jo",		3.030		},
	{	"twain",	3.6576074	},	
	{	"rod",		5.0292		},	
	{	"rd",		5.0292		},	
	{	"perch",	5.0292		},	
	{	"pole",		5.0292		},	
	{	"rope",		6.096		},	
	{	"dekameter",	10.		},	
	{	"dekametre",	10.		},	
	{	"dam",		10.		},	
	{	"chain",	20.1168		},
	{	"ch",		20.1168		},
	{	"actus",	35.47872	},	
	{	"arpent",	58.471		},	
	{	"hectometer",	100.		},	
	{	"hectometre",	100.		},	
	{	"hm",		100.		},	
	{	"cho",		109.1		},	
	{	"furlong",	201.168		},
	{	"fur",		201.168		},
	{	"kilometer",	1000.		},	
	{	"kilometre",	1000.		},	
	{	"km",		1000.		},	
	{	"mile",		1609.344	},	
	{	"mi",		1609.344	},	
	{	"ri",		3927.		},	
	{	"league",	4828.032	},	
	{	"lea",		4828.032	} };

#define NUM_UNITS ((int)((sizeof(units) / sizeof(struct units_s))))

static int beacon_options(char *cmd, struct beacon_s *b, int line, struct audio_s *p_audio_config);

/* Do we have a string of all digits? */

static int alldigits(char *p)
{
	if (p == NULL) return (0);
	if (strlen(p) == 0) return (0);
	while (*p != '\0') {
	  if ( ! isdigit(*p)) return (0);
	  p++;
	}
	return (1);
}

/* Do we have a string of all letters or + or -  ? */

static int alllettersorpm(char *p)
{
	if (p == NULL) return (0);
	if (strlen(p) == 0) return (0);
	while (*p != '\0') {
	  if ( ! isalpha(*p) && *p != '+' && *p != '-') return (0);
	  p++;
	}
	return (1);
}

/*------------------------------------------------------------------
 *
 * Name:        parse_ll
 *
 * Purpose:     Parse latitude or longitude from configuration file.
 *
 * Inputs:      str	- String like [-]deg[^min][hemisphere]
 *
 *		which	- LAT or LON for error checking and message.
 *
 *		line	- Line number for use in error message. 
 *
 * Returns:     Coordinate in signed degrees.
 *
 *----------------------------------------------------------------*/

/* Acceptable symbols to separate degrees & minutes. */
/* Degree symbol is not in ASCII so documentation says to use "^" instead. */
/* Some wise guy will try to use degree symbol. */
/* UTF-8 is more difficult because it is a two byte sequence, c2 b0. */

#define DEG1 '^'
#define DEG2 0xb0	/* ISO Latin1 */
#define DEG3 0xf8	/* Microsoft code page 437 */


enum parse_ll_which_e { LAT, LON };

static double parse_ll (char *str, enum parse_ll_which_e which, int line)
{
	char stemp[40];
	int sign;
	double degrees, minutes;
	char *endptr;
	char hemi;
	int limit;
	unsigned char sep;

/*
 * Remove any negative sign.
 */
	strlcpy (stemp, str, sizeof(stemp));
	sign = +1;
	if (stemp[0] == '-') {
	  sign = -1;
	  stemp[0] = ' ';
	}
/*
 * Process any hemisphere on the end.
 */
	if (strlen(stemp) >= 2) {
	  endptr = stemp + strlen(stemp) - 1;
	  if (isalpha(*endptr)) {

	    hemi = *endptr;
	    *endptr = '\0';
	    if (islower(hemi)) {
	      hemi = toupper(hemi);
	    }

	    if (hemi == 'W' || hemi == 'S') {
	      sign = -sign;
	    }

	    if (which == LAT) {
	      if (hemi != 'N' && hemi != 'S') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Latitude hemisphere in \"%s\" is not N or S.\n", line, str);
	      }
	    }
	    else {
	      if (hemi != 'E' && hemi != 'W') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Longitude hemisphere in \"%s\" is not E or W.\n", line, str);
	      }
	    }
	  }
	}

/*
 * Parse the degrees part.
 */
	degrees = strtod (stemp, &endptr);

/*
 * Is there a minutes part?
 */
	sep = *endptr;
	if (sep != '\0') {

	  if (sep == DEG1 || sep == DEG2 || sep == DEG3) {
	 
	    minutes = strtod (endptr+1, &endptr);
	    if (*endptr != '\0') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Unexpected character '%c' in location \"%s\"\n", line, sep, str);
	    }
	    if (minutes >= 60.0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Number of minutes in \"%s\" is >= 60.\n", line, str);
	    }
	    degrees += minutes / 60.0;
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Line %d: Unexpected character '%c' in location \"%s\"\n", line, sep, str);
	  }
	}

	degrees = degrees * sign;

	limit = which == LAT ? 90 : 180;
	if (degrees < -limit || degrees > limit) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Line %d: Number of degrees in \"%s\" is out of range for %s\n", line, str,
		which == LAT ? "latitude" : "longitude");
	}
	//dw_printf ("%s = %f\n", str, degrees);
	return (degrees);
}



/*------------------------------------------------------------------
 *
 * Name:        parse_utm_zone
 *
 * Purpose:     Parse UTM zone from configuration file.
 *
 * Inputs:      szone	- String like [-]number[letter]
 *
 * Outputs:	latband	- Latitude band if specified, otherwise space or -.
 *
 *		hemi	- Hemisphere, always one of 'N' or 'S'.
 *
 * Returns:	Zone as number.  
 *		Type is long because Convert_UTM_To_Geodetic expects that.
 *
 * Errors:	Prints message and return 0.
 *
 * Description:	
 *		It seems there are multiple conventions for specifying the UTM hemisphere.
 *		
 *		  - MGRS latitude band.  North if missing or >= 'N'.
 *		  - Negative zone for south.
 *		  - Separate North or South.
 *		
 *		I'm using the first alternative.
 *		GEOTRANS uses the third.
 *		We will also recognize the second one but I'm not sure if I want to document it.
 *
 *----------------------------------------------------------------*/

long parse_utm_zone (char *szone, char *latband, char *hemi)
{
	long lzone;
	char *zlet;


	*latband = ' ';
	*hemi = 'N';	/* default */

        lzone = strtol(szone, &zlet, 10);

        if (*zlet == '\0') {
	  /* Number is not followed by letter something else.  */
	  /* Allow negative number to mean south. */

	  if (lzone < 0) {
	    *latband = '-';
	    *hemi = 'S';
	    lzone = (- lzone);
	  }
	}
	else {
	  if (islower (*zlet)) {
	    *zlet = toupper(*zlet);
	  }
	  *latband = *zlet;
	  if (strchr ("CDEFGHJKLMNPQRSTUVWX", *zlet) != NULL) {
	    if (*zlet < 'N') {
	      *hemi = 'S';
	    }
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
            dw_printf ("Latitudinal band in \"%s\" must be one of CDEFGHJKLMNPQRSTUVWX.\n", szone);
 	    *hemi = '?';
	  }
        }

        if (lzone < 1 || lzone > 60) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("UTM Zone number %ld must be in range of 1 to 60.\n", lzone);
        
        }

	return (lzone);

} /* end parse_utm_zone */




#if 0
main ()
{

	parse_ll ("12.5", LAT);
	parse_ll ("12.5N", LAT);
	parse_ll ("12.5E", LAT);	// error

	parse_ll ("-12.5", LAT);
	parse_ll ("12.5S", LAT);
	parse_ll ("12.5W", LAT);	// error

	parse_ll ("12.5", LON);
	parse_ll ("12.5E", LON);
	parse_ll ("12.5N", LON);	// error

	parse_ll ("-12.5", LON);
	parse_ll ("12.5W", LON);
	parse_ll ("12.5S", LON);	// error

	parse_ll ("12^30", LAT);
	parse_ll ("12\xb030", LAT);			// ISO Latin-1 degree symbol

	parse_ll ("91", LAT);		// out of range
	parse_ll ("91", LON);
	parse_ll ("181", LON);		// out of range

	parse_ll ("12&5", LAT);		// bad character
}
#endif


/*------------------------------------------------------------------
 *
 * Name:        parse_interval
 *
 * Purpose:     Parse time interval from configuration file.
 *
 * Inputs:      str	- String like 10 or 9:30
 *
 *		line	- Line number for use in error message. 
 *
 * Returns:     Number of seconds.
 *
 * Description:	This is used by the BEACON configuration items
 *		for initial delay or time between beacons.
 *
 *		The format is either minutes or minutes:seconds.
 *
 *----------------------------------------------------------------*/


static int parse_interval (char *str, int line)
{
	char *p;
	int sec;
	int nc = 0;
	int bad = 0;

	for (p = str; *p != '\0'; p++) {
	  if (*p == ':') nc++;
	  else if ( ! isdigit(*p)) bad++;
	}
	if (bad > 0 || nc > 1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Config file, line %d: Time interval must be of the form minutes or minutes:seconds.\n", line);
	}

	p = strchr (str, ':');

	if (p != NULL) {
	  sec = atoi(str) * 60 + atoi(p+1);
	}
	else {
	  sec = atoi(str) * 60;
	}

	return (sec);

} /* end parse_interval */



/*------------------------------------------------------------------
 *
 * Name:        check_via_path
 *
 * Purpose:     Check for valid path in beacons, IGate, and APRStt configuration.
 *
 * Inputs:      via_path	- Zero or more comma separated stations.
 *
 * Returns:	Maximum number of digipeater hops or -1 for error.
 *
 * Description:	Beacons and IGate can use via paths such as:
 *
 *			WIDE1-1,MA3-3
 *			N2GH,RARA-7
 *
 * 		Each part could be a specific station, an alias, or a path
 *		from the "New n-N Paradigm."
 *		In the first example above, the maximum number of digipeater
 *		hops would be 4.  In the second example, 2.
 *
 *----------------------------------------------------------------*/

// Put something like this in the config file as a quick test.
// Not worth adding to "make check" regression tests.
//
// 	IBEACON via=
//	IBEACON via=W2UB
//	IBEACON via=W2UB-7
//	IBEACON via=WIDE1-1,WIDE2-2,WIDE3-3
//	IBEACON via=Lower
//	IBEACON via=T00LONG
//	IBEACON via=W2UB-16
//	IBEACON via=D1,D2,D3,D4,D5,D6,D7,D8
//	IBEACON via=D1,D2,D3,D4,D5,D6,D7,D8,D9
//
// Define below and visually check results.

//#define DEBUG8 1


static int check_via_path (char *via_path)
{
	char stemp[AX25_MAX_REPEATERS * (AX25_MAX_ADDR_LEN + 1)];
	int num_digi = 0;
	int max_digi_hops = 0;
	char *r;
	char *a;

#if DEBUG8
	text_color_set(DW_COLOR_DEBUG);
        dw_printf ("check_via_path %s\n", via_path);
#endif
	if (strlen(via_path) == 0) {
	  return (0);
	}

	strlcpy (stemp, via_path, sizeof(stemp));

	r = stemp;
	while (( a = strsep(&r,",")) != NULL) {
	  int strict = 2;
	  int ok;
	  char addr[AX25_MAX_ADDR_LEN];
	  int ssid;
	  int heard;

	  num_digi++;
	  ok = ax25_parse_addr (AX25_REPEATER_1 - 1 + num_digi, a, strict, addr, &ssid, &heard);
	  if ( ! ok) {
#if DEBUG8
	    text_color_set(DW_COLOR_DEBUG);
            dw_printf ("check_via_path bad address\n");
#endif
	    return (-1);
	  }

	  /* Based on assumption that a callsign can't end with a digit. */
	  /* For something of the form xxx9-9, we take the ssid as max hop count. */

	  if (ssid > 0 && strlen(addr) >= 2 && isdigit(addr[strlen(addr)-1])) {
	    max_digi_hops += ssid;
	  }
	  else {
	    max_digi_hops++;
	  }
	}

	if (num_digi > AX25_MAX_REPEATERS) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("Maximum of 8 digipeaters has been exceeded.\n");
	  return (-1);
        }

#if DEBUG8
	text_color_set(DW_COLOR_DEBUG);
        dw_printf ("check_via_path %d addresses, %d max digi hops\n", num_digi, max_digi_hops);
#endif

	return (max_digi_hops);

} /* end check_via_path */



/*-------------------------------------------------------------------
 *
 * Name:        split
 *
 * Purpose:     Separate a line into command and parameters.
 *
 * Inputs:	string		- Complete command line to start process.
 *				  NULL for subsequent calls.
 *
 *		rest_of_line	- Caller wants remainder of line, not just
 *				  the next parameter.
 *
 * Returns:	Pointer to next part with any quoting removed.
 *
 * Description:	the configuration file started out very simple and strtok
 *		was used to split up the lines.  As more complicated options
 *		were added, there were several different situations where
 *		parameter values might contain spaces.  These were handled
 *		inconsistently in different places.  In version 1.3, we now
 *		treat them consistently in one place.
 *
 *
 *--------------------------------------------------------------------*/

#define MAXCMDLEN 1200


static char *split (char *string, int rest_of_line)
{
	static char cmd[MAXCMDLEN];
	static char token[MAXCMDLEN];
	static char shutup[] = " ";	// Shut up static analysis which gets upset
					// over the case where this could be called with
					// string NULL and c was not yet initialized.
	static char *c = shutup;	// Current position in command line.
	char *s, *t;
	int in_quotes;

/*
 * If string is provided, make a copy.
 * Drop any CRLF at the end.
 * Change any tabs to spaces so we don't have to check for it later.
 */
	if (string != NULL) {

	  // dw_printf("split in: '%s'\n", string);

	  c = cmd;
	  for (s = string; *s != '\0'; s++) {
	    if (*s == '\t') {
	      *c++ = ' ';
	    }
	    else if (*s == '\r' || *s == '\n') {
	      ;
	    }
	    else {
	      *c++ = *s;
	    }
	  }
	  *c = '\0';
	  c = cmd;
	}

/*
 * Get next part, separated by whitespace, keeping spaces within quotes.
 * Quotation marks inside need to be doubled.
 */

	while (*c == ' ') {
	  c++;
	};

	t = token;
	in_quotes = 0;
	for ( ; *c != '\0'; c++) {

	  if (*c == '"') {
	    if (in_quotes) {
	      if (c[1] == '"') {
	        *t++ = *c++;
	      }
	      else {
	        in_quotes = 0;
	      }
	    }
	    else {
	      in_quotes = 1;
	    }
	  }
	  else if (*c == ' ') {
	    if (in_quotes || rest_of_line) {
	      *t++ = *c;
	    }
	    else {
	      break;
	    }
	  }
	  else {
	    *t++ = *c;
	  }
	}
	*t = '\0';

	// dw_printf("split out: '%s'\n", token);

	t = token;
	if (*t == '\0') {
	  return (NULL);
	}

	return (t);

} /* end split */



/*-------------------------------------------------------------------
 *
 * Name:        config_init
 *
 * Purpose:     Read configuration file when application starts up.
 *
 * Inputs:	fname		- Name of configuration file.
 *
 * Outputs:	p_audio_config		- Radio channel parameters stored here.
 *
 *		p_digi_config	- APRS Digipeater configuration stored here.
 *
 *		p_cdigi_config	- Connected Digipeater configuration stored here.
 *
 *		p_tt_config	- APRStt stuff.
 *
 *		p_igate_config	- Internet Gateway.
 *	
 *		p_misc_config	- Everything else.  This wasn't thought out well.
 *
 * Description:	Apply default values for various parameters then read the 
 *		the configuration file which can override those values.
 *
 * Errors:	For invalid input, display line number and message on stdout (not stderr).
 *		In many cases this will result in keeping the default rather than aborting.
 *
 * Bugs:	Very simple-minded parsing.
 *		Not much error checking.  (e.g. atoi() will return 0 for invalid string.)
 *		Not very forgiving about sloppy input.
 *
 *--------------------------------------------------------------------*/

static void rtfm()
{
	text_color_set(DW_COLOR_ERROR);
	dw_printf ("See online documentation:\n");
	dw_printf ("    stable release:    https://github.com/wb2osz/direwolf/tree/master/doc\n");
	dw_printf ("    development version:    https://github.com/wb2osz/direwolf/tree/dev/doc\n");
	dw_printf ("    additional topics:    https://github.com/wb2osz/direwolf-doc\n");
}

void config_init (char *fname, struct audio_s *p_audio_config, 
			struct digi_config_s *p_digi_config,
			struct cdigi_config_s *p_cdigi_config,
			struct tt_config_s *p_tt_config,
			struct igate_config_s *p_igate_config,
			struct misc_config_s *p_misc_config)
{
	FILE *fp;
	char filepath[128];
	char stuff[MAXCMDLEN];
	int line;
	int channel;
	int adevice;
	int m;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("config_init ( %s )\n", fname);
#endif

/* 
 * First apply defaults.
 */

	memset (p_audio_config, 0, sizeof(struct audio_s));

	p_audio_config->igate_vchannel = -1;		// none.

	/* First audio device is always available with defaults. */
	/* Others must be explicitly defined before use. */

	for (adevice=0; adevice<MAX_ADEVS; adevice++) {

	  strlcpy (p_audio_config->adev[adevice].adevice_in, DEFAULT_ADEVICE, sizeof(p_audio_config->adev[adevice].adevice_in));
	  strlcpy (p_audio_config->adev[adevice].adevice_out, DEFAULT_ADEVICE, sizeof(p_audio_config->adev[adevice].adevice_out));

	  p_audio_config->adev[adevice].defined = 0;
	  p_audio_config->adev[adevice].num_channels = DEFAULT_NUM_CHANNELS;		/* -2 stereo */
	  p_audio_config->adev[adevice].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;	/* -r option */
	  p_audio_config->adev[adevice].bits_per_sample = DEFAULT_BITS_PER_SAMPLE;	/* -8 option for 8 instead of 16 bits */
	}

	p_audio_config->adev[0].defined = 1;

	for (channel=0; channel<MAX_CHANS; channel++) {
	  int ot, it;

	  p_audio_config->chan_medium[channel] = MEDIUM_NONE;	/* One or both channels will be */
								/* set to radio when corresponding */
								/* audio device is defined. */
	  p_audio_config->achan[channel].modem_type = MODEM_AFSK;			
	  p_audio_config->achan[channel].v26_alternative = V26_UNSPECIFIED;
	  p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;		/* -m option */
	  p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;		/* -s option */
	  p_audio_config->achan[channel].baud = DEFAULT_BAUD;			/* -b option */

	  /* None.  Will set default later based on other factors. */
	  strlcpy (p_audio_config->achan[channel].profiles, "", sizeof(p_audio_config->achan[channel].profiles));	

	  p_audio_config->achan[channel].num_freq = 1;				
	  p_audio_config->achan[channel].offset = 0;

	  p_audio_config->achan[channel].layer2_xmit = LAYER2_AX25;
	  p_audio_config->achan[channel].il2p_max_fec = 1;
	  p_audio_config->achan[channel].il2p_invert_polarity = 0;

	  p_audio_config->achan[channel].fix_bits = DEFAULT_FIX_BITS;
	  p_audio_config->achan[channel].sanity_test = SANITY_APRS;
	  p_audio_config->achan[channel].passall = 0;

	  for (ot = 0; ot < NUM_OCTYPES; ot++) {
	    p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_NONE;
	    strlcpy (p_audio_config->achan[channel].octrl[ot].ptt_device, "", sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));
	    p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_NONE;
	    p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_NONE;
	    p_audio_config->achan[channel].octrl[ot].out_gpio_num = 0;
	    p_audio_config->achan[channel].octrl[ot].ptt_lpt_bit = 0;
	    p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
	    p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 0;
	  }

	  for (it = 0; it < NUM_ICTYPES; it++) {
	    p_audio_config->achan[channel].ictrl[it].method = PTT_METHOD_NONE;
	    p_audio_config->achan[channel].ictrl[it].in_gpio_num = 0;
	    p_audio_config->achan[channel].ictrl[it].invert = 0;
	  }

	  p_audio_config->achan[channel].dwait = DEFAULT_DWAIT;				
	  p_audio_config->achan[channel].slottime = DEFAULT_SLOTTIME;				
	  p_audio_config->achan[channel].persist = DEFAULT_PERSIST;				
	  p_audio_config->achan[channel].txdelay = DEFAULT_TXDELAY;				
	  p_audio_config->achan[channel].txtail = DEFAULT_TXTAIL;				
	  p_audio_config->achan[channel].fulldup = DEFAULT_FULLDUP;
	}

	p_audio_config->fx25_auto_enable = AX25_N2_RETRY_DEFAULT / 2;

	/* First channel should always be valid. */
	/* If there is no ADEVICE, it uses default device in mono. */

	p_audio_config->chan_medium[0] = MEDIUM_RADIO;

	memset (p_digi_config, 0, sizeof(struct digi_config_s));	// APRS digipeater
	p_digi_config->dedupe_time = DEFAULT_DEDUPE;
	memset (p_cdigi_config, 0, sizeof(struct cdigi_config_s));	// Connected mode digipeater

	memset (p_tt_config, 0, sizeof(struct tt_config_s));	
	p_tt_config->gateway_enabled = 0;
	p_tt_config->ttloc_size = 2;	/* Start with at least 2.  */
					/* When full, it will be increased by 50 %. */
	p_tt_config->ttloc_ptr = malloc (sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	p_tt_config->ttloc_len = 0;

	/* Retention time and decay algorithm from 13 Feb 13 version of */
	/* http://www.aprs.org/aprstt/aprstt-coding24.txt */
	/* Reduced by transmit count by one.  An 8 minute delay in between transmissions seems awful long. */

	p_tt_config->retain_time = 80 * 60;
	p_tt_config->num_xmits = 6;
	assert (p_tt_config->num_xmits <= TT_MAX_XMITS);
	p_tt_config->xmit_delay[0] = 3;		/* Before initial transmission. */
	p_tt_config->xmit_delay[1] = 16;
	p_tt_config->xmit_delay[2] = 32;
	p_tt_config->xmit_delay[3] = 64;
	p_tt_config->xmit_delay[4] = 2 * 60;
	p_tt_config->xmit_delay[5] = 4 * 60;
	p_tt_config->xmit_delay[6] = 8 * 60;		// not currently used.

	strlcpy (p_tt_config->status[0], "", 		sizeof(p_tt_config->status[0]));
	strlcpy (p_tt_config->status[1], "/off duty", 	sizeof(p_tt_config->status[1]));
	strlcpy (p_tt_config->status[2], "/enroute", 	sizeof(p_tt_config->status[2]));
	strlcpy (p_tt_config->status[3], "/in service", sizeof(p_tt_config->status[3]));
	strlcpy (p_tt_config->status[4], "/returning", 	sizeof(p_tt_config->status[4]));
	strlcpy (p_tt_config->status[5], "/committed", 	sizeof(p_tt_config->status[5]));
	strlcpy (p_tt_config->status[6], "/special", 	sizeof(p_tt_config->status[6]));
	strlcpy (p_tt_config->status[7], "/priority", 	sizeof(p_tt_config->status[7]));
	strlcpy (p_tt_config->status[8], "/emergency", 	sizeof(p_tt_config->status[8]));
	strlcpy (p_tt_config->status[9], "/custom 1", 	sizeof(p_tt_config->status[9]));

	for (m = 0; m < TT_ERROR_MAXP1; m++) {
	  strlcpy (p_tt_config->response[m].method, "MORSE",	sizeof(p_tt_config->response[m].method));
	  strlcpy (p_tt_config->response[m].mtext, "?",		sizeof(p_tt_config->response[m].mtext));
	}
	strlcpy (p_tt_config->response[TT_ERROR_OK].mtext, "R", sizeof(p_tt_config->response[TT_ERROR_OK].mtext));


	memset (p_misc_config, 0, sizeof(struct misc_config_s));
	p_misc_config->agwpe_port = DEFAULT_AGWPE_PORT;

	for (int i=0; i<MAX_KISS_TCP_PORTS; i++) {
	  p_misc_config->kiss_port[i] = 0;	// entry not used.
	  p_misc_config->kiss_chan[i] = -1;
	}
	p_misc_config->kiss_port[0] = DEFAULT_KISS_PORT;
	p_misc_config->kiss_chan[0] = -1;	// all channels.

	p_misc_config->enable_kiss_pt = 0;				/* -p option */
	p_misc_config->kiss_copy = 0;

	p_misc_config->dns_sd_enabled = 1;

	/* Defaults from http://info.aprs.net/index.php?title=SmartBeaconing */

	p_misc_config->sb_configured = 0;	/* TRUE if SmartBeaconing is configured. */
	p_misc_config->sb_fast_speed = 60;	/* MPH */
	p_misc_config->sb_fast_rate = 180;	/* seconds */
	p_misc_config->sb_slow_speed = 5;	/* MPH */
	p_misc_config->sb_slow_rate = 1800;	/* seconds */
	p_misc_config->sb_turn_time = 15;	/* seconds */
	p_misc_config->sb_turn_angle = 30;	/* degrees */
	p_misc_config->sb_turn_slope = 255;	/* degrees * MPH */

	memset (p_igate_config, 0, sizeof(struct igate_config_s));
	p_igate_config->t2_server_port = DEFAULT_IGATE_PORT;
	p_igate_config->tx_chan = -1;			/* IS->RF not enabled */
	p_igate_config->tx_limit_1 = IGATE_TX_LIMIT_1_DEFAULT;
	p_igate_config->tx_limit_5 = IGATE_TX_LIMIT_5_DEFAULT;
	p_igate_config->igmsp = 1;
	p_igate_config->rx2ig_dedupe_time = IGATE_RX2IG_DEDUPE_TIME;


	/* People find this confusing. */
	/* Ideally we'd like to figure out if com0com is installed */
	/* and automatically enable this.  */
	
	strlcpy (p_misc_config->kiss_serial_port, "", sizeof(p_misc_config->kiss_serial_port));
	p_misc_config->kiss_serial_speed = 0;
	p_misc_config->kiss_serial_poll = 0;

	strlcpy (p_misc_config->gpsnmea_port, "", sizeof(p_misc_config->gpsnmea_port));
	strlcpy (p_misc_config->waypoint_serial_port, "", sizeof(p_misc_config->waypoint_serial_port));

	p_misc_config->log_daily_names = 0;
	strlcpy (p_misc_config->log_path, "", sizeof(p_misc_config->log_path));

	/* connected mode. */

	p_misc_config->frack = AX25_T1V_FRACK_DEFAULT;		/* Number of seconds to wait for ack to transmission. */

	p_misc_config->retry = AX25_N2_RETRY_DEFAULT;		/* Number of times to retry before giving up. */

	p_misc_config->paclen = AX25_N1_PACLEN_DEFAULT;		/* Max number of bytes in information part of frame. */

	p_misc_config->maxframe_basic = AX25_K_MAXFRAME_BASIC_DEFAULT;	/* Max frames to send before ACK.  mod 8 "Window" size. */

	p_misc_config->maxframe_extended = AX25_K_MAXFRAME_EXTENDED_DEFAULT;	/* Max frames to send before ACK.  mod 128 "Window" size. */

	p_misc_config->maxv22 = AX25_N2_RETRY_DEFAULT / 3;	/* Max SABME before falling back to SABM. */
	p_misc_config->v20_addrs = NULL;			/* Go directly to v2.0 for stations listed. */
	p_misc_config->v20_count = 0;
	p_misc_config->noxid_addrs = NULL;			/* Don't send XID to these stations. */
	p_misc_config->noxid_count = 0;

/* 
 * Try to extract options from a file.
 * 
 * Windows:  File must be in current working directory.
 *
 * Linux: Search current directory then home directory.
 *
 * Future possibility - Could also search home directory
 * for Windows by combinting two variables:
 *	HOMEDRIVE=C:
 *	HOMEPATH=\Users\John
 *
 * It's not clear if this always points to same location:
 *	USERPROFILE=C:\Users\John
 */


	channel = 0;
	adevice = 0;

// TODO: Would be better to have a search list and loop thru it.

        strlcpy(filepath, fname, sizeof(filepath));

        fp = fopen (filepath, "r");
	
#ifndef __WIN32__
	if (fp == NULL && strcmp(fname, "direwolf.conf") == 0) {
	/* Failed to open the default location.  Try home dir. */
	  char *p;


          strlcpy (filepath, "", sizeof(filepath));

	  p = getenv("HOME");
	  if (p != NULL) {
	    strlcpy (filepath, p, sizeof(filepath));
	    strlcat (filepath, "/direwolf.conf", sizeof(filepath));
	    fp = fopen (filepath, "r");
	  } 
	}
#endif
	if (fp == NULL)	{
	  // TODO: not exactly right for all situations.
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Could not open config file %s\n", filepath);
	  dw_printf ("Try using -c command line option for alternate location.\n");
	  rtfm();
	  exit(EXIT_FAILURE);
	}
	
	dw_printf ("\nReading config file %s\n", filepath);

	line = 0;
	while (fgets(stuff, sizeof(stuff), fp) != NULL) {
	  char *t;

	  line++;

	  t = split(stuff,0);

	  if (t == NULL) {
	    continue;
	  }

	  if (*t == '#' || *t == '*') {
	    continue;
	  }



/*
 * ==================== Audio device parameters ==================== 
 */

/*
 * ADEVICE[n] 		- Name of input sound device, and optionally output, if different.   
 *
 *			ADEVICE    plughw:1,0			-- same for in and out.
 *			ADEVICE	   plughw:2,0  plughw:3,0	-- different in/out for a channel or channel pair.
 *			ADEVICE1   udp:7355  default		-- from Software defined radio (SDR) via UDP.
 *	
 */

	  /* Note that ALSA name can contain comma such as hw:1,0 */

	  if (strncasecmp(t, "ADEVICE", 7) == 0) {
	    /* "ADEVICE" is equivalent to "ADEVICE0". */
	    adevice = 0;
	    if (strlen(t) >= 8) {
	      adevice = atoi(t+7);
	    }

	    if (adevice < 0 || adevice >= MAX_ADEVS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Device number %d out of range for ADEVICE command on line %d.\n", adevice, line);
	      dw_printf ("If you really need more than %d audio devices, increase MAX_ADEVS and recompile.\n", MAX_ADEVS);
	      adevice = 0;
	      continue;
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing name of audio device for ADEVICE command on line %d.\n", line);
	      rtfm();
	      exit(EXIT_FAILURE);
	    }

	    p_audio_config->adev[adevice].defined = 1;
	
	    /* First channel of device is valid. */
	    p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice)] = MEDIUM_RADIO;

	    strlcpy (p_audio_config->adev[adevice].adevice_in, t, sizeof(p_audio_config->adev[adevice].adevice_in));
	    strlcpy (p_audio_config->adev[adevice].adevice_out, t, sizeof(p_audio_config->adev[adevice].adevice_out));

	    t = split(NULL,0);
	    if (t != NULL) {
	      strlcpy (p_audio_config->adev[adevice].adevice_out, t, sizeof(p_audio_config->adev[adevice].adevice_out));
	    }
	  }


/*
 * PAIDEVICE[n]  input-device
 * PAODEVICE[n]  output-device
 *
 *			This was submitted by KK5VD for the Mac OS X version.  (__APPLE__)
 *
 *			It looks like device names can contain spaces making it a little
 *			more difficult to put two names on the same line unless we come up with
 *			some other delimiter between them or a quoting scheme to handle
 *			embedded spaces in a name.
 *
 *			It concerns me that we could have one defined without the other
 *			if we don't put in more error checking later.
 *
 *	version 1.3 dev snapshot C:
 *
 *		We now have a general quoting scheme so the original ADEVICE can handle this.
 *		These options will probably be removed before general 1.3 release.
 */

	  else if (strcasecmp(t, "PAIDEVICE") == 0) {
		  adevice = 0;
		  if (isdigit(t[9])) {
			  adevice = t[9] - '0';
		  }

		  if (adevice < 0 || adevice >= MAX_ADEVS) {
			  text_color_set(DW_COLOR_ERROR);
			  dw_printf ("Config file: Device number %d out of range for PADEVICE command on line %d.\n", adevice, line);
			  adevice = 0;
			  continue;
		  }

		  t = split(NULL,1);
		  if (t == NULL) {
			  text_color_set(DW_COLOR_ERROR);
			  dw_printf ("Config file: Missing name of audio device for PADEVICE command on line %d.\n", line);
			  continue;
		  }

		  p_audio_config->adev[adevice].defined = 1;

		  /* First channel of device is valid. */
		  p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice)] = MEDIUM_RADIO;

		  strlcpy (p_audio_config->adev[adevice].adevice_in, t, sizeof(p_audio_config->adev[adevice].adevice_in));
	  }
	  else if (strcasecmp(t, "PAODEVICE") == 0) {
		  adevice = 0;
		  if (isdigit(t[9])) {
			  adevice = t[9] - '0';
		  }

		  if (adevice < 0 || adevice >= MAX_ADEVS) {
			  text_color_set(DW_COLOR_ERROR);
			  dw_printf ("Config file: Device number %d out of range for PADEVICE command on line %d.\n", adevice, line);
			  adevice = 0;
			  continue;
		  }

		  t = split(NULL,1);
		  if (t == NULL) {
			  text_color_set(DW_COLOR_ERROR);
			  dw_printf ("Config file: Missing name of audio device for PADEVICE command on line %d.\n", line);
			  continue;
		  }

		  p_audio_config->adev[adevice].defined = 1;

		  /* First channel of device is valid. */
		  p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice)] = MEDIUM_RADIO;

		  strlcpy (p_audio_config->adev[adevice].adevice_out, t, sizeof(p_audio_config->adev[adevice].adevice_out));		  
	  }


/*
 * ARATE 		- Audio samples per second, 11025, 22050, 44100, etc.
 */

	  else if (strcasecmp(t, "ARATE") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing audio sample rate for ARATE command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= MIN_SAMPLES_PER_SEC && n <= MAX_SAMPLES_PER_SEC) {
	      p_audio_config->adev[adevice].samples_per_sec = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Use a more reasonable audio sample rate in range of %d - %d.\n", 
							line, MIN_SAMPLES_PER_SEC, MAX_SAMPLES_PER_SEC);
   	    }
	  }

/*
 * ACHANNELS 		- Number of audio channels for current device: 1 or 2
 */

	  else if (strcasecmp(t, "ACHANNELS") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing number of audio channels for ACHANNELS command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n ==1 || n == 2) {
	      p_audio_config->adev[adevice].num_channels = n;

	      /* Set valid channels depending on mono or stereo. */

	      p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice)] = MEDIUM_RADIO;
	      if (n == 2) {
	        p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice) + 1] = MEDIUM_RADIO;
	      }
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Number of audio channels must be 1 or 2.\n", line);
   	    }
	  }

/*
 * ==================== Radio channel parameters ==================== 
 */

/*
 * CHANNEL n		- Set channel for channel-specific commands.
 */

	  else if (strcasecmp(t, "CHANNEL") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing channel number for CHANNEL command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n < MAX_CHANS) {

	      channel = n;

	      if (p_audio_config->chan_medium[n] != MEDIUM_RADIO) {

	        if ( ! p_audio_config->adev[ACHAN2ADEV(n)].defined) {
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: Channel number %d is not valid because audio device %d is not defined.\n", 
								line, n, ACHAN2ADEV(n));
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: Channel number %d is not valid because audio device %d is not in stereo.\n", 
								line, n, ACHAN2ADEV(n));
	        }
	      }
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Channel number must in range of 0 to %d.\n", line, MAX_CHANS-1);
   	    }
	  }

/*
 * ICHANNEL n			- Define IGate virtual channel.
 *
 *	This allows a client application to talk to to APRS-IS
 *	by using a channel number outside the normal range for modems.
 *	In the future there might be other typs of virtual channels.
 *	This does not change the current channel number used by MODEM, PTT, etc.
 */

	  else if (strcasecmp(t, "ICHANNEL") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing virtual channel number for ICHANNEL command.\n", line);
	      continue;
	    }
	    int ichan = atoi(t);
            if (ichan >= MAX_CHANS && ichan < MAX_TOTAL_CHANS) {

	      if (p_audio_config->chan_medium[ichan] == MEDIUM_NONE) {

	        p_audio_config->chan_medium[ichan] = MEDIUM_IGATE;

	        // This is redundant but saves the time of searching through all
	        // the channels for each packet.
	        p_audio_config->igate_vchannel = ichan;
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: ICHANNEL can't use %d because it is already in use.\n", line, ichan);
	      }
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: ICHANNEL number must in range of %d to %d.\n", line, MAX_CHANS, MAX_TOTAL_CHANS-1);
	    }
	  }

/*
 * MYCALL station
 */
	  else if (strcasecmp(t, "mycall") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing value for MYCALL command on line %d.\n", line);
	      continue;
	    }
	    else {

	      char *p;
	      int const strict = 2;
	      char call_no_ssid[AX25_MAX_ADDR_LEN];
	      int ssid, heard;

	      for (p = t; *p != '\0'; p++) {
	        if (islower(*p)) {
		  *p = toupper(*p);	/* Silently force upper case. */
					/* Might change to warning someday. */
	        }
	      }

	      if ( ! ax25_parse_addr (-1, t, strict, call_no_ssid, &ssid, &heard)) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Invalid value for MYCALL command on line %d.\n", line);
	        continue;
	      }

	      // Definitely set for current channel.
	      // Set for other channels which have not been set yet.

	      int c;

	      for (c = 0; c < MAX_CHANS; c++) {

	        if (c == channel || 
			strlen(p_audio_config->achan[c].mycall) == 0 || 
			strcasecmp(p_audio_config->achan[c].mycall, "NOCALL") == 0 ||
			strcasecmp(p_audio_config->achan[c].mycall, "N0CALL") == 0) {

	          strlcpy (p_audio_config->achan[c].mycall, t, sizeof(p_audio_config->achan[c].mycall));
	        }
	      }
	    }
	  }


/*
 * MODEM	- Set modem properties for current channel.
 *
 *
 * Old style:
 * 	MODEM  baud [ mark  space  [A][B][C][+]  [  num-decoders spacing ] ] 
 *
 * New style, version 1.2:
 *	MODEM  speed [ option ] ...
 *
 * Options:
 *	mark:space	- AFSK tones.  Defaults based on speed.
 *	num@offset	- Multiple decoders on different frequencies.
 *	/9		- Divide sample rate by specified number.
 *	*9		- Upsample ratio for G3RUH.
 *	[A-Z+-]+	- Letters, plus, minus for the demodulator "profile."
 *	g3ruh		- This modem type regardless of default for speed.
 *	v26a or v26b	- V.26 alternative.  a=original, b=MFJ compatible
 */

	  else if (strcasecmp(t, "MODEM") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing data transmission speed for MODEM command.\n", line);
	      continue;
	    }
	    if (strcasecmp(t,"AIS") == 0) {
	      n = MAX_BAUD-1;	// Hack - See special case later.
	    }
	    else if (strcasecmp(t,"EAS") == 0) {
	      n = MAX_BAUD-2;	// Hack - See special case later.
	    }
	    else {
	      n = atoi(t);
	    }
            if (n >= MIN_BAUD && n <= MAX_BAUD) {
	      p_audio_config->achan[channel].baud = n;
	      if (n != 300 && n != 1200 && n != 2400 && n != 4800 && n != 9600 && n != 19200 && n != MAX_BAUD-1 && n != MAX_BAUD-2) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Warning: Non-standard data rate of %d bits per second.  Are you sure?\n", line, n);
    	      }
	    }
	    else {
	      p_audio_config->achan[channel].baud = DEFAULT_BAUD;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable data rate. Using %d bits per second.\n",
				 line, p_audio_config->achan[channel].baud);
   	    }


	    /* Set defaults based on speed. */
	    /* Should be same as -B command line option in direwolf.c. */

	    /* We have similar logic in direwolf.c, config.c, gen_packets.c, and atest.c, */
	    /* that need to be kept in sync.  Maybe it could be a common function someday. */

	    if (p_audio_config->achan[channel].baud < 600) {
              p_audio_config->achan[channel].modem_type = MODEM_AFSK;
              p_audio_config->achan[channel].mark_freq = 1600;
              p_audio_config->achan[channel].space_freq = 1800;
	    }
	    else if (p_audio_config->achan[channel].baud < 1800) {
              p_audio_config->achan[channel].modem_type = MODEM_AFSK;
              p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;
              p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;
	    }
	    else if (p_audio_config->achan[channel].baud < 3600) {
              p_audio_config->achan[channel].modem_type = MODEM_QPSK;
              p_audio_config->achan[channel].mark_freq = 0;
              p_audio_config->achan[channel].space_freq = 0;
	    }
	    else if (p_audio_config->achan[channel].baud < 7200) {
              p_audio_config->achan[channel].modem_type = MODEM_8PSK;
              p_audio_config->achan[channel].mark_freq = 0;
              p_audio_config->achan[channel].space_freq = 0;
	    }
	    else if (p_audio_config->achan[channel].baud == MAX_BAUD-1) {
              p_audio_config->achan[channel].modem_type = MODEM_AIS;
              p_audio_config->achan[channel].mark_freq = 0;
              p_audio_config->achan[channel].space_freq = 0;
	    }
	    else if (p_audio_config->achan[channel].baud == MAX_BAUD-2) {
	      p_audio_config->achan[channel].modem_type = MODEM_EAS;
	      p_audio_config->achan[channel].baud = 521;	// Actually 520.83 but we have an integer field here.
								// Will make more precise in afsk demod init.
	      p_audio_config->achan[channel].mark_freq = 2083;	// Actually 2083.3 - logic 1.
	      p_audio_config->achan[channel].space_freq = 1563;	// Actually 1562.5 - logic 0.
	      // ? strlcpy (p_audio_config->achan[channel].profiles, "A", sizeof(p_audio_config->achan[channel].profiles));
	    }
	    else {
              p_audio_config->achan[channel].modem_type = MODEM_SCRAMBLE;
              p_audio_config->achan[channel].mark_freq = 0;
              p_audio_config->achan[channel].space_freq = 0;
	    }

	    /* Get any options. */

	    t = split(NULL,0);
	    if (t == NULL) {
	      /* all done. */
	      continue;
	    }

	    if (alldigits(t)) {

/* old style */

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Old style (pre version 1.2) format will no longer be supported in next version.\n", line);

	      n = atoi(t);
	      /* Originally the upper limit was 3000. */
	      /* Version 1.0 increased to 5000 because someone */
	      /* wanted to use 2400/4800 Hz AFSK. */
	      /* Of course the MIC and SPKR connections won't */
	      /* have enough bandwidth so radios must be modified. */
              if (n >= 300 && n <= 5000) {
	        p_audio_config->achan[channel].mark_freq = n;
	      }
	      else {
	        p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Unreasonable mark tone frequency. Using %d.\n", 
				 line, p_audio_config->achan[channel].mark_freq);
   	      }
	    
	      /* Get space frequency */

	      t = split(NULL,0);
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Missing tone frequency for space.\n", line);
	        continue;
	      }
	      n = atoi(t);
              if (n >= 300 && n <= 5000) {
	        p_audio_config->achan[channel].space_freq = n;
	      }
	      else {
	        p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Unreasonable space tone frequency. Using %d.\n", 
					 line, p_audio_config->achan[channel].space_freq);
   	      }

	      /* Gently guide users toward new format. */

	      if (p_audio_config->achan[channel].baud == 1200 &&
                  p_audio_config->achan[channel].mark_freq == 1200 &&
                  p_audio_config->achan[channel].space_freq == 2200) {

	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: The AFSK frequencies can be omitted when using the 1200 baud default 1200:2200.\n", line);
	      }
	      if (p_audio_config->achan[channel].baud == 300 &&
                  p_audio_config->achan[channel].mark_freq == 1600 &&
                  p_audio_config->achan[channel].space_freq == 1800) {

	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: The AFSK frequencies can be omitted when using the 300 baud default 1600:1800.\n", line);
	      }

	      /* New feature in 0.9 - Optional filter profile(s). */

	      t = split(NULL,0);
	      if (t != NULL) {

	        /* Look for some combination of letter(s) and + */

	        if (isalpha(t[0]) || t[0] == '+') {
		  char *pc;

		  /* Here we only catch something other than letters and + mixed in. */
		  /* Later, we check for valid letters and no more than one letter if + specified. */

	          for (pc = t; *pc != '\0'; pc++) {
		    if ( ! isalpha(*pc) && ! (*pc == '+')) {
	              text_color_set(DW_COLOR_ERROR);
                      dw_printf ("Line %d: Demodulator type can only contain letters and + character.\n", line);
		    }
		  }    
		
		  strlcpy (p_audio_config->achan[channel].profiles, t, sizeof(p_audio_config->achan[channel].profiles));
	          t = split(NULL,0);
		  if (strlen(p_audio_config->achan[channel].profiles) > 1 && t != NULL) {
	            text_color_set(DW_COLOR_ERROR);
                    dw_printf ("Line %d: Can't combine multiple demodulator types and multiple frequencies.\n", line);
		    continue;
		  }
	        }
	      }

	      /* New feature in 0.9 - optional number of decoders and frequency offset between. */

	      if (t != NULL) {
	        n = atoi(t);
                if (n < 1 || n > MAX_SUBCHANS) {
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: Number of demodulators is out of range. Using 3.\n", line);
		  n = 3;
	        }
	        p_audio_config->achan[channel].num_freq = n;

	        t = split(NULL,0);
	        if (t != NULL) {
	          n = atoi(t);
                  if (n < 5 || n > abs(p_audio_config->achan[channel].mark_freq - p_audio_config->achan[channel].space_freq)/2) {
	            text_color_set(DW_COLOR_ERROR);
                    dw_printf ("Line %d: Unreasonable value for offset between modems.  Using 50 Hz.\n", line);
		    n = 50;
	          }
		  p_audio_config->achan[channel].offset = n;

	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: New style for multiple demodulators is %d@%d\n", line,
			p_audio_config->achan[channel].num_freq, p_audio_config->achan[channel].offset);	  
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: Missing frequency offset between modems.  Using 50 Hz.\n", line);
	          p_audio_config->achan[channel].offset = 50;
	        }    
	      }
	    }
	    else {

/* New style in version 1.2. */

	      while (t != NULL) {
		char *s;

		if ((s = strchr(t, ':')) != NULL) {		/* mark:space */

	          p_audio_config->achan[channel].mark_freq = atoi(t);
	          p_audio_config->achan[channel].space_freq = atoi(s+1);

		  if (p_audio_config->achan[channel].mark_freq == 0 && p_audio_config->achan[channel].space_freq == 0) {
		    p_audio_config->achan[channel].modem_type = MODEM_SCRAMBLE;
	          }
	          else {
		    p_audio_config->achan[channel].modem_type = MODEM_AFSK;

                    if (p_audio_config->achan[channel].mark_freq < 300 || p_audio_config->achan[channel].mark_freq > 5000) {
	              p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;
	              text_color_set(DW_COLOR_ERROR);
                      dw_printf ("Line %d: Unreasonable mark tone frequency. Using %d instead.\n", 
				 line, p_audio_config->achan[channel].mark_freq);
		    }
                    if (p_audio_config->achan[channel].space_freq < 300 || p_audio_config->achan[channel].space_freq > 5000) {
	              p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;
	              text_color_set(DW_COLOR_ERROR);
                      dw_printf ("Line %d: Unreasonable space tone frequency. Using %d instead.\n", 
				 line, p_audio_config->achan[channel].space_freq);
		    }
	          }
	        }

		else if ((s = strchr(t, '@')) != NULL) {		/* num@offset */

	          p_audio_config->achan[channel].num_freq = atoi(t);
	          p_audio_config->achan[channel].offset = atoi(s+1);

                  if (p_audio_config->achan[channel].num_freq < 1 || p_audio_config->achan[channel].num_freq > MAX_SUBCHANS) {
	            text_color_set(DW_COLOR_ERROR);
                    dw_printf ("Line %d: Number of demodulators is out of range. Using 3.\n", line);
	            p_audio_config->achan[channel].num_freq = 3;
		  }

                  if (p_audio_config->achan[channel].offset < 5 || 
			p_audio_config->achan[channel].offset > abs(p_audio_config->achan[channel].mark_freq - p_audio_config->achan[channel].space_freq)/2) {
	            text_color_set(DW_COLOR_ERROR);
                    dw_printf ("Line %d: Offset between demodulators is unreasonable. Using 50 Hz.\n", line);
	            p_audio_config->achan[channel].offset = 50;
		  }
		}

	        else if (alllettersorpm(t)) {		/* profile of letter(s) + - */

		  // Will be validated later.
		  strlcpy (p_audio_config->achan[channel].profiles, t, sizeof(p_audio_config->achan[channel].profiles));
	        }

		else if (*t == '/') {		/* /div */
		  int n = atoi(t+1);

                  if (n >= 1 && n <= 8) {
	            p_audio_config->achan[channel].decimate = n;
		  }
	    	  else {
	            text_color_set(DW_COLOR_ERROR);
                    dw_printf ("Line %d: Ignoring unreasonable sample rate division factor of %d.\n", line, n);
		  }
		}

		else if (*t == '*') {		/* *upsample */
		  int n = atoi(t+1);

                  if (n >= 1 && n <= 4) {
	            p_audio_config->achan[channel].upsample = n;
		  }
	    	  else {
	            text_color_set(DW_COLOR_ERROR);
                    dw_printf ("Line %d: Ignoring unreasonable upsample ratio of %d.\n", line, n);
		  }
		}

		else if (strcasecmp(t, "G3RUH") == 0) {		/* Force G3RUH modem regardless of default for speed. New in 1.6. */

                  p_audio_config->achan[channel].modem_type = MODEM_SCRAMBLE;
                  p_audio_config->achan[channel].mark_freq = 0;
                  p_audio_config->achan[channel].space_freq = 0;
		}

		else if (strcasecmp(t, "V26A") == 0 || 		/* Compatible with direwolf versions <= 1.5.  New in 1.6. */
			 strcasecmp(t, "V26B") == 0) {		/* Compatible with MFJ-2400.  New in 1.6. */

                  if (p_audio_config->achan[channel].modem_type != MODEM_QPSK ||
		      p_audio_config->achan[channel].baud != 2400) {

	            text_color_set(DW_COLOR_ERROR);
                    dw_printf ("Line %d: %s option can only be used with 2400 bps PSK.\n", line, t);
	            continue;
		  }
                  p_audio_config->achan[channel].v26_alternative = (strcasecmp(t, "V26A") == 0) ? V26_A : V26_B;
		}

		else {
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: Unrecognized option for MODEM: %s\n", line, t);
	        } 

	        t = split(NULL,0);
	      }

	      /* A later place catches disallowed combination of + and @. */
	      /* A later place sets /n for 300 baud if not specified by user. */

	      //dw_printf ("debug: div = %d\n", p_audio_config->achan[channel].decimate);

	    }
	  }



/*
 * DTMF  		- Enable DTMF decoder.
 *
 * Future possibilities: 
 *	Option to determine if it goes to APRStt gateway and/or application.
 *	Disable normal demodulator to reduce CPU requirements.
 */


	  else if (strcasecmp(t, "DTMF") == 0) {

	    p_audio_config->achan[channel].dtmf_decode = DTMF_DECODE_ON;

	  }


/*
 * FIX_BITS  n  [ APRS | AX25 | NONE ] [ PASSALL ]
 *
 *	- Attempt to fix frames with bad FCS.
 *	- n is maximum number of bits to attempt fixing.
 *	- Optional sanity check & allow everything even with bad FCS.
 */

	  else if (strcasecmp(t, "FIX_BITS") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing value for FIX_BITS command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= RETRY_NONE && n < RETRY_MAX) {		// MAX is actually last valid +1
	      p_audio_config->achan[channel].fix_bits = (retry_t)n;
	    }
	    else {
	      p_audio_config->achan[channel].fix_bits = DEFAULT_FIX_BITS;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid value %d for FIX_BITS. Using default of %d.\n",
			line, n, p_audio_config->achan[channel].fix_bits);
	    }

	    if (p_audio_config->achan[channel].fix_bits > DEFAULT_FIX_BITS) {
	      text_color_set(DW_COLOR_INFO);
              dw_printf ("Line %d: Using a FIX_BITS value greater than %d is not recommended for normal operation.\n",
			line, DEFAULT_FIX_BITS);
              dw_printf ("FIX_BITS > 1 was an interesting experiment but turned out to be a bad idea.\n");
              dw_printf ("Don't be surprised if it takes 100%% CPU, direwolf can't keep up with the audio stream,\n");
              dw_printf ("and you see messages like \"Audio input device 0 error code -32: Broken pipe\"\n");
	    }

	    t = split(NULL,0);
	    while (t != NULL) {

	      // If more than one sanity test, we silently take the last one.

	      if (strcasecmp(t, "APRS") == 0) {
	        p_audio_config->achan[channel].sanity_test = SANITY_APRS;
	      }
	      else if (strcasecmp(t, "AX25") == 0 || strcasecmp(t, "AX.25") == 0) {
	        p_audio_config->achan[channel].sanity_test = SANITY_AX25;
	      }
	      else if (strcasecmp(t, "NONE") == 0) {
	        p_audio_config->achan[channel].sanity_test = SANITY_NONE;
	      }
	      else if (strcasecmp(t, "PASSALL") == 0) {
	        p_audio_config->achan[channel].passall = 1;
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: There is an old saying, \"Be careful what you ask for because you might get it.\"\n", line);
                dw_printf ("The PASSALL option means allow all frames even when they are invalid.\n");
                dw_printf ("You are asking to receive random trash and you WILL get your wish.\n");
                dw_printf ("Don't complain when you see all sorts of random garbage.  That's what you asked for.\n");
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Invalid option '%s' for FIX_BITS.\n", line, t);
	      }
	      t = split(NULL,0);
	    }
	  }


/*
 * PTT 		- Push To Talk signal line.
 * DCD		- Data Carrier Detect indicator.
 * CON		- Connected to another station indicator.
 *
 * xxx  serial-port [-]rts-or-dtr [ [-]rts-or-dtr ]
 * xxx  GPIO  [-]gpio-num
 * xxx  LPT  [-]bit-num
 * PTT  RIG  model  port [ rate ]
 * PTT  RIG  AUTO  port [ rate ]
 * PTT  CM108 [ [-]bit-num ] [ hid-device ]
 *
 * 		When model is 2, port would host:port like 127.0.0.1:4532
 *		Otherwise, port would be a serial port like /dev/ttyS0
 *
 *
 * Applies to most recent CHANNEL command.
 */

	  else if (strcasecmp(t, "PTT") == 0 || strcasecmp(t, "DCD") == 0 || strcasecmp(t, "CON") == 0) {
	    int ot;
	    char otname[8];

	    if (strcasecmp(t, "PTT") == 0) {
	      ot = OCTYPE_PTT;
	      strlcpy (otname, "PTT", sizeof(otname));
	    }
	    else if (strcasecmp(t, "DCD") == 0) {
	      ot = OCTYPE_DCD;
	      strlcpy (otname, "DCD", sizeof(otname));
	    }
	    else {
	      ot = OCTYPE_CON;
	      strlcpy (otname, "CON", sizeof(otname));
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: Missing output control device for %s command.\n",
			line, otname);
	      continue;
	    }

	    if (strcasecmp(t, "GPIO") == 0) {

/* GPIO case, Linux only. */

#if __WIN32__
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: %s with GPIO is only available on Linux.\n", line, otname);
#else		
	      t = split(NULL,0);
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing GPIO number for %s.\n", line, otname);
	        continue;
	      }

	      if (*t == '-') {
	        p_audio_config->achan[channel].octrl[ot].out_gpio_num = atoi(t+1);
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
	      }
	      else {
	        p_audio_config->achan[channel].octrl[ot].out_gpio_num = atoi(t);
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
	      }
	      p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_GPIO;
#endif
	    }
	    else if (strcasecmp(t, "LPT") == 0) {

/* Parallel printer case, x86 Linux only. */

#if  ( defined(__i386__) || defined(__x86_64__) ) && ( defined(__linux__) || defined(__unix__) )

	      t = split(NULL,0);
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing LPT bit number for %s.\n", line, otname);
	        continue;
	      }

	      if (*t == '-') {
	        p_audio_config->achan[channel].octrl[ot].ptt_lpt_bit = atoi(t+1);
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
	      }
	      else {
	        p_audio_config->achan[channel].octrl[ot].ptt_lpt_bit = atoi(t);
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
	      }
	      p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_LPT;
#else
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: %s with LPT is only available on x86 Linux.\n", line, otname);
#endif		
	    }
	    else if (strcasecmp(t, "RIG") == 0) {
#ifdef USE_HAMLIB

	      t = split(NULL,0);
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing model number for hamlib.\n", line);
	        continue;
	      }
	      if (strcasecmp(t, "AUTO") == 0) {
	        p_audio_config->achan[channel].octrl[ot].ptt_model = -1;
	      }
	      else {
		if ( ! alldigits(t)) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file line %d: A rig number, not a name, is required here.\n", line);
	          dw_printf ("For example, if you have a Yaesu FT-847, specify 101.\n");
	          dw_printf ("See https://github.com/Hamlib/Hamlib/wiki/Supported-Radios for more details.\n");
	          continue;
	        }
	        int n = atoi(t);
		if (n < 1 || n > 9999) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file line %d: Unreasonable model number %d for hamlib.\n", line, n);
	          continue;
	        }
	        p_audio_config->achan[channel].octrl[ot].ptt_model = n;
	      }

	      t = split(NULL,0);
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing port for hamlib.\n", line);
	        continue;
	      }
	      strlcpy (p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));

	      // Optional serial port rate for CAT control PTT.

	      t = split(NULL,0);
	      if (t != NULL) {
		if ( ! alldigits(t)) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file line %d: An optional number is required here for CAT serial port speed: %s\n", line, t);
	          continue;
	        }
	        int n = atoi(t);
	        p_audio_config->achan[channel].octrl[ot].ptt_rate = n;
	      }

	      t = split(NULL,0);
	      if (t != NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: %s was not expected after model & port for hamlib.\n", line, t);
	      }

	      p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_HAMLIB;

#else
#if __WIN32__
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: Windows version of direwolf does not support HAMLIB.\n", line);
	      exit (EXIT_FAILURE);
#else
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: %s with RIG is only available when hamlib support is enabled.\n", line, otname);
	      dw_printf ("You must rebuild direwolf with hamlib support.\n");
	      dw_printf ("See User Guide for details.\n");
#endif

#endif
	    }
	    else if (strcasecmp(t, "CM108") == 0) {

/* CM108 - GPIO of USB sound card. case, Linux and Windows only. */

#if USE_CM108

	      if (ot != OCTYPE_PTT) {
		// Future project:  Allow DCD and CON via the same device.
	        // This gets more complicated because we can't selectively change a single GPIO bit.
	        // We would need to keep track of what is currently there, change one bit, in our local
	        // copy of the status and then write out the byte for all of the pins.
	        // Let's keep it simple with just PTT for the first stab at this.

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: PTT CM108 option is only valid for PTT, not %s.\n", line, otname);
	        continue;
	      }

	      p_audio_config->achan[channel].octrl[ot].out_gpio_num = 3;	// All known designs use GPIO 3.
										// User can override for special cases.
	      p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;		// High for transmit.
	      strcpy (p_audio_config->achan[channel].octrl[ot].ptt_device, "");

	      // Try to find PTT device for audio output device.
	      // Simplifiying assumption is that we have one radio per USB Audio Adapter.
	      // Failure at this point is not an error.
	      // See if config file sets it explicitly before complaining.

	      cm108_find_ptt (p_audio_config->adev[ACHAN2ADEV(channel)].adevice_out,
				p_audio_config->achan[channel].octrl[ot].ptt_device,
				(int)sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));

	      while ((t = split(NULL,0)) != NULL) {
	        if (*t == '-') {
	          p_audio_config->achan[channel].octrl[ot].out_gpio_num = atoi(t+1);
		  p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
	        }
	        else if (isdigit(*t)) {
	          p_audio_config->achan[channel].octrl[ot].out_gpio_num = atoi(t);
		  p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
	        }
#if __WIN32__
	        else if (*t == '\\') {
	          strlcpy (p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));
		}
		else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file line %d: Found \"%s\" when expecting GPIO number or device name like \\\\?\\hid#vid_0d8c&... .\n", line, t);
	          continue;
	        }
#else
	        else if (*t == '/') {
	          strlcpy (p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));
		}
		else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file line %d: Found \"%s\" when expecting GPIO number or device name like /dev/hidraw1.\n", line, t);
	          continue;
	        }
#endif
	      }
	      if (p_audio_config->achan[channel].octrl[ot].out_gpio_num < 1 || p_audio_config->achan[channel].octrl[ot].out_gpio_num > 8) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file line %d: CM108 GPIO number %d is not in range of 1 thru 8.\n", line,
					p_audio_config->achan[channel].octrl[ot].out_gpio_num);
	          continue;
	      }
	      if (strlen(p_audio_config->achan[channel].octrl[ot].ptt_device) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Could not determine USB Audio GPIO PTT device for audio output %s.\n", line,
					p_audio_config->adev[ACHAN2ADEV(channel)].adevice_out);
#if __WIN32__
	        dw_printf ("You must explicitly mention a HID path.\n");
#else
	        dw_printf ("You must explicitly mention a device name such as /dev/hidraw1.\n");
#endif
	        dw_printf ("Run \"cm108\" utility to get a list.\n");
	        dw_printf ("See Interface Guide for details.\n");
	        continue;
	      }
	      p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_CM108;

#else
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: %s with CM108 is only available when USB Audio GPIO support is enabled.\n", line, otname);
	      dw_printf ("You must rebuild direwolf with CM108 Audio Adapter GPIO PTT support.\n");
	      dw_printf ("See Interface Guide for details.\n");
	      rtfm();
	      exit (EXIT_FAILURE);
#endif
	    }
	    else  {

/* serial port case. */

	      strlcpy (p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));

	      t = split(NULL,0);
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing RTS or DTR after %s device name.\n", 
			line, otname);
	        continue;
	      }

	      if (strcasecmp(t, "rts") == 0) {
	        p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_RTS;
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
	      }
	      else if (strcasecmp(t, "dtr") == 0) {
	        p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_DTR;
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
	      }
	      else if (strcasecmp(t, "-rts") == 0) {
	        p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_RTS;
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
	      }
	      else if (strcasecmp(t, "-dtr") == 0) {
	        p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_DTR;
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Expected RTS or DTR after %s device name.\n", 
			line, otname);
	        continue;
	      }


	      p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_SERIAL;


	      /* In version 1.2, we allow a second one for same serial port. */
	      /* Some interfaces want the two control lines driven with opposite polarity. */
	      /* e.g.   PTT COM1 RTS -DTR  */

	      t = split(NULL,0);
	      if (t != NULL) {

	        if (strcasecmp(t, "rts") == 0) {
	          p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_RTS;
		  p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 0;
	        }
	        else if (strcasecmp(t, "dtr") == 0) {
	          p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_DTR;
		  p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 0;
	        }
	        else if (strcasecmp(t, "-rts") == 0) {
	          p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_RTS;
		  p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 1;
	        }
	        else if (strcasecmp(t, "-dtr") == 0) {
	          p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_DTR;
		  p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 1;
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file line %d: Expected RTS or DTR after first RTS or DTR.\n", 
			line);
	          continue;
	        }

		/* Would not make sense to specify the same one twice. */

		if (p_audio_config->achan[channel].octrl[ot].ptt_line == p_audio_config->achan[channel].octrl[ot].ptt_line2) {
	          dw_printf ("Config file line %d: Doesn't make sense to specify the some control line twice.\n", 
			line);
	        }

	      }  /* end of second serial port control line. */
	    }  /* end of serial port case. */

	  }  /* end of PTT, DCD, CON */

/*
 * INPUTS
 *
 * TXINH - TX holdoff input
 *
 * TXINH GPIO [-]gpio-num (only type supported so far)
 */

	  else if (strcasecmp(t, "TXINH") == 0) {
	    char itname[8];

	    strlcpy (itname, "TXINH", sizeof(itname));

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: Missing input type name for %s command.\n", line, itname);
	      continue;
	    }

	    if (strcasecmp(t, "GPIO") == 0) {

#if __WIN32__
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: %s with GPIO is only available on Linux.\n", line, itname);
#else
	      t = split(NULL,0);
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
		dw_printf ("Config file line %d: Missing GPIO number for %s.\n", line, itname);
		continue;
	      }

	      if (*t == '-') {
	        p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].in_gpio_num = atoi(t+1);
		p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].invert = 1;
	      }
	      else {
	        p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].in_gpio_num = atoi(t);
		p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].invert = 0;
	      }
	      p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].method = PTT_METHOD_GPIO;
#endif
	    }
	  }


/*
 * DWAIT n		- Extra delay for receiver squelch. n = 10 mS units.
 */

	  else if (strcasecmp(t, "DWAIT") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing delay time for DWAIT command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n <= 255) {
	      p_audio_config->achan[channel].dwait = n;
	    }
	    else {
	      p_audio_config->achan[channel].dwait = DEFAULT_DWAIT;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid delay time for DWAIT. Using %d.\n", 
			line, p_audio_config->achan[channel].dwait);
   	    }
	  }

/*
 * SLOTTIME n		- For non-digipeat transmit delay timing. n = 10 mS units.
 */

	  else if (strcasecmp(t, "SLOTTIME") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing delay time for SLOTTIME command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n <= 255) {
	      p_audio_config->achan[channel].slottime = n;
	    }
	    else {
	      p_audio_config->achan[channel].slottime = DEFAULT_SLOTTIME;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid delay time for persist algorithm. Using %d.\n", 
			line, p_audio_config->achan[channel].slottime);
   	    }
	  }

/*
 * PERSIST 		- For non-digipeat transmit delay timing.
 */

	  else if (strcasecmp(t, "PERSIST") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing probability for PERSIST command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n <= 255) {
	      p_audio_config->achan[channel].persist = n;
	    }
	    else {
	      p_audio_config->achan[channel].persist = DEFAULT_PERSIST;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid probability for persist algorithm. Using %d.\n", 
			line, p_audio_config->achan[channel].persist);
   	    }
	  }

/*
 * TXDELAY n		- For transmit delay timing. n = 10 mS units.
 */

	  else if (strcasecmp(t, "TXDELAY") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing time for TXDELAY command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n <= 255) {
	      p_audio_config->achan[channel].txdelay = n;
	    }
	    else {
	      p_audio_config->achan[channel].txdelay = DEFAULT_TXDELAY;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid time for transmit delay. Using %d.\n", 
			line, p_audio_config->achan[channel].txdelay);
   	    }
	  }

/*
 * TXTAIL n		- For transmit timing. n = 10 mS units.
 */

	  else if (strcasecmp(t, "TXTAIL") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing time for TXTAIL command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n <= 255) {
	      p_audio_config->achan[channel].txtail = n;
	    }
	    else {
	      p_audio_config->achan[channel].txtail = DEFAULT_TXTAIL;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid time for transmit timing. Using %d.\n", 
			line, p_audio_config->achan[channel].txtail);
   	    }
	  }

/*
 * FULLDUP  {on|off} 		- Full Duplex
 */
	  else if (strcasecmp(t, "FULLDUP") == 0) {

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing parameter for FULLDUP command.  Expecting ON or OFF.\n", line);
	      continue;
	    }
	    if (strcasecmp(t, "ON") == 0) {
	      p_audio_config->achan[channel].fulldup = 1;
	    }
	    else if (strcasecmp(t, "OFF") == 0) {
	      p_audio_config->achan[channel].fulldup = 0;
	    }
	    else {
	      p_audio_config->achan[channel].fulldup = 0;
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Expected ON or OFF for FULLDUP.\n", line);
	    }
	  }

/*
 * SPEECH  script 
 *
 * Specify script for text-to-speech function.		
 */

	  else if (strcasecmp(t, "SPEECH") == 0) {

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing script for Text-to-Speech function.\n", line);
	      continue;
	    }
	    
	    /* See if we can run it. */

	    if (xmit_speak_it(t, -1, " ") == 0) {
	      if (strlcpy (p_audio_config->tts_script, t, sizeof(p_audio_config->tts_script)) >= sizeof(p_audio_config->tts_script)) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Script for text-to-speech function is too long.\n", line);
	      }
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Error trying to run Text-to-Speech function.\n", line);
	      continue;
	   }
	  }

/*
 * FX25TX n		- Enable FX.25 transmission.  Default off.
 *				0 = off, 1 = auto mode, others are suggestions for testing
 *				or special cases.  16, 32, 64 is number of parity bytes to add.
 *				Also set by "-X n" command line option.
 *				V1.7 changed from global to per-channel setting.
 */

	  else if (strcasecmp(t, "FX25TX") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing FEC mode for FX25TX command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n < 200) {
	      p_audio_config->achan[channel].fx25_strength = n;
	      p_audio_config->achan[channel].layer2_xmit = LAYER2_FX25;
	    }
	    else {
	      p_audio_config->achan[channel].fx25_strength = 1;
	      p_audio_config->achan[channel].layer2_xmit = LAYER2_FX25;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable value for FX.25 transmission mode. Using %d.\n", 
			line, p_audio_config->achan[channel].fx25_strength);
   	    }
	  }

/*
 * FX25AUTO n		- Enable Automatic use of FX.25 for connected mode.
 *				Automatically enable, for that session only, when an identical
 *				frame is sent more than this number of times.
 *				Default 5 based on half of default RETRY.
 *				0 to disable feature.
 *				Current a global setting.  Could be per channel someday.
 */

	  else if (strcasecmp(t, "FX25AUTO") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing count for FX25AUTO command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n < 20) {
	      p_audio_config->fx25_auto_enable = n;
	    }
	    else {
	      p_audio_config->fx25_auto_enable = AX25_N2_RETRY_DEFAULT / 2;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable count for connected mode automatic FX.25. Using %d.\n", 
			line, p_audio_config->fx25_auto_enable);
   	    }
	  }

/*
 * IL2PTX  [ + - ] [ 0 1 ]	- Enable IL2P transmission.  Default off.
 *				"+" means normal polarity. Redundant since it is the default.
 *					(command line -I for first channel)
 *				"-" means inverted polarity. Do not use for 1200 bps.
 *					(command line -i for first channel)
 *				"0" means weak FEC.  Not recommended.
 *				"1" means stronger FEC.  "Max FEC."  Default if not specified.
 */

	  else if (strcasecmp(t, "IL2PTX") == 0) {

	    p_audio_config->achan[channel].layer2_xmit = LAYER2_IL2P;
	    p_audio_config->achan[channel].il2p_max_fec = 1;
	    p_audio_config->achan[channel].il2p_invert_polarity = 0;

	    while ((t = split(NULL,0)) != NULL) {
	      for (char *c = t; *c != '\0'; c++) {
	        switch (*c) {
	          case '+':
	            p_audio_config->achan[channel].il2p_invert_polarity = 0;
	            break;
	          case '-':
	            p_audio_config->achan[channel].il2p_invert_polarity = 1;
	            break;
	          case '0':
	            p_audio_config->achan[channel].il2p_max_fec = 0;
	            break;
	          case '1':
	            p_audio_config->achan[channel].il2p_max_fec = 1;
	            break;
	          default:
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Line %d: Invalid parameter '%c' fol IL2PTX command.\n", line, *c);
	            continue;
	            break;
	        }
	      }
	    }
	  }


/*
 * ==================== APRS Digipeater parameters ====================
 */

/*
 * DIGIPEAT  from-chan  to-chan  alias-pattern  wide-pattern  [ OFF|DROP|MARK|TRACE | ATGP=alias ]
 *
 * ATGP is an ugly hack for the specific need of ATGP which needs more that 8 digipeaters.
 * DO NOT put this in the User Guide.  On a need to know basis.
 */

	  else if (strcasecmp(t, "DIGIPEAT") == 0 || strcasecmp(t, "DIGIPEATER") == 0) {
	    int from_chan, to_chan;
	    int e;
	    char message[100];
	    	    

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing FROM-channel on line %d.\n", line);
	      continue;
	    }
	    if ( ! alldigits(t)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: '%s' is not allowed for FROM-channel.  It must be a number.\n",
							 line, t);
	      continue;
	    }
	    from_chan = atoi(t);
	    if (from_chan < 0 || from_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: FROM-channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }

	    // Channels specified must be radio channels or network TNCs.

	    if (p_audio_config->chan_medium[from_chan] != MEDIUM_RADIO &&
	        p_audio_config->chan_medium[from_chan] != MEDIUM_NETTNC) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: FROM-channel %d is not valid.\n", 
							line, from_chan);
	      continue;
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }
	    if ( ! alldigits(t)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: '%s' is not allowed for TO-channel.  It must be a number.\n",
							 line, t);
	      continue;
	    }
	    to_chan = atoi(t);
	    if (to_chan < 0 || to_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: TO-channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }

	    if (p_audio_config->chan_medium[to_chan] != MEDIUM_RADIO &&
	        p_audio_config->chan_medium[to_chan] != MEDIUM_NETTNC) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TO-channel %d is not valid.\n", 
							line, to_chan);
	      continue;
	    }
	
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing alias pattern on line %d.\n", line);
	      continue;
	    }
	    e = regcomp (&(p_digi_config->alias[from_chan][to_chan]), t, REG_EXTENDED|REG_NOSUB);
	    if (e != 0) {
	      regerror (e, &(p_digi_config->alias[from_chan][to_chan]), message, sizeof(message));
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Invalid alias matching pattern on line %d:\n%s\n", 
							line, message);
	      continue;
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing wide pattern on line %d.\n", line);
	      continue;
	    }
	    e = regcomp (&(p_digi_config->wide[from_chan][to_chan]), t, REG_EXTENDED|REG_NOSUB);
	    if (e != 0) {
	      regerror (e, &(p_digi_config->wide[from_chan][to_chan]), message, sizeof(message));
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Invalid wide matching pattern on line %d:\n%s\n", 
							line, message);
	      continue;
	    }

	    p_digi_config->enabled[from_chan][to_chan] = 1;
	    p_digi_config->preempt[from_chan][to_chan] = PREEMPT_OFF;

	    t = split(NULL,0);
	    if (t != NULL) {
	      if (strcasecmp(t, "OFF") == 0) {
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_OFF;
	        t = split(NULL,0);
	      }
	      else if (strcasecmp(t, "DROP") == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: Preemptive digipeating DROP option is discouraged.\n", line);
	        dw_printf ("It can create a via path which is misleading about the actual path taken.\n");
	        dw_printf ("PREEMPT is the best choice for this feature.\n");
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_DROP;
	        t = split(NULL,0);
	      }
	      else if (strcasecmp(t, "MARK") == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: Preemptive digipeating MARK option is discouraged.\n", line);
	        dw_printf ("It can create a via path which is misleading about the actual path taken.\n");
	        dw_printf ("PREEMPT is the best choice for this feature.\n");
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_MARK;
	        t = split(NULL,0);
	      }
	      else if ((strcasecmp(t, "TRACE") == 0) || (strncasecmp(t, "PREEMPT", 7) == 0)){
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_TRACE;
	        t = split(NULL,0);
	      }
	      else if (strncasecmp(t, "ATGP=", 5) == 0) {
	        strlcpy (p_digi_config->atgp[from_chan][to_chan], t+5, sizeof(p_digi_config->atgp[from_chan][to_chan]));;
	        t = split(NULL,0);
	      }
	    }

	    if (t != NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Found \"%s\" where end of line was expected.\n", line, t);     
	    }
	  }

/*
 * DEDUPE 		- Time to suppress digipeating of duplicate APRS packets.
 */

	  else if (strcasecmp(t, "DEDUPE") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing time for DEDUPE command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n < 600) {
	      p_digi_config->dedupe_time = n;
	    }
	    else {
	      p_digi_config->dedupe_time = DEFAULT_DEDUPE;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable value for dedupe time. Using %d.\n", 
			line, p_digi_config->dedupe_time);
   	    }
	  }

/*
 * REGEN 		- Signal regeneration.
 */

	  else if (strcasecmp(t, "regen") == 0) {
	    int from_chan, to_chan;
	    	    

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing FROM-channel on line %d.\n", line);
	      continue;
	    }
	    if ( ! alldigits(t)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: '%s' is not allowed for FROM-channel.  It must be a number.\n",
							 line, t);
	      continue;
	    }
	    from_chan = atoi(t);
	    if (from_chan < 0 || from_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: FROM-channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }

	    // Only radio channels are valid for regenerate.

	    if (p_audio_config->chan_medium[from_chan] != MEDIUM_RADIO) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: FROM-channel %d is not valid.\n", 
							line, from_chan);
	      continue;
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }
	    if ( ! alldigits(t)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: '%s' is not allowed for TO-channel.  It must be a number.\n",
							 line, t);
	      continue;
	    }
	    to_chan = atoi(t);
	    if (to_chan < 0 || to_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: TO-channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }
	    if (p_audio_config->chan_medium[to_chan] != MEDIUM_RADIO) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TO-channel %d is not valid.\n", 
							line, to_chan);
	      continue;
	    }
	

	    p_digi_config->regen[from_chan][to_chan] = 1;

	  }


/*
 * ==================== Connected Digipeater parameters ====================
 */

/*
 * CDIGIPEAT  from-chan  to-chan [ alias-pattern ]
 */

	  else if (strcasecmp(t, "CDIGIPEAT") == 0 || strcasecmp(t, "CDIGIPEATER") == 0) {
	    int from_chan, to_chan;
	    int e;
	    char message[100];

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing FROM-channel on line %d.\n", line);
	      continue;
	    }
	    if ( ! alldigits(t)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: '%s' is not allowed for FROM-channel.  It must be a number.\n",
							 line, t);
	      continue;
	    }
	    from_chan = atoi(t);
	    if (from_chan < 0 || from_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: FROM-channel must be in range of 0 to %d on line %d.\n",
							MAX_CHANS-1, line);
	      continue;
	    }

	    // For connected mode Link layer, only internal modems should be allowed.
	    // A network TNC probably would not provide information about channel status.
	    // There is discussion about this in the document called
	    // Why-is-9600-only-twice-as-fast-as-1200.pdf

	    if (p_audio_config->chan_medium[from_chan] != MEDIUM_RADIO) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: FROM-channel %d is not valid.\n",
							line, from_chan);
	      dw_printf ("Only internal modems can be used for connected mode packet.\n");
	      continue;
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }
	    if ( ! alldigits(t)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: '%s' is not allowed for TO-channel.  It must be a number.\n",
							 line, t);
	      continue;
	    }
	    to_chan = atoi(t);
	    if (to_chan < 0 || to_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: TO-channel must be in range of 0 to %d on line %d.\n",
							MAX_CHANS-1, line);
	      continue;
	    }
	    if (p_audio_config->chan_medium[to_chan] != MEDIUM_RADIO) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TO-channel %d is not valid.\n",
							line, to_chan);
	      dw_printf ("Only internal modems can be used for connected mode packet.\n");
	      continue;
	    }

	    t = split(NULL,0);
	    if (t != NULL) {
	      e = regcomp (&(p_cdigi_config->alias[from_chan][to_chan]), t, REG_EXTENDED|REG_NOSUB);
	      if (e == 0) {
	        p_cdigi_config->has_alias[from_chan][to_chan] = 1;
	      }
	      else {
	        regerror (e, &(p_cdigi_config->alias[from_chan][to_chan]), message, sizeof(message));
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Invalid alias matching pattern on line %d:\n%s\n",
							line, message);
	        continue;
	      }
	      t = split(NULL,0);
	    }

	    p_cdigi_config->enabled[from_chan][to_chan] = 1;

	    if (t != NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Found \"%s\" where end of line was expected.\n", line, t);
	    }
	  }


/*
 * ==================== Packet Filtering for APRS digipeater or IGate ====================
 */

/*
 * FILTER  from-chan  to-chan  filter_specification_expression
 * FILTER  from-chan  IG       filter_specification_expression
 * FILTER  IG         to-chan  filter_specification_expression
 *
 *
 * Note that we have three different config file filter commands:
 *
 *	FILTER		- Originally for APRS digipeating but later enhanced
 *			  to include IGate client side.  Maybe it should be
 *			  renamed AFILTER to make it clearer after adding CFILTER.
 *
 *			  Both internal modem and NET TNC channels allowed here.
 *			  "IG" should be used for the IGate, NOT a virtual channel
 *			  assigned to it.
 *
 *	CFILTER		- Similar for connected moded digipeater.
 *
 *			  Only internal modems can be used because they provide
 *			  information about radio channel status.
 *			  A remote network TNC might not provide the necessary
 *			  status for correct operation.
 *			  There is discussion about this in the document called
 *			  Why-is-9600-only-twice-as-fast-as-1200.pdf
 *
 *	IGFILTER	- APRS-IS (IGate) server side - completely different.
 *			  I'm not happy with this name because IG sounds like IGate
 *			  which is really the client side.  More comments later.
 *			  Maybe it should be called subscribe or something like that
 *			  because the subscriptions are cumulative.
 */

	  else if (strcasecmp(t, "FILTER") == 0) {
	    int from_chan, to_chan;
	    	    

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing FROM-channel on line %d.\n", line);
	      continue;
	    }
	    if (*t == 'i' || *t == 'I') {
	      from_chan = MAX_CHANS;
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: FILTER IG ... on line %d.\n", line);
	      dw_printf ("Warning! Don't mess with IS>RF filtering unless you are an expert and have an unusual situation.\n");
	      dw_printf ("Warning! The default is fine for nearly all situations.\n");
	      dw_printf ("Warning! Be sure to read carefully and understand  Successful-APRS-Gateway-Operation.pdf .\n");
	      dw_printf ("Warning! If you insist, be sure to add \" | i/180 \" so you don't break messaging.\n");
	    }
	    else {
	      from_chan = isdigit(*t) ? atoi(t) : -999;
	      if (from_chan < 0 || from_chan >= MAX_CHANS) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Filter FROM-channel must be in range of 0 to %d or \"IG\" on line %d.\n", 
							MAX_CHANS-1, line);
	        continue;
	      }

	      if (p_audio_config->chan_medium[from_chan] != MEDIUM_RADIO &&
		  p_audio_config->chan_medium[from_chan] != MEDIUM_NETTNC) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: FROM-channel %d is not valid.\n", 
							line, from_chan);
	        continue;
	      }
	      if (p_audio_config->chan_medium[from_chan] == MEDIUM_IGATE) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: Use 'IG' rather than %d for FROM-channel.\n",
							line, from_chan);
	        continue;
	      }
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }
	    if (*t == 'i' || *t == 'I') {
	      to_chan = MAX_CHANS;
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: FILTER ... IG ... on line %d.\n", line);
	      dw_printf ("Warning! Don't mess with RF>IS filtering unless you are an expert and have an unusual situation.\n");
	      dw_printf ("Warning! Expected behavior is for everything to go from RF to IS.\n");
	      dw_printf ("Warning! The default is fine for nearly all situations.\n");
	      dw_printf ("Warning! Be sure to read carefully and understand  Successful-APRS-Gateway-Operation.pdf .\n");
	    }
	    else {
	      to_chan = isdigit(*t) ? atoi(t) : -999;
	      if (to_chan < 0 || to_chan >= MAX_CHANS) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Filter TO-channel must be in range of 0 to %d or \"IG\" on line %d.\n", 
							MAX_CHANS-1, line);
	        continue;
	      }
	      if (p_audio_config->chan_medium[to_chan] != MEDIUM_RADIO &&
		  p_audio_config->chan_medium[to_chan] != MEDIUM_NETTNC) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: TO-channel %d is not valid.\n", 
							line, to_chan);
	        continue;
	      }
	      if (p_audio_config->chan_medium[to_chan] == MEDIUM_IGATE) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: Use 'IG' rather than %d for TO-channel.\n",
							line, to_chan);
	        continue;
	      }
	    }

	    t = split(NULL,1);		/* Take rest of line including spaces. */

	    if (t == NULL) {
	      t = " ";				/* Empty means permit nothing. */
	    }

	    if (p_digi_config->filter_str[from_chan][to_chan] != NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Replacing previous filter for same from/to pair:\n        %s\n",
				line, p_digi_config->filter_str[from_chan][to_chan]);
	      free (p_digi_config->filter_str[from_chan][to_chan]);
	      p_digi_config->filter_str[from_chan][to_chan] = NULL;
	    }

	    p_digi_config->filter_str[from_chan][to_chan] = strdup(t);

//TODO:  Do a test run to see errors now instead of waiting.

	  }


/*
 * ==================== Packet Filtering for connected digipeater ====================
 */

/*
 * CFILTER  from-chan  to-chan  filter_specification_expression
 *
 * Why did I put this here?
 * What would be a useful use case?  Perhaps block by source or destination?
 */

	  else if (strcasecmp(t, "CFILTER") == 0) {
	    int from_chan, to_chan;
	    	    

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing FROM-channel on line %d.\n", line);
	      continue;
	    }

	    from_chan = isdigit(*t) ? atoi(t) : -999;
	    if (from_chan < 0 || from_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Filter FROM-channel must be in range of 0 to %d on line %d.\n",
							MAX_CHANS-1, line);
	      continue;
	    }

	    // DO NOT allow a network TNC here.
	    // Must be internal modem to have necessary knowledge about channel status.

            if (p_audio_config->chan_medium[from_chan] != MEDIUM_RADIO) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: FROM-channel %d is not valid.\n",
							line, from_chan);
	      continue;
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }

	    to_chan = isdigit(*t) ? atoi(t) : -999;
	    if (to_chan < 0 || to_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Filter TO-channel must be in range of 0 to %d on line %d.\n",
							MAX_CHANS-1, line);
	      continue;
	    }
            if (p_audio_config->chan_medium[to_chan] != MEDIUM_RADIO) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TO-channel %d is not valid.\n",
							line, to_chan);
	      continue;
	    }

	    t = split(NULL,1);		/* Take rest of line including spaces. */

	    if (t == NULL) {
	      t = " ";				/* Empty means permit nothing. */
	    }

	    p_cdigi_config->cfilter_str[from_chan][to_chan] = strdup(t);

//TODO1.2:  Do a test run to see errors now instead of waiting.

	  }


/*
 * ==================== APRStt gateway ==================== 
 */

/*
 * TTCORRAL 		- How to handle unknown positions
 *
 * TTCORRAL  latitude  longitude  offset-or-ambiguity 
 */

	  else if (strcasecmp(t, "TTCORRAL") == 0) {

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing latitude for TTCORRAL command.\n", line);
	      continue;
	    }
	    p_tt_config->corral_lat = parse_ll(t,LAT,line);

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing longitude for TTCORRAL command.\n", line);
	      continue;
	    }
	    p_tt_config->corral_lon = parse_ll(t,LON,line);

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing longitude for TTCORRAL command.\n", line);
	      continue;
	    }
	    p_tt_config->corral_offset = parse_ll(t,LAT,line);
	    if (p_tt_config->corral_offset == 1 ||
		p_tt_config->corral_offset == 2 ||
	 	p_tt_config->corral_offset == 3) {
	      p_tt_config->corral_ambiguity = p_tt_config->corral_offset;
	      p_tt_config->corral_offset = 0;
	    }

	    //dw_printf ("DEBUG: corral %f %f %f %d\n", p_tt_config->corral_lat,
	    //	p_tt_config->corral_lon, p_tt_config->corral_offset, p_tt_config->corral_ambiguity);
	  }

/*
 * TTPOINT 		- Define a point represented by touch tone sequence.
 *
 * TTPOINT   pattern  latitude  longitude   
 */
	  else if (strcasecmp(t, "TTPOINT") == 0) {

	    struct ttloc_s *tl;
	    int j;

	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    // Should make this a function/macro instead of repeating code.
	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_POINT;
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));
	    tl->point.lat = 0;
	    tl->point.lon = 0;

	    /* Pattern: B and digits */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTPOINT command.\n", line);
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTPOINT pattern must begin with upper case 'B'.\n", line);
	    }
	    for (j=1; j<(int)(strlen(t)); j++) {
	      if ( ! isdigit(t[j])) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTPOINT pattern must be B and digits only.\n", line);
	      }
	    }

	    /* Latitude */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing latitude for TTPOINT command.\n", line);
	      continue;
	    }
	    tl->point.lat = parse_ll(t,LAT,line);

	    /* Longitude */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing longitude for TTPOINT command.\n", line);
	      continue;
	    }
	    tl->point.lon = parse_ll(t,LON,line);

	    /* temp debugging */

	    //for (j=0; j<p_tt_config->ttloc_len; j++) {
	    //  dw_printf ("debug ttloc %d/%d %s\n", j, p_tt_config->ttloc_size, 
	    //		p_tt_config->ttloc_ptr[j].pattern);
	    //}
	  }

/*
 * TTVECTOR 		- Touch tone location with bearing and distance.
 *
 * TTVECTOR   pattern  latitude  longitude  scale  unit  
 */
	  else if (strcasecmp(t, "TTVECTOR") == 0) {

	    struct ttloc_s *tl;
	    int j;
	    double scale;
	    double meters;

	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_VECTOR;
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));
	    tl->vector.lat = 0;
	    tl->vector.lon = 0;
	    tl->vector.scale = 1;
	   
	    /* Pattern: B5bbbd... */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTVECTOR command.\n", line);
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTVECTOR pattern must begin with upper case 'B'.\n", line);
	    }
	    if (strncmp(t+1, "5bbb", 4) != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTVECTOR pattern would normally contain \"5bbb\".\n", line);
	    }
	    for (j=1; j<(int)(strlen(t)); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'b' && t[j] != 'd') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTVECTOR pattern must contain only B, digits, b, and d.\n", line);
	      }
	    }

	    /* Latitude */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing latitude for TTVECTOR command.\n", line);
	      continue;
	    }
	    tl->vector.lat = parse_ll(t,LAT,line);

	    /* Longitude */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing longitude for TTVECTOR command.\n", line);
	      continue;
	    }
	    tl->vector.lon = parse_ll(t,LON,line);

	    /* Longitude */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing scale for TTVECTOR command.\n", line);
	      continue;
	    }
	    scale = atof(t);

	    /* Unit. */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing unit for TTVECTOR command.\n", line);
	      continue;
	    }
	    meters = 0;
	    for (j=0; j<NUM_UNITS && meters == 0; j++) {
	      if (strcasecmp(units[j].name, t) == 0) {
	        meters = units[j].meters;
	      }
	    }
	    if (meters == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Unrecognized unit for TTVECTOR command.  Using miles.\n", line);
	      meters = 1609.344;
	    }
	    tl->vector.scale = scale * meters;

 	    //dw_printf ("ttvector: %f meters\n", tl->vector.scale);

	    /* temp debugging */

	    //for (j=0; j<p_tt_config->ttloc_len; j++) {
	    //  dw_printf ("debug ttloc %d/%d %s\n", j, p_tt_config->ttloc_size, 
	    //		p_tt_config->ttloc_ptr[j].pattern);
	    //}
	  }

/*
 * TTGRID 		- Define a grid for touch tone locations.
 *
 * TTGRID   pattern  min-latitude  min-longitude  max-latitude  max-longitude
 */
	  else if (strcasecmp(t, "TTGRID") == 0) {

	    struct ttloc_s *tl;
	    int j;

	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_GRID;
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));
	    tl->grid.lat0 = 0;
	    tl->grid.lon0 = 0;
	    tl->grid.lat9 = 0;
	    tl->grid.lon9 = 0;

	    /* Pattern: B [digit] x... y... */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTGRID command.\n", line);
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTGRID pattern must begin with upper case 'B'.\n", line);
	    }
	    for (j=1; j<(int)(strlen(t)); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'x' && t[j] != 'y') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTGRID pattern must be B, optional digit, xxx, yyy.\n", line);
	      }
	    }

	    /* Minimum Latitude - all zeros in received data */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing minimum latitude for TTGRID command.\n", line);
	      continue;
	    }
	    tl->grid.lat0 = parse_ll(t,LAT,line);

	    /* Minimum Longitude - all zeros in received data */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing minimum longitude for TTGRID command.\n", line);
	      continue;
	    }
	    tl->grid.lon0 = parse_ll(t,LON,line);

	    /* Maximum Latitude - all nines in received data */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing maximum latitude for TTGRID command.\n", line);
	      continue;
	    }
	    tl->grid.lat9 = parse_ll(t,LAT,line);

	    /* Maximum Longitude - all nines in received data */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing maximum longitude for TTGRID command.\n", line);
	      continue;
	    }
	    tl->grid.lon9 = parse_ll(t,LON,line);

	    /* temp debugging */

	    // dw_printf ("CONFIG TTGRID min %f %f\n", tl->grid.lat0, tl->grid.lon0);
	    // dw_printf ("CONFIG TTGRID max %f %f\n", tl->grid.lat9, tl->grid.lon9);

	    //for (j=0; j<p_tt_config->ttloc_len; j++) {
	    //  dw_printf ("debug ttloc %d/%d %s\n", j, p_tt_config->ttloc_size, 
	    //	p_tt_config->ttloc_ptr[j].pattern);
	    //}
	  }

/*
 * TTUTM 		- Specify UTM zone for touch tone locations.
 *
 * TTUTM   pattern  zone [ scale [ x-offset y-offset ] ]
 */
	  else if (strcasecmp(t, "TTUTM") == 0) {

	    struct ttloc_s *tl;
	    int j;
	    double dlat, dlon;
	    long lerr;


	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_UTM;
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));
	    tl->utm.lzone = 0;
	    tl->utm.scale = 1;
	    tl->utm.x_offset = 0;
	    tl->utm.y_offset = 0;

	    /* Pattern: B [digit] x... y... */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTUTM command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTUTM pattern must begin with upper case 'B'.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    for (j=1; j < (int)(strlen(t)); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'x' && t[j] != 'y') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTUTM pattern must be B, optional digit, xxx, yyy.\n", line);
		// Bail out somehow.  continue would match inner for.
	      }
	    }

	    /* Zone 1 - 60 and optional latitudinal letter. */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing zone for TTUTM command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    tl->utm.lzone = parse_utm_zone (t, &(tl->utm.latband), &(tl->utm.hemi));

 	    /* Optional scale. */

	    t = split(NULL,0);
	    if (t != NULL) {
	      
	      tl->utm.scale = atof(t);

	      /* Optional x offset. */

	      t = split(NULL,0);
	      if (t != NULL) {

	        tl->utm.x_offset = atof(t);

	        /* Optional y offset. */

	        t = split(NULL,0);
	        if (t != NULL) {
	     
	          tl->utm.y_offset = atof(t);
	        }
	      }
	    }

	    /* Practice run to see if conversion might fail later with actual location. */

	    lerr = Convert_UTM_To_Geodetic(tl->utm.lzone, tl->utm.hemi,               
                        tl->utm.x_offset + 5 * tl->utm.scale,
                        tl->utm.y_offset + 5 * tl->utm.scale,
                        &dlat, &dlon);

            if (lerr != 0) {
	      char message [300];

              utm_error_string (lerr, message);
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid UTM location: \n%s\n", line, message);
	      p_tt_config->ttloc_len--;
	      continue;
            }
	  }

/*
 * TTUSNG, TTMGRS 		- Specify zone/square for touch tone locations.
 *
 * TTUSNG   pattern  zone_square 
 * TTMGRS   pattern  zone_square 
 */
	  else if (strcasecmp(t, "TTUSNG") == 0 || strcasecmp(t, "TTMGRS") == 0) {

	    struct ttloc_s *tl;
	    int j;
	    int num_x, num_y;
	    double lat, lon;
	    long lerr;
	    char message[300];

	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);

// TODO1.2: in progress...
	    if (strcasecmp(t, "TTMGRS") == 0) {
	      tl->type = TTLOC_MGRS;
	    }
	    else {
	      tl->type = TTLOC_USNG;
	    }
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));
	    strlcpy(tl->mgrs.zone, "", sizeof(tl->mgrs.zone));

	    /* Pattern: B [digit] x... y... */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTUSNG/TTMGRS command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTUSNG/TTMGRS pattern must begin with upper case 'B'.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    num_x = 0;
	    num_y = 0;
	    for (j=1; j<(int)(strlen(t)); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'x' && t[j] != 'y') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTUSNG/TTMGRS pattern must be B, optional digit, xxx, yyy.\n", line);
		// Bail out somehow.  continue would match inner for.
	      }
	      if (t[j] == 'x') num_x++;
	      if (t[j] == 'y') num_y++;
	    }
	    if (num_x < 1 || num_x > 5 || num_x != num_y) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTUSNG/TTMGRS must have 1 to 5 x and same number y.\n", line);  
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    /* Zone 1 - 60 and optional latitudinal letter. */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing zone & square for TTUSNG/TTMGRS command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strlcpy (tl->mgrs.zone, t, sizeof(tl->mgrs.zone));

	    /* Try converting it rather do our own error checking. */

	    if (tl->type == TTLOC_MGRS) {
	      lerr = Convert_MGRS_To_Geodetic (tl->mgrs.zone, &lat, &lon);
	    }
	    else {
	      lerr = Convert_USNG_To_Geodetic (tl->mgrs.zone, &lat, &lon);
	    }
            if (lerr != 0) {

              mgrs_error_string (lerr, message);
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid USNG/MGRS zone & square:  %s\n%s\n", line, tl->mgrs.zone, message);
	      p_tt_config->ttloc_len--;
	      continue;
            }

	    /* Should be the end. */

	    t = split(NULL,0);
	    if (t != NULL) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unexpected stuff at end ignored:  %s\n", line, t);
	    }
	  }

/*
 * TTMHEAD 		- Define pattern to be used for Maidenhead Locator.
 *
 * TTMHEAD   pattern   [ prefix ] 
 *
 *			Pattern would be  B[0-9A-D]xxxx...	
 *			Optional prefix is 10, 6, or 4 digits.
 *
 *			The total number of digts in both must be 4, 6, 10, or 12.
 */
	  else if (strcasecmp(t, "TTMHEAD") == 0) {

// TODO1.3:  TTMHEAD needs testing. 

	    struct ttloc_s *tl;
	    int j;
	    int k;
	    int count_x;
	    int count_other;


	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len > 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_MHEAD;
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));
	    strlcpy(tl->mhead.prefix, "", sizeof(tl->mhead.prefix));

	    /* Pattern: B, optional additional button, some number of xxxx... for matching */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTMHEAD command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTMHEAD pattern must begin with upper case 'B'.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    /* Optionally one of 0-9ABCD */

	    if (strchr("ABCD", t[1]) != NULL || isdigit(t[1])) {
	      j = 2;
	    }
	    else {
	      j = 1;
	    }

	    count_x = 0;
	    count_other = 0;
	    for (k = j ; k < (int)(strlen(t)); k++) {
	      if (t[k] == 'x') count_x++;
	      else count_other++;
	    }

	    if (count_other != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTMHEAD must have only lower case x to match received data.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    // optional prefix

	    t = split(NULL,0);
	    if (t != NULL) {
	      char mh[30];

	      strlcpy(tl->mhead.prefix, t, sizeof(tl->mhead.prefix));

	      if (!alldigits(t) || (strlen(t) != 4 &&  strlen(t) != 6 && strlen(t) != 10)) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTMHEAD prefix must be 4, 6, or 10 digits.\n", line);
	        p_tt_config->ttloc_len--;
	        continue;
	      }
	      if (tt_mhead_to_text(t, 0, mh, sizeof(mh)) != 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTMHEAD prefix not a valid DTMF sequence.\n", line);
	        p_tt_config->ttloc_len--;
	        continue;
	      }      
	    }

	    k = strlen(tl->mhead.prefix) + count_x;

	    if (k != 4 && k != 6  && k != 10  && k != 12 ) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTMHEAD prefix and user data must have a total of 4, 6, 10, or 12 digits.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    
	  }


/*
 * TTSATSQ 		- Define pattern to be used for Satellite square.
 *
 * TTSATSQ   pattern    
 *
 *			Pattern would be  B[0-9A-D]xxxx 
 *			
 *			Must have exactly 4 x.
 */

	  else if (strcasecmp(t, "TTSATSQ") == 0) {

// TODO1.2:  TTSATSQ To be continued...

	    struct ttloc_s *tl;
	    int j;

	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len > 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_SATSQ;
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));
	    tl->point.lat = 0;
	    tl->point.lon = 0;

	    /* Pattern: B, optional additional button, exactly xxxx for matching */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTSATSQ command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTSATSQ pattern must begin with upper case 'B'.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    /* Optionally one of 0-9ABCD */

	    if (strchr("ABCD", t[1]) != NULL || isdigit(t[1])) {
	      j = 2;
	    }
	    else {
	      j = 1;
	    }

	    if (strcmp(t+j, "xxxx") != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTSATSQ pattern must end with exactly xxxx in lower case.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    /* temp debugging */

	    //for (j=0; j<p_tt_config->ttloc_len; j++) {
	    //  dw_printf ("debug ttloc %d/%d %s\n", j, p_tt_config->ttloc_size, 
	    //		p_tt_config->ttloc_ptr[j].pattern);
	    //}
	  }

/*
 * TTAMBIG 		- Define pattern to be used for Object Location Ambiguity.
 *
 * TTAMBIG   pattern
 *
 *			Pattern would be  B[0-9A-D]x
 *
 *			Must have exactly one x.
 */

	  else if (strcasecmp(t, "TTAMBIG") == 0) {

// TODO1.3:  TTAMBIG To be continued...

	    struct ttloc_s *tl;
	    int j;

	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len > 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_AMBIG;
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));

	    /* Pattern: B, optional additional button, exactly x for matching */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTAMBIG command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTAMBIG pattern must begin with upper case 'B'.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    /* Optionally one of 0-9ABCD */

	    if (strchr("ABCD", t[1]) != NULL || isdigit(t[1])) {
	      j = 2;
	    }
	    else {
	      j = 1;
	    }

	    if (strcmp(t+j, "x") != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTAMBIG pattern must end with exactly one x in lower case.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    /* temp debugging */

	    //for (j=0; j<p_tt_config->ttloc_len; j++) {
	    //  dw_printf ("debug ttloc %d/%d %s\n", j, p_tt_config->ttloc_size,
	    //		p_tt_config->ttloc_ptr[j].pattern);
	    //}
	  }


/*
 * TTMACRO 		- Define compact message format with full expansion
 *
 * TTMACRO   pattern  definition
 *
 *		pattern can contain:
 *			0-9 which must match exactly.
 *				In version 1.2, also allow A,B,C,D for exact match.
 *			x, y, z which are used for matching of variable fields.
 *			
 *		definition can contain:
 *			0-9, A, B, C, D, *, #, x, y, z.
 *			Not sure why # was included in there.
 *
 *	    new for version 1.3 - in progress
 *
 *			AA{objname}
 *			AB{symbol}
 *			AC{call}
 *
 *		These provide automatic conversion from plain text to the TT encoding.
 *		
 */
	  else if (strcasecmp(t, "TTMACRO") == 0) {

	    struct ttloc_s *tl;
	    int j;
	    int p_count[3], d_count[3];
	    int tt_error = 0;

	    assert (p_tt_config->ttloc_size >= 2);
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_MACRO;
	    strlcpy(tl->pattern, "", sizeof(tl->pattern));

	    /* Pattern: Any combination of digits, x, y, and z. */
	    /* Also make note of which letters are used in pattern and definition. */
 	    /* Version 1.2: also allow A,B,C,D in the pattern. */

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTMACRO command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strlcpy (tl->pattern, t, sizeof(tl->pattern));

	    p_count[0] = p_count[1] = p_count[2] = 0;

	    for (j=0; j<(int)(strlen(t)); j++) {
	      if ( strchr ("0123456789ABCDxyz", t[j]) == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTMACRO pattern can contain only digits, A, B, C, D, and lower case x, y, or z.\n", line);
	        p_tt_config->ttloc_len--;
	        continue;
	      }
	      /* Count how many x, y, z in the pattern. */
	      if (t[j] >= 'x' && t[j] <= 'z') {
		p_count[t[j]-'x']++;
	      }
	    }

	    //text_color_set(DW_COLOR_DEBUG);
	    //dw_printf ("Line %d: TTMACRO pattern \"%s\" p_count = %d %d %d.\n", line, t, p_count[0], p_count[1], p_count[2]);

	    /* Next we should find the definition. */
	    /* It can contain touch tone characters and lower case x, y, z for substitutions. */

	    t = split(NULL,1);;
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing definition for TTMACRO command.\n", line);
	      tl->macro.definition = "";	/* Don't die on null pointer later. */
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    /* Make a pass over the definition, looking for the xx{...} substitutions. */
	    /* These are done just once when reading the configuration file. */

	    char *pi;
	    char *ps;
	    char stemp[100];  // text inside of xx{...}
	    char ttemp[300];  // Converted to tone sequences.
	    char otemp[1000]; // Result after any substitutions.
	    char t2[2];

	    strlcpy (otemp, "", sizeof(otemp));
	    t2[1] = '\0';
	    pi = t;
	    while (*pi == ' ' || *pi == '\t') {
	      pi++;
	    }
	    for ( ; *pi != '\0'; pi++) {

	      if (strncmp(pi, "AC{", 3) == 0) {

		// Convert to fixed length 10 digit callsign.

	        pi += 3;
	        ps = stemp;
	        while (*pi != '}' && *pi != '*' && *pi != '\0') {
	          *ps++ = *pi++;
	        }
	        if (*pi == '}') {
	          *ps = '\0';
	          if (tt_text_to_call10 (stemp, 0, ttemp) == 0) {
	            //text_color_set(DW_COLOR_DEBUG);
	            //dw_printf ("DEBUG Line %d: AC{%s} -> AC%s\n", line, stemp, ttemp);
	            strlcat (otemp, "AC", sizeof(otemp));
	            strlcat (otemp, ttemp, sizeof(otemp));
	          }
	          else {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Line %d: AC{%s} could not be converted to tones for callsign.\n", line, stemp);
		    tt_error++;
	          }
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Line %d: AC{... is missing matching } in TTMACRO definition.\n", line);
		  tt_error++;
	        }
	      }

	      else if (strncmp(pi, "AA{", 3) == 0) {

		// Convert to object name.

	        pi += 3;
	        ps = stemp;
	        while (*pi != '}' && *pi != '*' && *pi != '\0') {
	          *ps++ = *pi++;
	        }
	        if (*pi == '}') {
	          *ps = '\0';
	          if (strlen(stemp) > 9) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Line %d: Object name %s has been truncated to 9 characters.\n", line, stemp);
	            stemp[9] = '\0';
	          }
	          if (tt_text_to_two_key (stemp, 0, ttemp) == 0) {
	            //text_color_set(DW_COLOR_DEBUG);
	            //dw_printf ("DEBUG Line %d: AA{%s} -> AA%s\n", line, stemp, ttemp);
	            strlcat (otemp, "AA", sizeof(otemp));
	            strlcat (otemp, ttemp, sizeof(otemp));
	          }
	          else {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Line %d: AA{%s} could not be converted to tones for object name.\n", line, stemp);
		    tt_error++;
	          }
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Line %d: AA{... is missing matching } in TTMACRO definition.\n", line);
		  tt_error++;
	        }
	      }

	      else if (strncmp(pi, "AB{", 3) == 0) {

		// Attempt conversion from description to symbol code.

	        pi += 3;
	        ps = stemp;
	        while (*pi != '}' && *pi != '*' && *pi != '\0') {
	          *ps++ = *pi++;
	        }
	        if (*pi == '}') {
	          char symtab;
	          char symbol;

	          *ps = '\0';

	          // First try to find something matching the description.

	          if (symbols_code_from_description (' ', stemp, &symtab, &symbol) == 0) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Line %d: Couldn't convert \"%s\" to APRS symbol code.  Using default.\n", line, stemp);
	            symtab = '\\';	// Alternate
	            symbol = 'A';	// Box
	          }

		  // Convert symtab(overlay) & symbol to tone sequence.

		  symbols_to_tones (symtab, symbol, ttemp, sizeof(ttemp));

	          //text_color_set(DW_COLOR_DEBUG);
	          //dw_printf ("DEBUG config file Line %d: AB{%s} -> %s\n", line, stemp, ttemp);

		  strlcat (otemp, ttemp, sizeof(otemp));
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Line %d: AB{... is missing matching } in TTMACRO definition.\n", line);
		  tt_error++;
	        }
	      }

	      else if (strncmp(pi, "CA{", 3) == 0) {

		// Convert to enhanced comment that can contain any ASCII character.

	        pi += 3;
	        ps = stemp;
	        while (*pi != '}' && *pi != '*' && *pi != '\0') {
	          *ps++ = *pi++;
	        }
	        if (*pi == '}') {
	          *ps = '\0';
	          if (tt_text_to_ascii2d (stemp, 0, ttemp) == 0) {
	            //text_color_set(DW_COLOR_DEBUG);
	            //dw_printf ("DEBUG Line %d: CA{%s} -> CA%s\n", line, stemp, ttemp);
	            strlcat (otemp, "CA", sizeof(otemp));
	            strlcat (otemp, ttemp, sizeof(otemp));
	          }
	          else {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Line %d: CA{%s} could not be converted to tones for enhanced comment.\n", line, stemp);
		    tt_error++;
	          }
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Line %d: CA{... is missing matching } in TTMACRO definition.\n", line);
		  tt_error++;
	        }
	      }


	      else if (strchr("0123456789ABCD*#xyz", *pi) != NULL) {
	        t2[0] = *pi;
	        strlcat (otemp, t2, sizeof(otemp));
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTMACRO definition can contain only 0-9, A, B, C, D, *, #, x, y, z.\n", line);
	        tt_error++;
	      }
	    } 

	    /* Make sure that number of x, y, z, in pattern and definition match. */

	    d_count[0] = d_count[1] = d_count[2] = 0;

	    for (j=0; j<(int)(strlen(otemp)); j++) {
	      if (otemp[j] >= 'x' && otemp[j] <= 'z') {
		d_count[otemp[j]-'x']++;
	      }
	    }

	    /* A little validity checking. */

	    for (j=0; j<3; j++) {
	      if (p_count[j] > 0 && d_count[j] == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: '%c' is in TTMACRO pattern but is not used in definition.\n", line, 'x'+j);
	      }
	      if (d_count[j] > 0 && p_count[j] == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: '%c' is referenced in TTMACRO definition but does not appear in the pattern.\n", line, 'x'+j);
	      }
	    }

	    //text_color_set(DW_COLOR_DEBUG);
	    //dw_printf ("DEBUG Config Line %d: %s -> %s\n", line, t, otemp);

	    if (tt_error == 0) {
	      tl->macro.definition = strdup(otemp);
	    }
	    else {
	      p_tt_config->ttloc_len--;
	    }
	  }

/*
 * TTOBJ 		- TT Object Report options.
 *
 * TTOBJ  recv-chan  where-to  [ via-path ] 
 *
 *	whereto is any combination of transmit channel, APP, IG.
 */


	  else if (strcasecmp(t, "TTOBJ") == 0) {
	    int r, x = -1;
	    int app = 0;
	    int ig = 0;
	    char *p;

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing DTMF receive channel for TTOBJ command.\n", line);
	      continue;
	    }

	    r = atoi(t);
	    if (r < 0 || r > MAX_CHANS-1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: DTMF receive channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }

	    // I suppose we need internal modem channel here.
	    // otherwise a DTMF decoder would not be available.

	    if (p_audio_config->chan_medium[r] != MEDIUM_RADIO) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TTOBJ DTMF receive channel %d is not valid.\n", 
							line, r);
	      continue;
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing transmit channel for TTOBJ command.\n", line);
	      continue;
	    }

	    // Can have any combination of number, APP, IG.  
    	    // Would it be easier with strtok?

	    for (p = t; *p != '\0'; p++) {

	      if (isdigit(*p)) {
	        x = *p - '0';
	        if (x < 0 || x > MAX_CHANS-1) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file: Transmit channel must be in range of 0 to %d on line %d.\n", MAX_CHANS-1, line);
	          x = -1;
	        }
	        else if (p_audio_config->chan_medium[x] != MEDIUM_RADIO &&
			 p_audio_config->chan_medium[x] != MEDIUM_NETTNC) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file, line %d: TTOBJ transmit channel %d is not valid.\n", line, x);
	          x = -1;
	        }
	      }
	      else if (*p == 'a' || *p == 'A') {
	        app = 1;
	      }
	      else if (*p == 'i' || *p == 'I') {
	        ig = 1;
	      }
	      else if (strchr("pPgG,", *p) != NULL) {
	        ;
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: Expected comma separated list with some combination of transmit channel, APP, and IG.\n", line);
	      }              
	    }

// This enables the DTMF decoder on the specified channel.
// Additional channels can be enabled with the DTMF command.
// Note that DTMF command does not enable the APRStt gateway.


	    //text_color_set(DW_COLOR_DEBUG);
	    //dw_printf ("Debug TTOBJ r=%d, x=%d, app=%d, ig=%d\n", r, x, app, ig);

	    p_audio_config->achan[r].dtmf_decode = DTMF_DECODE_ON;
	    p_tt_config->gateway_enabled = 1;
	    p_tt_config->obj_recv_chan = r;
	    p_tt_config->obj_xmit_chan = x;
	    p_tt_config->obj_send_to_app = app;
	    p_tt_config->obj_send_to_ig = ig;

	    t = split(NULL,0);
	    if (t != NULL) {

	      if (check_via_path(t) >= 0) {
	        strlcpy (p_tt_config->obj_xmit_via, t, sizeof(p_tt_config->obj_xmit_via));
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: invalid via path.\n", line);
	      }
	    }
	  }

/*
 * TTERR 		- TT responses for success or errors.
 *
 * TTERR  msg_id  method  text...  
 */

	  else if (strcasecmp(t, "TTERR") == 0) {
	    int n, msg_num;
	    char *p;
	    char method[AX25_MAX_ADDR_LEN];
	    int ssid;
	    int heard;


	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing message identifier for TTERR command.\n", line);
	      continue;
	    }
	    
	    msg_num = -1;
	    for (n=0; n<TT_ERROR_MAXP1; n++) {
	      if (strcasecmp(t, tt_msg_id[n]) == 0) {
	        msg_num = n;
	        break;
	      }
	    }
	    if (msg_num < 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Invalid message identifier for TTERR command.\n", line);	
		// pick one of ...
	      continue;
	    }

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing method (SPEECH, MORSE) for TTERR command.\n", line);
	      continue;
	    }

	    for (p=t; *p!= '\0'; p++) {
	      if (islower(*p)) *p = toupper(*p);
	    }

	    if ( ! ax25_parse_addr(-1, t, 1, method, &ssid, &heard)) {
	       continue;  // function above prints any error message
	    }

	    if (strcmp(method,"MORSE") != 0 && strcmp(method,"SPEECH") != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Response method of %s must be SPEECH or MORSE for TTERR command.\n", line, method);
	      continue;
	    }

	    t = split(NULL,1);;
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing response text for TTERR command.\n", line);
	      continue;
	    }

  	    //text_color_set(DW_COLOR_DEBUG);
	    //dw_printf ("Line %d: TTERR debug %d %s-%d \"%s\"\n", line, msg_num, method, ssid, t);
 
	    assert (msg_num >= 0 && msg_num < TT_ERROR_MAXP1);

	    strlcpy (p_tt_config->response[msg_num].method, method, sizeof(p_tt_config->response[msg_num].method));

// TODO1.3: Need SSID too!

	    strlcpy (p_tt_config->response[msg_num].mtext, t, sizeof(p_tt_config->response[msg_num].mtext));
	    p_tt_config->response[msg_num].mtext[TT_MTEXT_LEN-1] = '\0';

	  }

/*
 * TTSTATUS 		- TT custom status messages.
 *
 * TTSTATUS  status_id  text...  
 */

	  else if (strcasecmp(t, "TTSTATUS") == 0) {
	    int status_num;

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing status number for TTSTATUS command.\n", line);
	      continue;
	    }
	    
	    status_num = atoi(t);

	    if (status_num < 1 || status_num > 9) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Status number for TTSTATUS command must be in range of 1 to 9.\n", line);	
	      continue;
	    }

	    t = split(NULL,1);;
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing status text for TTSTATUS command.\n", line);
	      continue;
	    }

  	    //text_color_set(DW_COLOR_DEBUG);
	    //dw_printf ("Line %d: TTSTATUS debug %d \"%s\"\n", line, status_num, t);
 
	    while (*t == ' ' || *t == '\t') t++;   // remove leading white space.

	    strlcpy (p_tt_config->status[status_num], t, sizeof(p_tt_config->status[status_num]));
	  }


/*
 * TTCMD 		- Command to run when valid sequence is received.
 *			  Any text generated will be sent back to user.
 *
 * TTCMD ...  
 */

	  else if (strcasecmp(t, "TTCMD") == 0) {

	    t = split(NULL,1);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing command for TTCMD command.\n", line);
	      continue;
	    }
	    
	    strlcpy (p_tt_config->ttcmd, t, sizeof(p_tt_config->ttcmd));
	  }


/*
 * ==================== Internet gateway ==================== 
 */

/*
 * IGSERVER 		- Name of IGate server.
 *
 * IGSERVER  hostname [ port ] 				-- original implementation.
 *
 * IGSERVER  hostname:port				-- more in line with usual conventions.
 */

	  else if (strcasecmp(t, "IGSERVER") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing IGate server name for IGSERVER command.\n", line);
	      continue;
	    }
	    strlcpy (p_igate_config->t2_server_name, t, sizeof(p_igate_config->t2_server_name));

	    /* If there is a : in the name, split it out as the port number. */

	    t = strchr (p_igate_config->t2_server_name, ':');
	    if (t != NULL) {
	      *t = '\0';
	      t++;
	      int n = atoi(t);
              if (n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) {
	        p_igate_config->t2_server_port = n;
	      }
	      else {
	        p_igate_config->t2_server_port = DEFAULT_IGATE_PORT;
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Invalid port number for IGate server. Using default %d.\n", 
			line, p_igate_config->t2_server_port);
   	      }
	    }

	    /* Alternatively, the port number could be separated by white space. */
	    
	    t = split(NULL,0);
	    if (t != NULL) {
	      int n = atoi(t);
              if (n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) {
	        p_igate_config->t2_server_port = n;
	      }
	      else {
	        p_igate_config->t2_server_port = DEFAULT_IGATE_PORT;
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Invalid port number for IGate server. Using default %d.\n", 
			line, p_igate_config->t2_server_port);
   	      }
	    }
	    //dw_printf ("DEBUG  server=%s   port=%d\n", p_igate_config->t2_server_name, p_igate_config->t2_server_port);
	    //exit (0);
	  }

/*
 * IGLOGIN 		- Login callsign and passcode for IGate server
 *
 * IGLOGIN  callsign  passcode
 */

	  else if (strcasecmp(t, "IGLOGIN") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing login callsign for IGLOGIN command.\n", line);
	      continue;
	    }
	    // TODO: Wouldn't hurt to do validity checking of format.
	    strlcpy (p_igate_config->t2_login, t, sizeof(p_igate_config->t2_login));

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing passcode for IGLOGIN command.\n", line);
	      continue;
	    }
	    strlcpy (p_igate_config->t2_passcode, t, sizeof(p_igate_config->t2_passcode));
	  }

/*
 * IGTXVIA 		- Transmit channel and VIA path for messages from IGate server
 *
 * IGTXVIA  channel  [ path ]
 */

	  else if (strcasecmp(t, "IGTXVIA") == 0) {
	    int n;

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing transmit channel for IGTXVIA command.\n", line);
	      continue;
	    }

	    n = atoi(t);
	    if (n < 0 || n > MAX_CHANS-1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Transmit channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }
	    p_igate_config->tx_chan = n;

	    t = split(NULL,0);
	    if (t != NULL) {

#if 1	// proper checking

	      n = check_via_path(t);
	      if (n >= 0) {
	        p_igate_config->max_digi_hops = n;
	        p_igate_config->tx_via[0] = ',';
	        strlcpy (p_igate_config->tx_via + 1, t, sizeof(p_igate_config->tx_via)-1);
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: invalid via path.\n", line);
	      }

#else	// previously

	      char *p;
	      p_igate_config->tx_via[0] = ',';
	      strlcpy (p_igate_config->tx_via + 1, t, sizeof(p_igate_config->tx_via)-1);
	      for (p = p_igate_config->tx_via; *p != '\0'; p++) {
	        if (islower(*p)) {
		  *p = toupper(*p);	/* silently force upper case. */
	        }
	      }
#endif
	    }
	  }

/*
 * IGFILTER 		- IGate Server side filters.
 *			  Is this name too confusing.  Too similar to FILTER IG 0 ...
 *			  Maybe SSFILTER suggesting Server Side.
 *			  SUBSCRIBE might be better because it's not a filter that limits.
 *
 * IGFILTER  filter-spec ... 
 */

	  else if (strcasecmp(t, "IGFILTER") == 0) {

	    t = split(NULL,1);		/* Take rest of line as one string. */

	    if (p_igate_config->t2_filter != NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Warning - Earlier IGFILTER value will be replaced by this one.\n", line);
	      continue;
	    }

	    if (t != NULL && strlen(t) > 0) {
	      p_igate_config->t2_filter = strdup (t);
	    }
	  }


/*
 * IGTXLIMIT 		- Limit transmissions during 1 and 5 minute intervals.
 *
 * IGTXLIMIT  one-minute-limit  five-minute-limit
 */

	  else if (strcasecmp(t, "IGTXLIMIT") == 0) {
	    int n;

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing one minute limit for IGTXLIMIT command.\n", line);
	      continue;
	    }
	    
	    n = atoi(t);
            if (n < 1) {
	      p_igate_config->tx_limit_1 = 1;
	    }
            else if (n <= IGATE_TX_LIMIT_1_MAX) {
	      p_igate_config->tx_limit_1 = n;
	    }
	    else {
	      p_igate_config->tx_limit_1 = IGATE_TX_LIMIT_1_MAX;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: One minute transmit limit has been reduced to %d.\n",
				line, p_igate_config->tx_limit_1);
	      dw_printf ("You won't make friends by setting a limit this high.\n");
   	    }


	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing five minute limit for IGTXLIMIT command.\n", line);
	      continue;
	    }
	    
	    n = atoi(t);
            if (n < 1) {
	      p_igate_config->tx_limit_5 = 1;
	    }
            else if (n <= IGATE_TX_LIMIT_5_MAX) {
	      p_igate_config->tx_limit_5 = n;
	    }
	    else {
	      p_igate_config->tx_limit_5 = IGATE_TX_LIMIT_5_MAX;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Five minute transmit limit has been reduced to %d.\n",
				line, p_igate_config->tx_limit_5);
	      dw_printf ("You won't make friends by setting a limit this high.\n");
   	    }
	  }


/*
 * IGMSP 		- Number of times to send position of message sender.
 *
 * IGMSP  n
 */

	  else if (strcasecmp(t, "IGMSP") == 0) {

	    t = split(NULL,0);
	    if (t != NULL) {

	      int n = atoi(t);
              if (n >= 0 && n <= 10) {
	        p_igate_config->igmsp = n;
	      }
	      else {
	        p_igate_config->igmsp = 1;
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Unreasonable number of times for message sender position.  Using default 1.\n", line);
	      }
	    }
	    else {
	      p_igate_config->igmsp = 1;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Missing number of times for message sender position.  Using default 1.\n", line);
	    }
	  }



/*
 * SATGATE 		- Special SATgate mode to delay packets heard directly.
 *
 * SATGATE [ n ]
 */

	  else if (strcasecmp(t, "SATGATE") == 0) {

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Line %d: SATGATE is pretty useless and will be removed in a future version.\n", line);

	    t = split(NULL,0);
	    if (t != NULL) {

	      int n = atoi(t);
              if (n >= MIN_SATGATE_DELAY && n <= MAX_SATGATE_DELAY) {
	        p_igate_config->satgate_delay = n;
	      }
	      else {
	        p_igate_config->satgate_delay = DEFAULT_SATGATE_DELAY;
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Unreasonable SATgate delay.  Using default.\n", line);
	      }
	    }
	    else {
	      p_igate_config->satgate_delay = DEFAULT_SATGATE_DELAY;
	    }
	  }



/*
 * ==================== All the left overs ==================== 
 */

/*
 * AGWPORT 		- Port number for "AGW TCPIP Socket Interface" 
 *
 * In version 1.2 we allow 0 to disable listening.
 */

	  else if (strcasecmp(t, "AGWPORT") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing port number for AGWPORT command.\n", line);
	      continue;
	    }
	    n = atoi(t);
	    t = split(NULL,0);
	    if (t != NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Unexpected \"%s\" after the port number.\n", line, t);
	      dw_printf ("Perhaps you were trying to use feature available only with KISSPORT.\n");
	      continue;
	    }
            if ((n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) || n == 0) {
	      p_misc_config->agwpe_port = n;
	    }
	    else {
	      p_misc_config->agwpe_port = DEFAULT_AGWPE_PORT;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid port number for AGW TCPIP Socket Interface. Using %d.\n", 
			line, p_misc_config->agwpe_port);
   	    }
	  }

/*
 * KISSPORT port [ chan ]		- Port number for KISS over IP.
 */

	// Previously we allowed only a single TCP port for KISS.
	// An increasing number of people want to run multiple radios.
	// Unfortunately, most applications don't know how to deal with multi-radio TNCs.
	// They ignore the channel on receive and always transmit to channel 0.
	// Running multiple instances of direwolf is a work-around but this leads to
	// more complex configuration and we lose the cross-channel digipeating capability.
	// In release 1.7 we add a new feature to assign a single radio channel to a TCP port.
	// e.g.
	//	KISSPORT 8001		# default, all channels.  Radio channel = KISS channel.
	//
	//	KISSPORT 7000 0		# Only radio channel 0 for receive.
	//				# Transmit to radio channel 0, ignoring KISS channel.
	//
	//	KISSPORT 7001 1		# Only radio channel 1 for receive.  KISS channel set to 0.
	//				# Transmit to radio channel 1, ignoring KISS channel.

// FIXME
	  else if (strcasecmp(t, "KISSPORT") == 0) {
	    int n;
	    int tcp_port = 0;
	    int chan = -1;	// optional.  default to all if not specified.
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing TCP port number for KISSPORT command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if ((n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) || n == 0) {
	      tcp_port = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid TCP port number for KISS TCPIP Socket Interface.\n", line);
              dw_printf ("Use something in the range of %d to %d.\n", MIN_IP_PORT_NUMBER, MAX_IP_PORT_NUMBER);
	      continue;
   	    }

	    t = split(NULL,0);
	    if (t != NULL) {
	      chan = atoi(t);
	      if (chan < 0 || chan >= MAX_CHANS) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Invalid channel %d for KISSPORT command.  Must be in range 0 thru %d.\n", line, chan, MAX_CHANS-1);
	        continue;
	      }
	    }

	    // "KISSPORT 0" is used to remove the default entry.

	    if (tcp_port == 0) {
	      p_misc_config->kiss_port[0] = 0;		// Should all be wiped out?
	    }
	    else {

	      // Try to find an empty slot.
	      // A duplicate TCP port number will overwrite the previous value.

	      int slot = -1;
	      for (int i = 0; i < MAX_KISS_TCP_PORTS && slot == -1; i++) {
	        if (p_misc_config->kiss_port[i] == tcp_port) {
	          slot = i;
	          if ( ! (slot == 0 && tcp_port == DEFAULT_KISS_PORT)) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Line %d: Warning: Duplicate TCP port %d will overwrite previous value.\n", line, tcp_port);
	          }
	        }
	        else if (p_misc_config->kiss_port[i] == 0) {
	          slot = i;
	        }
	      }
	      if (slot >= 0) {
	        p_misc_config->kiss_port[slot] = tcp_port;
	        p_misc_config->kiss_chan[slot] = chan;
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Too many KISSPORT commands.\n", line);
	      }
	    }
	  }

/*
 * NULLMODEM name [ speed ]	- Device name for serial port or our end of the virtual "null modem"
 * SERIALKISS name  [ speed ]
 *
 * Version 1.5:  Added SERIALKISS which is equivalent to NULLMODEM.
 * The original name sort of made sense when it was used only for one end of a virtual
 * null modem cable on Windows only.  Now it is also available for Linux.
 * TODO1.5: In retrospect, this doesn't seem like such a good name.
 */

	  else if (strcasecmp(t, "NULLMODEM") == 0 || strcasecmp(t, "SERIALKISS") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing serial port name on line %d.\n", line);
	      continue;
	    }
	    else {
	      if (strlen(p_misc_config->kiss_serial_port) > 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Warning serial port name on line %d replaces earlier value.\n", line);
	      }
	      strlcpy (p_misc_config->kiss_serial_port, t, sizeof(p_misc_config->kiss_serial_port));
	      p_misc_config->kiss_serial_speed = 0;
	      p_misc_config->kiss_serial_poll = 0;
	    }

	    t = split(NULL,0);
	    if (t != NULL) {
	      p_misc_config->kiss_serial_speed = atoi(t);
	    }
	  }

/*
 * SERIALKISSPOLL name		- Poll for serial port name that might come and go.
 *			  	  e.g. /dev/rfcomm0 for bluetooth.
 */

	  else if (strcasecmp(t, "SERIALKISSPOLL") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing serial port name on line %d.\n", line);
	      continue;
	    }
	    else {
	      if (strlen(p_misc_config->kiss_serial_port) > 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Warning serial port name on line %d replaces earlier value.\n", line);
	      }
	      strlcpy (p_misc_config->kiss_serial_port, t, sizeof(p_misc_config->kiss_serial_port));
	      p_misc_config->kiss_serial_speed = 0;
	      p_misc_config->kiss_serial_poll = 1;	// set polling.
	    }
	  }


/*
 * KISSCOPY 		- Data from network KISS client is copied to all others.
 *			  This does not apply to pseudo terminal KISS.
 */

	  else if (strcasecmp(t, "KISSCOPY") == 0) {
	    p_misc_config->kiss_copy = 1;
	  }


/*
 * DNSSD 		- Enable or disable (1/0) dns-sd, DNS Service Discovery announcements
 * DNSSDNAME            - Set DNS-SD service name, defaults to "Dire Wolf on <hostname>"
 */

	  else if (strcasecmp(t, "DNSSD") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing integer value for DNSSD command.\n", line);
	      continue;
	    }
	    n = atoi(t);
	    if (n == 0 || n == 1) {
	      p_misc_config->dns_sd_enabled = n;
	    } else {
	      p_misc_config->dns_sd_enabled = 0;
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Invalid integer value for DNSSD. Disabling dns-sd.\n", line);
	    }
	  }

	  else if (strcasecmp(t, "DNSSDNAME") == 0) {
	    t = split(NULL, 1);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing service name for DNSSDNAME.\n", line);
	      continue;
	    }
	    else {
	      strlcpy(p_misc_config->dns_sd_name, t, sizeof(p_misc_config->dns_sd_name));
	    }
	  }



/*
 * GPSNMEA  serial-device  [ speed ]		- Direct connection to GPS receiver.
 */
	  else if (strcasecmp(t, "gpsnmea") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Missing serial port name for GPS receiver.\n", line);
	      continue;
	    }
	    strlcpy (p_misc_config->gpsnmea_port, t, sizeof(p_misc_config->gpsnmea_port));

	    t = split(NULL,0);
	    if (t != NULL) {
	      int n = atoi(t);
	      p_misc_config->gpsnmea_speed = n;
	    }
	    else {
	      p_misc_config->gpsnmea_speed = 4800;	// The standard at one time.
	    }
	  }

/*
 * GPSD		- Use GPSD server.
 *
 * GPSD [ host [ port ] ]
 */
	  else if (strcasecmp(t, "gpsd") == 0) {

#if __WIN32__

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: The GPSD interface is not available for Windows.\n", line);
	    continue;

#elif ENABLE_GPSD

	    strlcpy (p_misc_config->gpsd_host, "localhost", sizeof(p_misc_config->gpsd_host));
	    p_misc_config->gpsd_port = atoi(DEFAULT_GPSD_PORT);

	    t = split(NULL,0);
	    if (t != NULL) {
	      strlcpy (p_misc_config->gpsd_host, t, sizeof(p_misc_config->gpsd_host));

	      t = split(NULL,0);
	      if (t != NULL) {

	        int n = atoi(t);
                if ((n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) || n == 0) {
	          p_misc_config->gpsd_port = n;
	        }
	        else {
	          p_misc_config->gpsd_port = atoi(DEFAULT_GPSD_PORT);
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: Invalid port number for GPSD Socket Interface. Using default of %d.\n", 
			line, p_misc_config->gpsd_port);
	        }
	      }
	    }
#else
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: The GPSD interface has not been enabled.\n", line);
	    dw_printf ("Install gpsd and libgps-dev packages then rebuild direwolf.\n");
	    continue;
#endif

	  }

/*
 * WAYPOINT		- Generate WPL and AIS NMEA sentences for display on map.
 *
 * WAYPOINT  serial-device [ formats ]
 * WAYPOINT  host:udpport [ formats ]
 *		  
 */
	  else if (strcasecmp(t, "waypoint") == 0) {

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing output device for WAYPOINT on line %d.\n", line);
	      continue;
	    }

	    /* If there is a ':' in the name, split it into hostname:udpportnum. */
	    /* Otherwise assume it is serial port name. */

	    char *p = strchr (t, ':');
	    if (p != NULL) {
	      *p = '\0';
	      int n = atoi(p+1);
              if (n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) {
	        strlcpy (p_misc_config->waypoint_udp_hostname, t, sizeof(p_misc_config->waypoint_udp_hostname));
	        if (strlen(p_misc_config->waypoint_udp_hostname) == 0) {
	          strlcpy (p_misc_config->waypoint_udp_hostname, "localhost", sizeof(p_misc_config->waypoint_udp_hostname));
	        }
	        p_misc_config->waypoint_udp_portnum = n;
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Invalid UDP port number %d for sending waypoints.\n", line, n);
	      }
	    }
	    else {
	      strlcpy (p_misc_config->waypoint_serial_port, t, sizeof(p_misc_config->waypoint_serial_port));
	    }
	
	    /* Anything remaining is the formats to enable. */

	    t = split(NULL,1);
	    if (t != NULL) {
	      for ( ; *t != '\0' ; t++ ) {
	        switch (toupper(*t)) {
	          case 'N':
	            p_misc_config->waypoint_formats |= WPL_FORMAT_NMEA_GENERIC;
	            break;
	          case 'G':
	            p_misc_config->waypoint_formats |= WPL_FORMAT_GARMIN;
	            break;
	          case 'M':
	            p_misc_config->waypoint_formats |= WPL_FORMAT_MAGELLAN;
	            break;
	          case 'K':
	            p_misc_config->waypoint_formats |= WPL_FORMAT_KENWOOD;
	            break;
	          case 'A':
	            p_misc_config->waypoint_formats |= WPL_FORMAT_AIS;
	            break;
	          case ' ':
	          case ',':
	            break;
	          default:
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Config file: Invalid output format '%c' for WAYPOINT on line %d.\n", *t, line);   
	            break;
	        }
	      }
	    }
	  }

/*
 * LOGDIR	- Directory name for automatically named daily log files.  Use "." for current working directory.
 */
	  else if (strcasecmp(t, "logdir") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing directory name for LOGDIR on line %d.\n", line);
	      continue;
	    }
	    else {
	      if (strlen(p_misc_config->log_path) > 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: LOGDIR on line %d is replacing an earlier LOGDIR or LOGFILE.\n", line);
	      }
	      p_misc_config->log_daily_names = 1;
	      strlcpy (p_misc_config->log_path, t, sizeof(p_misc_config->log_path));
	    }
	    t = split(NULL,0);
	    if (t != NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: LOGDIR on line %d should have directory path and nothing more.\n", line);
	    }
	  }

/*
 * LOGFILE	- Log file name, including any directory part.
 */
	  else if (strcasecmp(t, "logfile") == 0) {
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing file name for LOGFILE on line %d.\n", line);
	      continue;
	    }
	    else {
	      if (strlen(p_misc_config->log_path) > 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: LOGFILE on line %d is replacing an earlier LOGDIR or LOGFILE.\n", line);
	      }
	      p_misc_config->log_daily_names = 0;
	      strlcpy (p_misc_config->log_path, t, sizeof(p_misc_config->log_path));
	    }
	    t = split(NULL,0);
	    if (t != NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: LOGFILE on line %d should have file name and nothing more.\n", line);
	    }
	  }

/*
 * BEACON channel delay every message
 *
 * Original handcrafted style.  Removed in version 1.0.
 */

	  else if (strcasecmp(t, "BEACON") == 0) {
	    	    
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: Old style 'BEACON' has been replaced with new commands.\n", line);
	    dw_printf ("Use PBEACON, OBEACON, TBEACON, or CBEACON instead.\n");
  
	  }


/*
 * PBEACON keyword=value ...
 * OBEACON keyword=value ...
 * TBEACON keyword=value ...
 * CBEACON keyword=value ...
 * IBEACON keyword=value ...
 *
 * New style with keywords for options.
 */

// TODO: maybe add proportional pathing so multiple beacon timing does not need to be manually constructed?
// http://www.aprs.org/newN/ProportionalPathing.txt

	  else if (strcasecmp(t, "PBEACON") == 0 ||
		   strcasecmp(t, "OBEACON") == 0 ||
		   strcasecmp(t, "TBEACON") == 0 ||
		   strcasecmp(t, "CBEACON") == 0 ||
		   strcasecmp(t, "IBEACON") == 0) {

	    if (p_misc_config->num_beacons < MAX_BEACONS) {

	      memset (&(p_misc_config->beacon[p_misc_config->num_beacons]), 0, sizeof(struct beacon_s));
	      if (strcasecmp(t, "PBEACON") == 0) {
	        p_misc_config->beacon[p_misc_config->num_beacons].btype = BEACON_POSITION;
	      }
	      else if (strcasecmp(t, "OBEACON") == 0) {
	        p_misc_config->beacon[p_misc_config->num_beacons].btype = BEACON_OBJECT;
	      }
	      else if (strcasecmp(t, "TBEACON") == 0) {
	        p_misc_config->beacon[p_misc_config->num_beacons].btype = BEACON_TRACKER;
	      }
	      else if (strcasecmp(t, "IBEACON") == 0) {
	        p_misc_config->beacon[p_misc_config->num_beacons].btype = BEACON_IGATE;
	      }
	      else {
	        p_misc_config->beacon[p_misc_config->num_beacons].btype = BEACON_CUSTOM;
	      }

	      /* Save line number because some errors will be reported later. */
	      p_misc_config->beacon[p_misc_config->num_beacons].lineno = line;

	      if (beacon_options(t + strlen("xBEACON") + 1, &(p_misc_config->beacon[p_misc_config->num_beacons]), line, p_audio_config)) {
	        p_misc_config->num_beacons++;
	      }
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Maximum number of beacons exceeded on line %d.\n", line);
	      continue;
	    }
	  }


/*
 * SMARTBEACONING [ fast_speed fast_rate slow_speed slow_rate turn_time turn_angle turn_slope ]
 *
 * Parameters must be all or nothing.
 */

	  else if (strcasecmp(t, "SMARTBEACON") == 0 ||	
	           strcasecmp(t, "SMARTBEACONING") == 0) {

	    int n;

#define SB_NUM(name,sbvar,minn,maxx,unit)  							\
	    t = split(NULL,0);									\
	    if (t == NULL) {									\
	      if (strcmp(name, "fast speed") == 0) {						\
	        p_misc_config->sb_configured = 1;						\
	        continue;									\
	      }											\
	      text_color_set(DW_COLOR_ERROR);							\
	      dw_printf ("Line %d: Missing %s for SmartBeaconing.\n", line, name);		\
	      continue;										\
	    }											\
	    n = atoi(t);									\
            if (n >= minn && n <= maxx) {							\
	      p_misc_config->sbvar = n;								\
	    }											\
	    else {										\
	      text_color_set(DW_COLOR_ERROR);							\
              dw_printf ("Line %d: Invalid %s for SmartBeaconing. Using default %d %s.\n",	\
			line, name, p_misc_config->sbvar, unit);				\
   	    }

#define SB_TIME(name,sbvar,minn,maxx,unit)  							\
	    t = split(NULL,0);									\
	    if (t == NULL) {									\
	      text_color_set(DW_COLOR_ERROR);							\
	      dw_printf ("Line %d: Missing %s for SmartBeaconing.\n", line, name);		\
	      continue;										\
	    }											\
	    n = parse_interval(t,line);								\
            if (n >= minn && n <= maxx) {							\
	      p_misc_config->sbvar = n;								\
	    }											\
	    else {										\
	      text_color_set(DW_COLOR_ERROR);							\
              dw_printf ("Line %d: Invalid %s for SmartBeaconing. Using default %d %s.\n",	\
			line, name, p_misc_config->sbvar, unit);				\
   	    }


	    SB_NUM  ("fast speed", sb_fast_speed,  2,   90,  "MPH")
	    SB_TIME ("fast rate",  sb_fast_rate,  10,  300,  "seconds")

	    SB_NUM  ("slow speed", sb_slow_speed,  1,   30,  "MPH")
	    SB_TIME ("slow rate",  sb_slow_rate,  30, 3600,  "seconds")

	    SB_TIME ("turn time",  sb_turn_time,   5,  180,  "seconds")
	    SB_NUM  ("turn angle", sb_turn_angle,  5,   90,  "degrees")
	    SB_NUM  ("turn slope", sb_turn_slope,  1,  255,  "deg*mph")

	    /* If I was ambitious, I might allow optional */
	    /* unit at end for miles or km / hour. */

	    p_misc_config->sb_configured = 1;
	  }


/*
 * ==================== AX.25 connected mode ====================
 */


/*
 * FRACK  n 		- Number of seconds to wait for ack to transmission.
 */

	  else if (strcasecmp(t, "FRACK") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing value for FRACK.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= AX25_T1V_FRACK_MIN && n <= AX25_T1V_FRACK_MAX) {
	      p_misc_config->frack = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid FRACK time. Using default %d.\n", line, p_misc_config->frack);
	    }
	  }


/*
 * RETRY  n 		- Number of times to retry before giving up.
 */

	  else if (strcasecmp(t, "RETRY") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing value for RETRY.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= AX25_N2_RETRY_MIN && n <= AX25_N2_RETRY_MAX) {
	      p_misc_config->retry = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid RETRY number. Using default %d.\n", line, p_misc_config->retry);
	    }
	  }


/*
 * PACLEN  n 		- Maximum number of bytes in information part.
 */

	  else if (strcasecmp(t, "PACLEN") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing value for PACLEN.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= AX25_N1_PACLEN_MIN && n <= AX25_N1_PACLEN_MAX) {
	      p_misc_config->paclen = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid PACLEN value. Using default %d.\n", line, p_misc_config->paclen);
	    }
	  }


/*
 * MAXFRAME  n 		- Max frames to send before ACK.  mod 8 "Window" size.
 *
 * Window size would make more sense but everyone else calls it MAXFRAME.
 */

	  else if (strcasecmp(t, "MAXFRAME") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing value for MAXFRAME.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= AX25_K_MAXFRAME_BASIC_MIN && n <= AX25_K_MAXFRAME_BASIC_MAX) {
	      p_misc_config->maxframe_basic = n;
	    }
	    else {
	      p_misc_config->maxframe_basic = AX25_K_MAXFRAME_BASIC_DEFAULT;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid MAXFRAME value outside range of %d to %d. Using default %d.\n",
			line, AX25_K_MAXFRAME_BASIC_MIN, AX25_K_MAXFRAME_BASIC_MAX, p_misc_config->maxframe_basic);
	    }
	  }



/*
 * EMAXFRAME  n 		- Max frames to send before ACK.  mod 128 "Window" size.
 */

	  else if (strcasecmp(t, "EMAXFRAME") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing value for EMAXFRAME.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= AX25_K_MAXFRAME_EXTENDED_MIN && n <= AX25_K_MAXFRAME_EXTENDED_MAX) {
	      p_misc_config->maxframe_extended = n;
	    }
	    else {
	      p_misc_config->maxframe_extended = AX25_K_MAXFRAME_EXTENDED_DEFAULT;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid EMAXFRAME value outside of range %d to %d. Using default %d.\n",
			line, AX25_K_MAXFRAME_EXTENDED_MIN, AX25_K_MAXFRAME_EXTENDED_MAX, p_misc_config->maxframe_extended);
   	    }
	  }

/*
 * MAXV22  n 		- Max number of SABME sent before trying SABM.
 */

	  else if (strcasecmp(t, "MAXV22") == 0) {
	    int n;
	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing value for MAXV22.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n <= AX25_N2_RETRY_MAX) {
	      p_misc_config->maxv22 = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid MAXV22 number. Will use half of RETRY.\n", line);
	    }
	  }


/*
 * V20  address [ address ... ] 	- Stations known to support only AX.25 v2.0.
 *					  When connecting to these, skip SABME and go right to SABM.
 *					  Possible to have multiple and they are cumulative.
 */

	  else if (strcasecmp(t, "V20") == 0) {

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing address(es) for V20.\n", line);
	      continue;
	    }

	    while (t != NULL) {
	      int const strict = 2;
	      char call_no_ssid[AX25_MAX_ADDR_LEN];
	      int ssid, heard;

	      if (ax25_parse_addr (AX25_DESTINATION, t, strict, call_no_ssid, &ssid, &heard)) {
	        p_misc_config->v20_addrs = (char**)realloc (p_misc_config->v20_addrs, sizeof(char*) * (p_misc_config->v20_count + 1));
	        p_misc_config->v20_addrs[p_misc_config->v20_count++] = strdup(t);
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Invalid station address for V20 command.\n", line);

	        // continue processing any others following.
	      }
	      t = split(NULL,0);
	    }
	  }


/*
 * NOXID  address [ address ... ] 	- Stations known not to understand XID.
 *					  After connecting to these (with v2.2 obviously), don't try using XID command.
 *					  AX.25 for Linux is the one known case so far.
 *					  Possible to have multiple and they are cumulative.
 */

	  else if (strcasecmp(t, "NOXID") == 0) {

	    t = split(NULL,0);
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing address(es) for NOXID.\n", line);
	      continue;
	    }

	    while (t != NULL) {
	      int const strict = 2;
	      char call_no_ssid[AX25_MAX_ADDR_LEN];
	      int ssid, heard;

	      if (ax25_parse_addr (AX25_DESTINATION, t, strict, call_no_ssid, &ssid, &heard)) {
	        p_misc_config->noxid_addrs = (char**)realloc (p_misc_config->noxid_addrs, sizeof(char*) * (p_misc_config->noxid_count + 1));
	        p_misc_config->noxid_addrs[p_misc_config->noxid_count++] = strdup(t);
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Invalid station address for NOXID command.\n", line);

	        // continue processing any others following.
	      }
	      t = split(NULL,0);
	    }
	  }


/*
 * Invalid command.
 */
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file: Unrecognized command '%s' on line %d.\n", t, line);
	  }  

	}

	fclose (fp);

/*
 * A little error checking for option interactions.
 */

/*
 * Require that MYCALL be set when digipeating or IGating.
 *
 * Suggest that beaconing be enabled when digipeating.
 */
	int i, j, k, b;

	for (i=0; i<MAX_CHANS; i++) {
	  for (j=0; j<MAX_CHANS; j++) {

/* APRS digipeating. */

	    if (p_digi_config->enabled[i][j]) {

	      if ( strcmp(p_audio_config->achan[i].mycall, "") == 0 || 
		   strcmp(p_audio_config->achan[i].mycall, "NOCALL") == 0 || 
		   strcmp(p_audio_config->achan[i].mycall, "N0CALL") == 0) {

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: MYCALL must be set for receive channel %d before digipeating is allowed.\n", i);
	        p_digi_config->enabled[i][j] = 0;
	      }

	      if ( strcmp(p_audio_config->achan[j].mycall, "") == 0 || 
	           strcmp(p_audio_config->achan[j].mycall, "NOCALL") == 0 ||
		   strcmp(p_audio_config->achan[j].mycall, "N0CALL") == 0) {

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: MYCALL must be set for transmit channel %d before digipeating is allowed.\n", i); 
	        p_digi_config->enabled[i][j] = 0;
	      }

	      b = 0;
	      for (k=0; k<p_misc_config->num_beacons; k++) {
	        if (p_misc_config->beacon[k].sendto_chan == j) b++;
	      }
	      if (b == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Beaconing should be configured for channel %d when digipeating is enabled.\n", j); 
		// It's a recommendation, not a requirement.
		// Was there some good reason to turn it off in earlier version?
	        //p_digi_config->enabled[i][j] = 0;
	      }
	    }

/* Connected mode digipeating. */

	    if (p_cdigi_config->enabled[i][j]) {

	      if ( strcmp(p_audio_config->achan[i].mycall, "") == 0 ||
		   strcmp(p_audio_config->achan[i].mycall, "NOCALL") == 0 ||
		   strcmp(p_audio_config->achan[i].mycall, "N0CALL") == 0) {

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: MYCALL must be set for receive channel %d before digipeating is allowed.\n", i);
	        p_cdigi_config->enabled[i][j] = 0;
	      }

	      if ( strcmp(p_audio_config->achan[j].mycall, "") == 0 ||
	           strcmp(p_audio_config->achan[j].mycall, "NOCALL") == 0 ||
		   strcmp(p_audio_config->achan[j].mycall, "N0CALL") == 0) {

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: MYCALL must be set for transmit channel %d before digipeating is allowed.\n", i);
	        p_cdigi_config->enabled[i][j] = 0;
	      }

	      b = 0;
	      for (k=0; k<p_misc_config->num_beacons; k++) {
	        if (p_misc_config->beacon[k].sendto_chan == j) b++;
	      }
	      if (b == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Beaconing should be configured for channel %d when digipeating is enabled.\n", j);
		// It's a recommendation, not a requirement.
	      }
	    }
	  }

/* When IGate is enabled, all radio channels must have a callsign associated. */

	  if (strlen(p_igate_config->t2_login) > 0 &&
	      (p_audio_config->chan_medium[i] == MEDIUM_RADIO || p_audio_config->chan_medium[i] == MEDIUM_NETTNC)) {

	    if (strcmp(p_audio_config->achan[i].mycall, "NOCALL") == 0  || strcmp(p_audio_config->achan[i].mycall, "N0CALL") == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: MYCALL must be set for receive channel %d before Rx IGate is allowed.\n", i);
	      strlcpy (p_igate_config->t2_login, "", sizeof(p_igate_config->t2_login));
	    }
	    // Currently we can have only one transmit channel.
	    // This might be generalized someday to allow more.
	    if (p_igate_config->tx_chan >= 0 && 
			( strcmp(p_audio_config->achan[p_igate_config->tx_chan].mycall, "") == 0 ||
		          strcmp(p_audio_config->achan[p_igate_config->tx_chan].mycall, "NOCALL") == 0 ||
			  strcmp(p_audio_config->achan[p_igate_config->tx_chan].mycall, "N0CALL") == 0)) {

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: MYCALL must be set for transmit channel %d before Tx IGate is allowed.\n", i);
	      p_igate_config->tx_chan = -1;
	    }
	  }
	}

// Apply default IS>RF IGate filter if none specified.  New in 1.4.
// This will handle eventual case of multiple transmit channels.

	if (strlen(p_igate_config->t2_login) > 0) {
	  for (j=0; j<MAX_CHANS; j++) {
	    if (p_audio_config->chan_medium[j] == MEDIUM_RADIO || p_audio_config->chan_medium[j] == MEDIUM_NETTNC) {
	      if (p_digi_config->filter_str[MAX_CHANS][j] == NULL) {
	        p_digi_config->filter_str[MAX_CHANS][j] = strdup("i/180");
	      }
	    }
	  }
	}

// Terrible hack.  But what can we do?

	if (p_misc_config->maxv22 < 0) {
	  p_misc_config->maxv22 = p_misc_config->retry / 3;
	}

} /* end config_init */


/*
 * Parse the PBEACON or OBEACON options.
 * Returns 1 for success, 0 for serious error.
 */

// FIXME: provide error messages when non applicable option is used for particular beacon type.
// e.g.  IBEACON DELAY=1 EVERY=1 SENDTO=IG OVERLAY=R SYMBOL="igate" LAT=37^44.46N LONG=122^27.19W COMMENT="N1KOL-1 IGATE"
// Just ignores overlay, symbol, lat, long, and comment.

static int beacon_options(char *cmd, struct beacon_s *b, int line, struct audio_s *p_audio_config)
{
	char *t;
	char temp_symbol[100];
	int ok;
	char zone[8];
	double easting = G_UNKNOWN;
	double northing = G_UNKNOWN;

	strlcpy (temp_symbol, "", sizeof(temp_symbol));
	strlcpy (zone, "", sizeof(zone));

	b->sendto_type = SENDTO_XMIT;
	b->sendto_chan = 0;
	b->delay = 60;
	b->slot = G_UNKNOWN;
	b->every = 600;
	//b->delay = 6;		// temp test.
	//b->every = 3600;
	b->lat = G_UNKNOWN;
	b->lon = G_UNKNOWN;
	b->ambiguity = 0;
	b->alt_m = G_UNKNOWN;
	b->symtab = '/';
	b->symbol = '-';	/* house */
	b->freq = G_UNKNOWN;
	b->tone = G_UNKNOWN;
	b->offset = G_UNKNOWN;
	b->source = NULL;
	b->dest = NULL;

	while ((t = split(NULL,0)) != NULL) {

	  char keyword[20];
	  char value[1000];
	  char *e;
	  char *p;


	  e = strchr(t, '=');
	  if (e == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file: No = found in, %s, on line %d.\n", t, line);
	    return (0);
	  }
	  *e = '\0';
	  strlcpy (keyword, t, sizeof(keyword));
	  strlcpy (value, e+1, sizeof(value));

// QUICK TEMP EXPERIMENT, maybe permanent new feature.
// Recognize \xnn as hexadecimal value.  Handy for UTF-8 in comment.
// Maybe recognize the <0xnn> form that we print.
//
// # Convert between languages here:  https://translate.google.com/  then
// # Convert to UTF-8 bytes here: https://codebeautify.org/utf8-converter
//
// pbeacon delay=0:05 every=0:30 sendto=R0 lat=12.5N long=69.97W  comment="\xe3\x82\xa2\xe3\x83\x9e\xe3\x83\x81\xe3\x83\xa5\xe3\x82\xa2\xe7\x84\xa1\xe7\xb7\x9a   \xce\xa1\xce\xb1\xce\xb4\xce\xb9\xce\xbf\xce\xb5\xcf\x81\xce\xb1\xcf\x83\xce\xb9\xcf\x84\xce\xb5\xcf\x87\xce\xbd\xce\xb9\xcf\x83\xce\xbc\xcf\x8c\xcf\x82"

	  char temp[256];
	  int tlen = 0;

	  for (char *p = value; *p != '\0'; ) {
	    if (p[0] == '\\' && p[1] == 'x' && strlen(p) >= 4 && isxdigit(p[2]) && isxdigit(p[3])) {
	      int n = 0;
	      for (int i = 2; i < 4; i++) {
	        n = n * 16;
	        if (islower(p[i])) {
	          n += p[i] - 'a' + 10;
	        }
	        else if (isupper(p[i])) {
	          n += p[i] - 'A' + 10;
	        }
	        else {		// must be digit due to isxdigit test above.
	          n += p[i] - '0';
	        }
	      }
	      temp[tlen++] = n;
	      p += 4;
	    }
	    else {
	      temp[tlen++] = *p++;
	    }
	  }
	  temp[tlen] = '\0';
	  strlcpy (value, temp, sizeof(value));

// end
	  if (strcasecmp(keyword, "DELAY") == 0) {
	    b->delay = parse_interval(value,line);
	  }
	  else if (strcasecmp(keyword, "SLOT") == 0) {
	    int n = parse_interval(value,line);
	    if ( n < 1 || n > 3600) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Beacon time slot, %d, must be in range of 1 to 3600 seconds.\n", line, n);
	      continue;
	    }
	    b->slot = n;
	  }
	  else if (strcasecmp(keyword, "EVERY") == 0) {
	    b->every = parse_interval(value,line);
	  }
	  else if (strcasecmp(keyword, "SENDTO") == 0) {
	    if (value[0] == 'i' || value[0] == 'I') {
	       b->sendto_type = SENDTO_IGATE;
	       b->sendto_chan = 0;
	    }
	    else if (value[0] == 'r' || value[0] == 'R') {
	       int n = atoi(value+1);
	       if (( n < 0 || n >= MAX_CHANS || p_audio_config->chan_medium[n] == MEDIUM_NONE)
			&& p_audio_config->chan_medium[n] != MEDIUM_IGATE) {
	         text_color_set(DW_COLOR_ERROR);
	         dw_printf ("Config file, line %d: Simulated receive on channel %d is not valid.\n", line, n);
	         continue;
	       }
	       b->sendto_type = SENDTO_RECV;
	       b->sendto_chan = n;
	    }
	    else if (value[0] == 't' || value[0] == 'T' || value[0] == 'x' || value[0] == 'X') {
	      int n = atoi(value+1);
	      if (( n < 0 || n >= MAX_CHANS || p_audio_config->chan_medium[n] == MEDIUM_NONE)
			&& p_audio_config->chan_medium[n] != MEDIUM_IGATE) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: Send to channel %d is not valid.\n", line, n);
	        continue;
	      }

	      b->sendto_type = SENDTO_XMIT;
	      b->sendto_chan = n;
	    }
	    else {
	       int n = atoi(value);
	       if (( n < 0 || n >= MAX_CHANS || p_audio_config->chan_medium[n] == MEDIUM_NONE)
			&& p_audio_config->chan_medium[n] != MEDIUM_IGATE) {
	         text_color_set(DW_COLOR_ERROR);
	         dw_printf ("Config file, line %d: Send to channel %d is not valid.\n", line, n);
	         continue;
	       }
	       b->sendto_type = SENDTO_XMIT;
	       b->sendto_chan = n;
	    }
	  }
	  else if (strcasecmp(keyword, "SOURCE") == 0) {
	    b->source = strdup(value);
	    for (p = b->source; *p != '\0'; p++) {
	      if (islower(*p)) {
	        *p = toupper(*p);	/* silently force upper case. */
	      }
	    }
	    if (strlen(b->source) > 9) {
	       b->source[9] = '\0';
	    }
	  }
	  else if (strcasecmp(keyword, "DEST") == 0) {
	    b->dest = strdup(value);
	    for (p = b->dest; *p != '\0'; p++) {
	      if (islower(*p)) {
	        *p = toupper(*p);	/* silently force upper case. */
	      }
	    }
	    if (strlen(b->dest) > 9) {
	       b->dest[9] = '\0';
	    }
	  }
	  else if (strcasecmp(keyword, "VIA") == 0) {

#if 1	// proper checking

	    if (check_via_path(value) >= 0) {
	      b->via = strdup(value);
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: invalid via path.\n", line);
	    }


#else	// previously

	    b->via = strdup(value);
	    for (p = b->via; *p != '\0'; p++) {
	      if (islower(*p)) {
	        *p = toupper(*p);	/* silently force upper case. */
	      }
	    }
#endif
	  }
	  else if (strcasecmp(keyword, "INFO") == 0) {
	    b->custom_info = strdup(value);
	  }
	  else if (strcasecmp(keyword, "INFOCMD") == 0) {
	    b->custom_infocmd = strdup(value);
	  }
	  else if (strcasecmp(keyword, "OBJNAME") == 0) {
	    strlcpy(b->objname, value, sizeof(b->objname));
	  }
	  else if (strcasecmp(keyword, "LAT") == 0) {
	    b->lat = parse_ll (value, LAT, line);
	  }
	  else if (strcasecmp(keyword, "LONG") == 0 || strcasecmp(keyword, "LON") == 0) {
	    b->lon = parse_ll (value, LON, line);
	  }
	  else if (strcasecmp(keyword, "AMBIGUITY") == 0 || strcasecmp(keyword, "AMBIG") == 0) {
	    int n = atoi(value);
	    if (n >= 0 && n <= 4) {
	      b->ambiguity = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Location ambiguity, on line %d, must be in range of 0 to 4.\n", line);
	    }
	  }
	  else if (strcasecmp(keyword, "ALT") == 0 || strcasecmp(keyword, "ALTITUDE") == 0) {

	    char *unit = strpbrk(value, "abcedfghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
	    if (unit != NULL) {
	      float meters = 0;
	      for (int j=0; j<NUM_UNITS && meters == 0; j++) {
	        if (strcasecmp(units[j].name, unit) == 0) {
	          meters = units[j].meters;
	        }
	      }
	      if (meters == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Unrecognized unit '%s' for altitude.  Using meter.\n", line, unit);
	        dw_printf ("Try using singular form.  e.g.  ft or foot rather than feet.\n");
	        b->alt_m = atof(value);
	      }
	      else {
	        // valid unit
	        b->alt_m = atof(value) * meters;
	      }
	    } else {
	      // no unit specified
	      b->alt_m = atof(value);
	    }
	  }
	  else if (strcasecmp(keyword, "ZONE") == 0) {
	    strlcpy(zone, value, sizeof(zone));
	  }
	  else if (strcasecmp(keyword, "EAST") == 0 || strcasecmp(keyword, "EASTING") == 0) {
	    easting = atof(value);
	  }
	  else if (strcasecmp(keyword, "NORTH") == 0 || strcasecmp(keyword, "NORTHING") == 0) {
	    northing = atof(value);
	  }
	  else if (strcasecmp(keyword, "SYMBOL") == 0) {
	    /* Defer processing in case overlay appears later. */
	    strlcpy (temp_symbol, value, sizeof(temp_symbol));
	  }
	  else if (strcasecmp(keyword, "OVERLAY") == 0) {
	    if (strlen(value) == 1 && (isupper(value[0]) || isdigit(value[0]))) {
	      b->symtab = value[0];
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Overlay must be one character in range of 0-9 or A-Z, upper case only, on line %d.\n", line);
	    }
	  }
	  else if (strcasecmp(keyword, "POWER") == 0) {
	    b->power = atoi(value);
	  }
	  else if (strcasecmp(keyword, "HEIGHT") == 0) {	// This is in feet.
	    b->height = atoi(value);
	    // TODO: ability to add units suffix, e.g.  10m
	  }
	  else if (strcasecmp(keyword, "GAIN") == 0) {
	    b->gain = atoi(value);
	  }
	  else if (strcasecmp(keyword, "DIR") == 0 || strcasecmp(keyword, "DIRECTION") == 0) {
	    strlcpy(b->dir, value, sizeof(b->dir));
	  }
	  else if (strcasecmp(keyword, "FREQ") == 0) {
	    b->freq = atof(value);
	  }
	  else if (strcasecmp(keyword, "TONE") == 0) {
	    b->tone = atof(value);
	  }
	  else if (strcasecmp(keyword, "OFFSET") == 0 || strcasecmp(keyword, "OFF") == 0) {
	    b->offset = atof(value);
	  }
	  else if (strcasecmp(keyword, "COMMENT") == 0) {
	    b->comment = strdup(value);
	  }
	  else if (strcasecmp(keyword, "COMMENTCMD") == 0) {
	    b->commentcmd = strdup(value);
	  }
	  else if (strcasecmp(keyword, "COMPRESS") == 0 || strcasecmp(keyword, "COMPRESSED") == 0) {
	    b->compress = atoi(value);
	  }
	  else if (strcasecmp(keyword, "MESSAGING") == 0) {
	    b->messaging = atoi(value);
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: Invalid option keyword, %s.\n", line, keyword);
	    return (0);
	  }
	}

	if (b->custom_info != NULL && b->custom_infocmd != NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Config file, line %d: Can't use both INFO and INFOCMD at the same time.\n", line);
	}

	if (b->compress && b->ambiguity != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Config file, line %d: Position ambiguity can't be used with compressed location format.\n", line);
	  b->ambiguity = 0;
	}

/*
 * Convert UTM coordinates to lat / long.
 */
	if (strlen(zone) > 0 || easting != G_UNKNOWN || northing != G_UNKNOWN) {

	  if (strlen(zone) > 0 && easting != G_UNKNOWN && northing != G_UNKNOWN) {

	    long lzone;
	    char latband, hemi;
	    long lerr;
	    double dlat, dlon;

	    lzone = parse_utm_zone (zone, &latband, &hemi);

	    lerr = Convert_UTM_To_Geodetic(lzone, hemi, easting, northing, &dlat, &dlon);

            if (lerr == 0) {
              b->lat = R2D(dlat);
	      b->lon = R2D(dlon);
	    }
	    else {
	      char message [300];

              utm_error_string (lerr, message);
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid UTM location: \n%s\n", line, message);
            }
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: When any of ZONE, EASTING, NORTHING specified, they must all be specified.\n", line);
	  }
	}

/*
 * Process symbol now that we have any later overlay.
 *
 * FIXME: Someone who used this was surprised to end up with Solar Powser  (S-).
 *	overlay=S symbol="/-"
 * We should complain if overlay used with symtab other than \.
 */
	if (strlen(temp_symbol) > 0) {

	  if (strlen(temp_symbol) == 2 && 
		(temp_symbol[0] == '/' || temp_symbol[0] == '\\' || isupper(temp_symbol[0]) || isdigit(temp_symbol[0])) &&
		temp_symbol[1] >= '!' && temp_symbol[1] <= '~') {

	    /* Explicit table and symbol. */

	    if (isupper(b->symtab) || isdigit(b->symtab)) {
	      b->symbol = temp_symbol[1];
	    } 
	    else {
	      b->symtab = temp_symbol[0];
	      b->symbol = temp_symbol[1];
	    }
	  }
	  else {

	    /* Try to look up by description. */
	    ok = symbols_code_from_description (b->symtab, temp_symbol, &(b->symtab), &(b->symbol));
	    if (!ok) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Could not find symbol matching %s.\n", line, temp_symbol);
	    }
	  }
	}

/* Check is here because could be using default channel when SENDTO= is not specified. */

	if (b->sendto_type == SENDTO_XMIT) {

	  if (( b->sendto_chan < 0 || b->sendto_chan >= MAX_CHANS || p_audio_config->chan_medium[b->sendto_chan] == MEDIUM_NONE)
		&& p_audio_config->chan_medium[b->sendto_chan] != MEDIUM_IGATE) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: Send to channel %d is not valid.\n", line, b->sendto_chan);
	    return (0);
	  }

	  if (p_audio_config->chan_medium[b->sendto_chan] == MEDIUM_IGATE) {  // Prevent subscript out of bounds.
									     // Will be using call from chan 0 later.
	    if ( strcmp(p_audio_config->achan[0].mycall, "") == 0 || 
	         strcmp(p_audio_config->achan[0].mycall, "NOCALL") == 0 || 
	         strcmp(p_audio_config->achan[0].mycall, "N0CALL") == 0 ) {

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: MYCALL must be set for channel %d before beaconing is allowed.\n", 0); 
	      return (0);
	    }
	  } else {
	    if ( strcmp(p_audio_config->achan[b->sendto_chan].mycall, "") == 0 || 
	         strcmp(p_audio_config->achan[b->sendto_chan].mycall, "NOCALL") == 0 || 
	         strcmp(p_audio_config->achan[b->sendto_chan].mycall, "N0CALL") == 0 ) {

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: MYCALL must be set for channel %d before beaconing is allowed.\n", b->sendto_chan); 
	      return (0);
	    }
	  }
	}

	return (1);
}

/* end config.c */
