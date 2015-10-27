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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#if __WIN32__
#include "pthreads/pthread.h"
#else
#include <pthread.h>
#endif

#ifdef USE_HAMLIB
#include <hamlib/rig.h>
#endif

#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "digipeater.h"
#include "config.h"
#include "aprs_tt.h"
#include "igate.h"
#include "latlong.h"
#include "symbols.h"
#include "xmit.h"

// geotranz

#include "utm.h"
#include "mgrs.h"
#include "usng.h"
#include "error_string.h"

#define D2R(d) ((d) * M_PI / 180.)
#define R2D(r) ((r) * 180. / M_PI)



//#include "tq.h"

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

#define NUM_UNITS (sizeof(units) / sizeof(struct units_s))

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

#define DEG1 '^'
#define DEG2 0xb0	/* ISO Latin1 */
#define DEG3 0xf8	/* Microsoft code page 437 */

// TODO: recognize UTF-8 degree symbol.


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
	strcpy (stemp, str);
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
 * Output:	hemi	- Hemisphere, 'N' or 'S'.
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
 *		I'm using the first alternatve.
 *		GEOTRANS uses the third.
 *		We will also recognize the second one but I'm not sure if I want to document it.
 *
 *----------------------------------------------------------------*/

long parse_utm_zone (char *szone, char *hemi)
{
	long lzone;
	char *zlet;


	*hemi = 'N';	/* default */

        lzone = strtol(szone, &zlet, 10);

        if (*zlet == '\0') {
	  /* Number is not followed by letter something else.  */
	  /* Allow negative number to mean south. */

	  if (lzone < 0) {
	    *hemi = 'S';
	    lzone = (- lzone);
	  }
	}
	else {
	  if (islower (*zlet)) {
	    *zlet = toupper(*zlet);
	  }
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
}




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
	parse_ll ("12�30", LAT);

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
 *		p_digi_config	- Digipeater configuration stored here.
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


void config_init (char *fname, struct audio_s *p_audio_config, 
			struct digi_config_s *p_digi_config,
			struct tt_config_s *p_tt_config,
			struct igate_config_s *p_igate_config,
			struct misc_config_s *p_misc_config)
{
	FILE *fp;
	char stuff[256];
	//char *p;
	//int c, p;
	//int err;
	int line;
	int channel;
	int adevice;
#if USE_HAMLIB
    RIG *rig;
    int rigs = 0;
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("config_init ( %s )\n", fname);
#endif

/* 
 * First apply defaults.
 */

	memset (p_audio_config, 0, sizeof(struct audio_s));

	/* First audio device is always available with defaults. */
	/* Others must be explicitly defined before use. */

	for (adevice=0; adevice<MAX_ADEVS; adevice++) {

	  strcpy (p_audio_config->adev[adevice].adevice_in, DEFAULT_ADEVICE);
	  strcpy (p_audio_config->adev[adevice].adevice_out, DEFAULT_ADEVICE);

	  p_audio_config->adev[adevice].defined = 0;
	  p_audio_config->adev[adevice].num_channels = DEFAULT_NUM_CHANNELS;		/* -2 stereo */
	  p_audio_config->adev[adevice].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;	/* -r option */
	  p_audio_config->adev[adevice].bits_per_sample = DEFAULT_BITS_PER_SAMPLE;	/* -8 option for 8 instead of 16 bits */
	}

	p_audio_config->adev[0].defined = 1;

	for (channel=0; channel<MAX_CHANS; channel++) {
	  int ot;

	  p_audio_config->achan[channel].valid = 0;				/* One or both channels will be */
								/* set to valid when corresponding */
								/* audio device is defined. */
	  p_audio_config->achan[channel].modem_type = MODEM_AFSK;			
	  p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;		/* -m option */
	  p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;		/* -s option */
	  p_audio_config->achan[channel].baud = DEFAULT_BAUD;			/* -b option */

	  /* None.  Will set default later based on other factors. */
	  strcpy (p_audio_config->achan[channel].profiles, "");	

	  p_audio_config->achan[channel].num_freq = 1;				
	  p_audio_config->achan[channel].offset = 0;

	  p_audio_config->achan[channel].fix_bits = DEFAULT_FIX_BITS;
	  p_audio_config->achan[channel].sanity_test = SANITY_APRS;
	  p_audio_config->achan[channel].passall = 0;

	  for (ot = 0; ot < NUM_OCTYPES; ot++) {
	    p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_NONE;
	    strcpy (p_audio_config->achan[channel].octrl[ot].ptt_device, "");
	    p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_NONE;
	    p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_NONE;
	    p_audio_config->achan[channel].octrl[ot].ptt_gpio = 0;
	    p_audio_config->achan[channel].octrl[ot].ptt_lpt_bit = 0;
	    p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
	    p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 0;
	  }

	  p_audio_config->achan[channel].dwait = DEFAULT_DWAIT;				
	  p_audio_config->achan[channel].slottime = DEFAULT_SLOTTIME;				
	  p_audio_config->achan[channel].persist = DEFAULT_PERSIST;				
	  p_audio_config->achan[channel].txdelay = DEFAULT_TXDELAY;				
	  p_audio_config->achan[channel].txtail = DEFAULT_TXTAIL;				
	}

	/* First channel should always be valid. */
	/* If there is no ADEVICE, it uses default device in mono. */

	p_audio_config->achan[0].valid = 1;


	memset (p_digi_config, 0, sizeof(struct digi_config_s));
	//p_digi_config->num_chans = p_audio_config->adev[0].num_channels;	// TODO: rethink for > 2 case.
	p_digi_config->dedupe_time = DEFAULT_DEDUPE;

	memset (p_tt_config, 0, sizeof(struct tt_config_s));	
	p_tt_config->gateway_enabled = 0;
	p_tt_config->ttloc_size = 2;	/* Start with at least 2.  */
					/* When full, it will be increased by 50 %. */
	p_tt_config->ttloc_ptr = malloc (sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	p_tt_config->ttloc_len = 0;

	/* Retention time and decay algorithm from 13 Feb 13 version of */
	/* http://www.aprs.org/aprstt/aprstt-coding24.txt */

	p_tt_config->retain_time = 80 * 60;
	p_tt_config->num_xmits = 7;
	assert (p_tt_config->num_xmits <= TT_MAX_XMITS);
	p_tt_config->xmit_delay[0] = 3;		/* Before initial transmission. */
	p_tt_config->xmit_delay[1] = 16;
	p_tt_config->xmit_delay[2] = 32;
	p_tt_config->xmit_delay[3] = 64;
	p_tt_config->xmit_delay[4] = 2 * 60;
	p_tt_config->xmit_delay[5] = 4 * 60;
	p_tt_config->xmit_delay[6] = 8 * 60;

	memset (p_misc_config, 0, sizeof(struct misc_config_s));
	p_misc_config->agwpe_port = DEFAULT_AGWPE_PORT;
	p_misc_config->kiss_port = DEFAULT_KISS_PORT;
	p_misc_config->enable_kiss_pt = 0;				/* -p option */

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
	p_igate_config->tx_limit_1 = 6;
	p_igate_config->tx_limit_5 = 20;


	/* People find this confusing. */
	/* Ideally we'd like to figure out if com0com is installed */
	/* and automatically enable this.  */
	
	//strcpy (p_misc_config->nullmodem, DEFAULT_NULLMODEM);
	strcpy (p_misc_config->nullmodem, "");
	strcpy (p_misc_config->nmea_port, "");
	strcpy (p_misc_config->logdir, "");


/* 
 * Try to extract options from a file.
 * 
 * Windows:  File must be in current working directory.
 *
 * Linux: Search current directory then home directory.
 */


	channel = 0;
	adevice = 0;

	fp = fopen (fname, "r");
#ifndef __WIN32__
	if (fp == NULL && strcmp(fname, "direwolf.conf") == 0) {
	/* Failed to open the default location.  Try home dir. */
	  char *p;

	  p = getenv("HOME");
	  if (p != NULL) {
	    strcpy (stuff, p);
	    strcat (stuff, "/direwolf.conf");
	    fp = fopen (stuff, "r");
	  } 
	}
#endif
	if (fp == NULL)	{
	  // TODO: not exactly right for all situations.
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Could not open config file %s\n", fname);
	  dw_printf ("Try using -c command line option for alternate location.\n");
	  return;
	}
	

	line = 0;
	while (fgets(stuff, sizeof(stuff), fp) != NULL) {
	  char *t;

	  line++;


	  t = strtok (stuff, " ,\t\n\r");
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
 */

	  /* Note that ALSA name can contain comma such as hw:1,0 */

	  if (strncasecmp(t, "ADEVICE", 7) == 0) {
	    adevice = 0;
	    if (isdigit(t[7])) {
	      adevice = t[7] - '0';
	    }

	    if (adevice < 0 || adevice >= MAX_ADEVS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Device number %d out of range for ADEVICE command on line %d.\n", adevice, line);
	      adevice = 0;
	      continue;
	    }

	    t = strtok (NULL, " \t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing name of audio device for ADEVICE command on line %d.\n", line);
	      continue;
	    }

	    p_audio_config->adev[adevice].defined = 1;
	
	    /* First channel of device is valid. */
	    p_audio_config->achan[ADEVFIRSTCHAN(adevice)].valid = 1;

	    strncpy (p_audio_config->adev[adevice].adevice_in, t, sizeof(p_audio_config->adev[adevice].adevice_in)-1);
	    strncpy (p_audio_config->adev[adevice].adevice_out, t, sizeof(p_audio_config->adev[adevice].adevice_out)-1);

	    t = strtok (NULL, " \t\n\r");
	    if (t != NULL) {
	      strncpy (p_audio_config->adev[adevice].adevice_out, t, sizeof(p_audio_config->adev[adevice].adevice_out)-1);
	    }
	  }

/*
 * ARATE 		- Audio samples per second, 11025, 22050, 44100, etc.
 */

	  else if (strcasecmp(t, "ARATE") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
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
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing number of audio channels for ACHANNELS command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n ==1 || n == 2) {
	      p_audio_config->adev[adevice].num_channels = n;

	      /* Set valid channels depending on mono or stereo. */

	      p_audio_config->achan[ADEVFIRSTCHAN(adevice)].valid = 1;
	      if (n == 2) {
	        p_audio_config->achan[ADEVFIRSTCHAN(adevice) + 1].valid = 1;
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
 * CHANNEL 		- Set channel for following commands.
 */

	  else if (strcasecmp(t, "CHANNEL") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing channel number for CHANNEL command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 0 && n < MAX_CHANS) {

	      channel = n;

	      if ( ! p_audio_config->achan[n].valid) {

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
 * MYCALL station
 */
	  else if (strcasecmp(t, "mycall") == 0) {
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing value for MYCALL command on line %d.\n", line);
	      continue;
	    }
	    else {

	      // Definitely set for current channel.
	      // Set for other channels which have not been set yet.

	      int c;

	      for (c = 0; c < MAX_CHANS; c++) {

	        if (c == channel || 
			strlen(p_audio_config->achan[c].mycall) == 0 || 
			strcasecmp(p_audio_config->achan[c].mycall, "NOCALL") == 0 ||
			strcasecmp(p_audio_config->achan[c].mycall, "N0CALL") == 0) {

	          char *p;

	          strncpy (p_audio_config->achan[c].mycall, t, sizeof(p_audio_config->achan[c].mycall)-1);

	          for (p = p_audio_config->achan[c].mycall; *p != '\0'; p++) {
	            if (islower(*p)) {
		      *p = toupper(*p);	/* silently force upper case. */
	            }
	          }
	          // TODO: additional checks if valid
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
 *	
 */

	  else if (strcasecmp(t, "MODEM") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing data transmission speed for MODEM command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 100 && n <= 10000) {
	      p_audio_config->achan[channel].baud = n;
	      if (n != 300 && n != 1200 && n != 9600) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Warning: Non-standard baud rate.  Are you sure?\n", line);
    	      }
	    }
	    else {
	      p_audio_config->achan[channel].baud = DEFAULT_BAUD;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable baud rate. Using %d.\n", 
				 line, p_audio_config->achan[channel].baud);
   	    }


	    /* Set defaults based on speed. */
	    /* Should be same as -B command line option in direwolf.c. */

	    if (p_audio_config->achan[channel].baud < 600) {
              p_audio_config->achan[channel].modem_type = MODEM_AFSK;
              p_audio_config->achan[channel].mark_freq = 1600;
              p_audio_config->achan[channel].space_freq = 1800;
	    }
	    else if (p_audio_config->achan[channel].baud > 2400) {
              p_audio_config->achan[channel].modem_type = MODEM_SCRAMBLE;
              p_audio_config->achan[channel].mark_freq = 0;
              p_audio_config->achan[channel].space_freq = 0;
	    }
	    else {
              p_audio_config->achan[channel].modem_type = MODEM_AFSK;
              p_audio_config->achan[channel].mark_freq = 1200;
              p_audio_config->achan[channel].space_freq = 2200;
	    }

	    /* Get mark frequency. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      /* all done. */
	      continue;
	    }

	    if (alldigits(t)) {

/* old style */

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

	      t = strtok (NULL, " ,\t\n\r");
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

	      t = strtok (NULL, " ,\t\n\r");
	      if (t != NULL) {

	        /* Look for some combination of letter(s) and + */

	        if (isalpha(t[0]) || t[0] == '+') {
		  char *pc;

		  /* Here we only catch something other than letters and + mixed in. */
		  /* Later, we check for valid letters and no more than one letter if + specified. */

	          for (pc = t; *pc != '\0'; pc++) {
		    if ( ! isalpha(*pc) && ! *pc == '+') {
	              text_color_set(DW_COLOR_ERROR);
                      dw_printf ("Line %d: Demodulator type can only contain letters and + character.\n", line);
		    }
		  }    
		
		  strncpy (p_audio_config->achan[channel].profiles, t, sizeof(p_audio_config->achan[channel].profiles));
	          t = strtok (NULL, " ,\t\n\r");
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

	        t = strtok (NULL, " ,\t\n\r");
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

/* New style. */

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
		  strncpy (p_audio_config->achan[channel].profiles, t, sizeof(p_audio_config->achan[channel].profiles));
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

		else {
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: Unrecognized option for MODEM: %s\n", line, t);
	        } 

	        t = strtok (NULL, " ,\t\n\r");
	      }

	      /* A later place catches disallowed combination of + and @. */
	      /* A later place sets /n for 300 baud if not specified by user. */

	      //printf ("debug: div = %d\n", p_audio_config->achan[channel].decimate);

	    }
	  }

/*
 * (deprecated) HBAUD 		- Set data bits per second.  Standard values are 300 & 1200 for AFSK
 *				and 9600 for baseband with scrambling.
 */

#if 0
	  else if (strcasecmp(t, "HBAUD") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing date transmission rate for HBAUD command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 100 && n <= 10000) {
	      p_audio_config->achan[channel].baud = n;
	      if (n != 300 && n != 1200 && n != 9600) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Warning: Non-standard baud rate.  Are you sure?\n", line);
    	      }
	      if (n == 9600) {
		/* TODO: should be separate option to keep it more general. */
	        //text_color_set(DW_COLOR_ERROR);
	        //dw_printf ("Line %d: Note: Using scrambled baseband for 9600 baud.\n", line);
	        p_audio_config->achan[channel].modem_type = MODEM_SCRAMBLE;		
    	      }
	    }
	    else {
	      p_audio_config->achan[channel].baud = DEFAULT_BAUD;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable baud rate. Using %d.\n", 
				 line, p_audio_config->achan[channel].baud);
   	    }
	  }
#endif

/*
 * (deprecated) MARK 		- Mark tone frequency.
 */

#if 0
	  else if (strcasecmp(t, "MARK") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing tone frequency for MARK command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 300 && n <= 3000) {
	      p_audio_config->achan[channel].mark_freq = n;
	    }
	    else {
	      p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable mark tone frequency. Using %d.\n", 
				 line, p_audio_config->achan[channel].mark_freq);
   	    }
	  }
#endif

/*
 * (deprecated) SPACE 		- Space tone frequency.
 */

#if 0
	  else if (strcasecmp(t, "SPACE") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing tone frequency for SPACE command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 300 && n <= 3000) {
	      p_audio_config->achan[channel].space_freq = n;
	    }
	    else {
	      p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable space tone frequency. Using %d.\n", 
					 line, p_audio_config->achan[channel].space_freq);
   	    }
	  }
#endif

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
 */

	  else if (strcasecmp(t, "FIX_BITS") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing value for FIX_BITS command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= RETRY_NONE && n <= RETRY_REMOVE_TWO_SEP) {
	      p_audio_config->achan[channel].fix_bits = (retry_t)n;
	    }
	    else {
	      p_audio_config->achan[channel].fix_bits = DEFAULT_FIX_BITS;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid value for FIX_BITS. Using %d.\n", 
			line, p_audio_config->achan[channel].fix_bits);
   	    }

	    t = strtok (NULL, " ,\t\n\r");
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
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Invalid option '%s' for FIX_BITS.\n", line, t);
	      }
	      t = strtok (NULL, " ,\t\n\r");
	    }
	  }


/*
 * PTT 		- Push To Talk signal line.
 * DCD		- Data Carrier Detect indicator.
 *
 * xxx  serial-port [-]rts-or-dtr [ [-]rts-or-dtr ]
 * xxx  GPIO  [-]gpio-num
 * xxx  LPT  [-]bit-num
 *
 * Applies to most recent CHANNEL command.
 */

	  else if (strcasecmp(t, "PTT") == 0 || strcasecmp(t, "DCD") == 0) {
	    //int n;
	    int ot;
	    char otname[8];

	    if (strcasecmp(t, "PTT") == 0) {
	      ot = OCTYPE_PTT;
	      strcpy (otname, "PTT");
	    }
	    else if (strcasecmp(t, "DCD") == 0) {
	      ot = OCTYPE_DCD;
	      strcpy (otname, "DCD");
	    }
	    else {
	      ot = OCTYPE_FUTURE;
	      strcpy (otname, "FUTURE");
	    }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: Missing serial port name for %s command.\n", 
			line, otname);
	      continue;
	    }

	    if (strcasecmp(t, "GPIO") == 0) {

/* GPIO case, Linux only. */

#if __WIN32__
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: %s with GPIO is only available on Linux.\n", line, otname);
#else		
	      t = strtok (NULL, " ,\t\n\r");
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing GPIO number for %s.\n", line, otname);
	        continue;
	      }

	      if (*t == '-') {
	        p_audio_config->achan[channel].octrl[ot].ptt_gpio = atoi(t+1);
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
	      }
	      else {
	        p_audio_config->achan[channel].octrl[ot].ptt_gpio = atoi(t);
		p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
	      }
	      p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_GPIO;
#endif
	    }
	    else if (strcasecmp(t, "LPT") == 0) {

/* Parallel printer case, x86 Linux only. */

#if  ( defined(__i386__) || defined(__x86_64__) ) && ( defined(__linux__) || defined(__unix__) )

	      t = strtok (NULL, " ,\t\n\r");
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
          p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_HAMLIB;

	      t = strtok (NULL, " ,\t\n\r");
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing RIG number.\n", line);
	        continue;
	      }

          p_audio_config->achan[channel].octrl[ot].ptt_rig = atoi(t);

#else
          text_color_set(DW_COLOR_ERROR);
          dw_printf ("Config file line %d: %s with RIG is only available when hamlib support is enabled.\n", line, otname);
#endif
        }
	    else  {

/* serial port case. */

	      strncpy (p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));

	      t = strtok (NULL, " ,\t\n\r");
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

	      t = strtok (NULL, " ,\t\n\r");
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

	  }  /* end of PTT */


/*
 * RIG          - HAMLib rig configuration.
 *
 * xxx port model
 *
 * For example a Yeasu FT-817 on /dev/ttyUSB0:
 * RIG /dev/ttyUSB0 120
 *
 * For example rigctld on localhost:
 * RIG 127.0.0.1:4532 2
 */

      else if (strcasecmp(t, "RIG") == 0) {
#ifdef USE_HAMLIB
        int n;
        hamlib_port_t port;
        rig_model_t rig_model;

        if (rigs == MAX_RIGS) {
          text_color_set(DW_COLOR_ERROR);
          dw_printf ("Config file line %d: Maximum number of rigs reached.\n", line);
          continue;
        }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: Missing port, model[, key=value].\n", 
			line);
	      continue;
	    }

	    strncpy (port.pathname, t, FILPATHLEN - 1);

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: Missing model[, key=value]\n", line);
	      continue;
	    }

        if (strcasecmp(t, "AUTO") == 0) {
          rig_load_all_backends();
          rig_model = rig_probe(&port);
        }
        else {
          rig_model = atoi(t);
        }

        rig = rig_init(rig_model);
        if (!rig) {
          text_color_set(DW_COLOR_ERROR);
          dw_printf ("Config file line %d: Unknown rig %d, please check riglist.h.\n", line, rig_model);
          continue;
        }

        strncpy (rig->state.rigport.pathname, port.pathname, FILPATHLEN - 1);
        n = rig_open(rig);
        if (n != RIG_OK) {
          text_color_set(DW_COLOR_ERROR);
          dw_printf ("Config file line %d: Rig open error %d: %s\n", line, n, rigerror(n));
          continue;
        }

        p_audio_config->rig[rigs++] = rig;
        p_audio_config->rigs = rigs;
    
#else
        text_color_set(DW_COLOR_ERROR);
        dw_printf ("Config file line %d: RIG is only available when hamlib support is enabled.\n", line);
        continue;
#endif
      }

/*
 * DWAIT 		- Extra delay for receiver squelch.
 */

	  else if (strcasecmp(t, "DWAIT") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
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
 * SLOTTIME 		- For non-digipeat transmit delay timing.
 */

	  else if (strcasecmp(t, "SLOTTIME") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
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
	    t = strtok (NULL, " ,\t\n\r");
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
 * TXDELAY 		- For transmit delay timing.
 */

	  else if (strcasecmp(t, "TXDELAY") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
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
 * TXTAIL 		- For transmit timing.
 */

	  else if (strcasecmp(t, "TXTAIL") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
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
 * SPEECH  script 
 *
 * Specify script for text-to-speech function.		
 */

	  else if (strcasecmp(t, "SPEECH") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing script for Text-to-Speech function.\n", line);
	      continue;
	    }
	    
	    /* See if we can run it. */

	    if (xmit_speak_it(t, -1, " ") == 0) {
	      strcpy (p_audio_config->tts_script, t);
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Error trying to run Text-to-Speech function.\n", line);
	      continue;
	   }
	  }

/*
 * ==================== Digipeater parameters ==================== 
 */

/*
 * DIGIPEAT  from-chan  to-chan  alias-pattern  wide-pattern  [ OFF|DROP|MARK|TRACE ] 
 */

	  else if (strcasecmp(t, "digipeat") == 0) {
	    int from_chan, to_chan;
	    int e;
	    char message[100];
	    	    

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing FROM-channel on line %d.\n", line);
	      continue;
	    }
	    from_chan = atoi(t);
	    if (from_chan < 0 || from_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: FROM-channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }
	    if ( ! p_audio_config->achan[from_chan].valid) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: FROM-channel %d is not valid.\n", 
							line, from_chan);
	      continue;
	    }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }
	    to_chan = atoi(t);
	    if (to_chan < 0 || to_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: TO-channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }
	    if ( ! p_audio_config->achan[to_chan].valid) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TO-channel %d is not valid.\n", 
							line, to_chan);
	      continue;
	    }
	
	    t = strtok (NULL, " ,\t\n\r");
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

	    t = strtok (NULL, " ,\t\n\r");
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

	    t = strtok (NULL, " ,\t\n\r");
	    if (t != NULL) {
	      if (strcasecmp(t, "OFF") == 0) {
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_OFF;
	        t = strtok (NULL, " ,\t\n\r");
	      }
	      else if (strcasecmp(t, "DROP") == 0) {
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_DROP;
	        t = strtok (NULL, " ,\t\n\r");
	      }
	      else if (strcasecmp(t, "MARK") == 0) {
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_MARK;
	        t = strtok (NULL, " ,\t\n\r");
	      }
	      else if (strcasecmp(t, "TRACE") == 0) {
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_TRACE;
	        t = strtok (NULL, " ,\t\n\r");
	      }
	    }

	    if (t != NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Found \"%s\" where end of line was expected.\n", line, t);     
	    }
	  }

/*
 * DEDUPE 		- Time to suppress digipeating of duplicate packets.
 */

	  else if (strcasecmp(t, "DEDUPE") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
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
	    int e;
	    char message[100];
	    	    

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing FROM-channel on line %d.\n", line);
	      continue;
	    }
	    from_chan = atoi(t);
	    if (from_chan < 0 || from_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: FROM-channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }
	    if ( ! p_audio_config->achan[from_chan].valid) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: FROM-channel %d is not valid.\n", 
							line, from_chan);
	      continue;
	    }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }
	    to_chan = atoi(t);
	    if (to_chan < 0 || to_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: TO-channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }
	    if ( ! p_audio_config->achan[to_chan].valid) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TO-channel %d is not valid.\n", 
							line, to_chan);
	      continue;
	    }
	

	    p_digi_config->regen[from_chan][to_chan] = 1;

	  }


/*
 * ==================== Packet Filtering for digipeater or IGate ==================== 
 */

/*
 * FILTER  from-chan  to-chan  filter_specification_expression
 * FILTER  from-chan  IG       filter_specification_expression
 * FILTER  IG         to-chan  filter_specification_expression
 */

	  else if (strcasecmp(t, "FILTER") == 0) {
	    int from_chan, to_chan;
	    int e;
	    char message[100];
	    	    

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing FROM-channel on line %d.\n", line);
	      continue;
	    }
	    if (*t == 'i' || *t == 'I') {
	      from_chan = MAX_CHANS;
	    }
	    else {
	      from_chan = isdigit(*t) ? atoi(t) : -999;
	      if (from_chan < 0 || from_chan >= MAX_CHANS) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Filter FROM-channel must be in range of 0 to %d or \"IG\" on line %d.\n", 
							MAX_CHANS-1, line);
	        continue;
	      }

	      if ( ! p_audio_config->achan[from_chan].valid) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: FROM-channel %d is not valid.\n", 
							line, from_chan);
	        continue;
	      }
	    }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }
	    if (*t == 'i' || *t == 'I') {
	      to_chan = MAX_CHANS;
	    }
	    else {
	      to_chan = isdigit(*t) ? atoi(t) : -999;
	      if (to_chan < 0 || to_chan >= MAX_CHANS) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Filter TO-channel must be in range of 0 to %d or \"IG\" on line %d.\n", 
							MAX_CHANS-1, line);
	        continue;
	      }
	      if ( ! p_audio_config->achan[to_chan].valid) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: TO-channel %d is not valid.\n", 
							line, to_chan);
	        continue;
	      }
	    }

	    t = strtok (NULL, "\n\r");		/* Take rest of line including spaces. */

	    if (t == NULL) {
	      t = " ";				/* Empty means permit nothing. */
	    }

	    p_digi_config->filter_str[from_chan][to_chan] = strdup(t);

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
	    //int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing latitude for TTCORRAL command.\n", line);
	      continue;
	    }
	    p_tt_config->corral_lat = parse_ll(t,LAT,line);

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing longitude for TTCORRAL command.\n", line);
	      continue;
	    }
	    p_tt_config->corral_lon = parse_ll(t,LON,line);

	    t = strtok (NULL, " ,\t\n\r");
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

	    /* Allocate new space, but first, if already full, make larger. */
	    if (p_tt_config->ttloc_len == p_tt_config->ttloc_size) {
	      p_tt_config->ttloc_size += p_tt_config->ttloc_size / 2;
	      p_tt_config->ttloc_ptr = realloc (p_tt_config->ttloc_ptr, sizeof(struct ttloc_s) * p_tt_config->ttloc_size);
	    }
	    p_tt_config->ttloc_len++;
	    assert (p_tt_config->ttloc_len >= 0 && p_tt_config->ttloc_len <= p_tt_config->ttloc_size);

	    tl = &(p_tt_config->ttloc_ptr[p_tt_config->ttloc_len-1]);
	    tl->type = TTLOC_POINT;
	    strcpy(tl->pattern, "");
	    tl->point.lat = 0;
	    tl->point.lon = 0;

	    /* Pattern: B and digits */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTPOINT command.\n", line);
	      continue;
	    }
	    strcpy (tl->pattern, t);

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTPOINT pattern must begin with upper case 'B'.\n", line);
	    }
	    for (j=1; j<strlen(t); j++) {
	      if ( ! isdigit(t[j])) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTPOINT pattern must be B and digits only.\n", line);
	      }
	    }

	    /* Latitude */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing latitude for TTPOINT command.\n", line);
	      continue;
	    }
	    tl->point.lat = parse_ll(t,LAT,line);

	    /* Longitude */

	    t = strtok (NULL, " ,\t\n\r");
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
	    strcpy(tl->pattern, "");
	    tl->vector.lat = 0;
	    tl->vector.lon = 0;
	    tl->vector.scale = 1;
	   
	    /* Pattern: B5bbbd... */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTVECTOR command.\n", line);
	      continue;
	    }
	    strcpy (tl->pattern, t);

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTVECTOR pattern must begin with upper case 'B'.\n", line);
	    }
	    if (strncmp(t+1, "5bbb", 4) != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTVECTOR pattern would normally contain \"5bbb\".\n", line);
	    }
	    for (j=1; j<strlen(t); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'b' && t[j] != 'd') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTVECTOR pattern must contain only B, digits, b, and d.\n", line);
	      }
	    }

	    /* Latitude */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing latitude for TTVECTOR command.\n", line);
	      continue;
	    }
	    tl->vector.lat = parse_ll(t,LAT,line);

	    /* Longitude */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing longitude for TTVECTOR command.\n", line);
	      continue;
	    }
	    tl->vector.lon = parse_ll(t,LON,line);

	    /* Longitude */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing scale for TTVECTOR command.\n", line);
	      continue;
	    }
	    scale = atof(t);

	    /* Unit. */

	    t = strtok (NULL, " ,\t\n\r");
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
	    strcpy(tl->pattern, "");
	    tl->grid.lat0 = 0;
	    tl->grid.lon0 = 0;
	    tl->grid.lat9 = 0;
	    tl->grid.lon9 = 0;

	    /* Pattern: B [digit] x... y... */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTGRID command.\n", line);
	      continue;
	    }
	    strcpy (tl->pattern, t);

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTGRID pattern must begin with upper case 'B'.\n", line);
	    }
	    for (j=1; j<strlen(t); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'x' && t[j] != 'y') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTGRID pattern must be B, optional digit, xxx, yyy.\n", line);
	      }
	    }

	    /* Minimum Latitude - all zeros in received data */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing latitude for TTGRID command.\n", line);
	      continue;
	    }
	    tl->grid.lat0 = parse_ll(t,LAT,line);

	    /* Minimum Longitude - all zeros in received data */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing longitude for TTGRID command.\n", line);
	      continue;
	    }
	    tl->grid.lon0 = parse_ll(t,LON,line);

	    /* Maximum Latitude - all nines in received data */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing latitude for TTGRID command.\n", line);
	      continue;
	    }
	    tl->grid.lat9 = parse_ll(t,LAT,line);

	    /* Maximum Longitude - all nines in received data */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing longitude for TTGRID command.\n", line);
	      continue;
	    }
	    tl->grid.lon0 = parse_ll(t,LON,line);

	    /* temp debugging */

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
	    int znum;
	    char *zlet;
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
	    strcpy(tl->pattern, "");
	    tl->utm.lzone = 0;
	    tl->utm.scale = 1;
	    tl->utm.x_offset = 0;
	    tl->utm.y_offset = 0;

	    /* Pattern: B [digit] x... y... */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTUTM command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strcpy (tl->pattern, t);

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTUTM pattern must begin with upper case 'B'.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    for (j=1; j<strlen(t); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'x' && t[j] != 'y') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTUTM pattern must be B, optional digit, xxx, yyy.\n", line);
		// Bail out somehow.  continue would match inner for.
	      }
	    }

	    /* Zone 1 - 60 and optional latitudinal letter. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing zone for TTUTM command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }

	    tl->utm.lzone = parse_utm_zone (t, &(tl->utm.hemi));

 	    /* Optional scale. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t != NULL) {
	      
	      tl->utm.scale = atof(t);

	      /* Optional x offset. */

	      t = strtok (NULL, " ,\t\n\r");
	      if (t != NULL) {

	        tl->utm.x_offset = atof(t);

	        /* Optional y offset. */

	        t = strtok (NULL, " ,\t\n\r");
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
	    int znum;
	    char *zlet;
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
	    strcpy(tl->pattern, "");
	    strcpy(tl->mgrs.zone, "");

	    /* Pattern: B [digit] x... y... */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTUSNG/TTMGRS command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strcpy (tl->pattern, t);

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTUSNG/TTMGRS pattern must begin with upper case 'B'.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    num_x = 0;
	    num_y = 0;
	    for (j=1; j<strlen(t); j++) {
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

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing zone & square for TTUSNG/TTMGRS command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    memset (tl->mgrs.zone, 0, sizeof (tl->mgrs.zone));
	    strncpy (tl->mgrs.zone, t, sizeof (tl->mgrs.zone) - 1);

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

	    t = strtok (NULL, " ,\t\n\r");
	    if (t != NULL) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unexpected stuff at end ignored:  %s\n", line, t);
	    }
	  }


/*
 * TTSATSQ 		- Define pattern to be used for Satellite square.
 *
 * TTSATSQ   pattern    
 *
 *			Pattern would be  B[0-9A-D]xxxx 
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
	    strcpy(tl->pattern, "");
	    tl->point.lat = 0;
	    tl->point.lon = 0;

	    /* Pattern: B, optional additional button, exactly xxxx for matching */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTSATSQ command.\n", line);
	      p_tt_config->ttloc_len--;
	      continue;
	    }
	    strcpy (tl->pattern, t);

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
 */
	  else if (strcasecmp(t, "TTMACRO") == 0) {

	    struct ttloc_s *tl;
	    int j;
	    //char ch;
	    int p_count[3], d_count[3];

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
	    strcpy(tl->pattern, "");

	    /* Pattern: Any combination of digits, x, y, and z. */
	    /* Also make note of which letters are used in pattern and defintition. */
 	    /* Version 1.2: also allow A,B,C,D in the pattern. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTMACRO command.\n", line);
	      continue;
	    }
	    strcpy (tl->pattern, t);

	    p_count[0] = p_count[1] = p_count[2] = 0;

	    for (j=0; j<strlen(t); j++) {
	      if ( strchr ("0123456789ABCDxyz", t[j]) == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTMACRO pattern can contain only digits, A, B, C, D, and lower case x, y, or z.\n", line);
	        continue;
	      }
	      /* Count how many x, y, z in the pattern. */
	      if (t[j] >= 'x' && t[j] <= 'z') {
		p_count[t[j]-'x']++;
	      }
	    }

	    //text_color_set(DW_COLOR_DEBUG);
	    //dw_printf ("Line %d: TTMACRO pattern \"%s\" p_count = %d %d %d.\n", line, t, p_count[0], p_count[1], p_count[2]);

	    /* Now gather up the definition. */
	    /* It can contain touch tone characters and lower case x, y, z for substitutions. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing definition for TTMACRO command.\n", line);
	      tl->macro.definition = "";	/* Don't die on null pointer later. */
	      continue;
	    }
	    tl->macro.definition = strdup(t);

	    d_count[0] = d_count[1] = d_count[2] = 0;

	    for (j=0; j<strlen(t); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'x' && t[j] != 'y' && t[j] != 'z' &&
			t[j] != 'A' && t[j] != 'B' && t[j] != 'C' && t[j] != 'D' && 
			t[j] != '*' && t[j] != '#') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTMACRO definition can contain only 0-9, A, B, C, D, *, #, x, y, z.\n", line);
	        continue;
	      }
	      if (t[j] >= 'x' && t[j] <= 'z') {
		d_count[t[j]-'x']++;
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
	  }

/*
 * TTOBJ 		- TT Object Report options.
 *
 * TTOBJ  recv-chan  xmit-chan  [ via-path ] 
 */


	  else if (strcasecmp(t, "TTOBJ") == 0) {
	    int r, x;

	    t = strtok (NULL, " ,\t\n\r");
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
	    if ( ! p_audio_config->achan[r].valid) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TTOBJ DTMF receive channel %d is not valid.\n", 
							line, r);
	      continue;
	    }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing transmit channel for TTOBJ command.\n", line);
	      continue;
	    }

	    x = atoi(t);
	    if (x < 0 || x > MAX_CHANS-1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Transmit channel must be in range of 0 to %d on line %d.\n", 
							MAX_CHANS-1, line);
	      continue;
	    }
	    if ( ! p_audio_config->achan[x].valid) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TTOBJ transmit channel %d is not valid.\n", 
							line, x);
	      continue;
	    }

// This enables the DTMF decoder on the specified channel.
// Additional channels can be enabled with the DTMF command.
// Note that they do not enable the APRStt gateway.

	    p_audio_config->achan[r].dtmf_decode = DTMF_DECODE_ON;
	    p_tt_config->gateway_enabled = 1;
	    p_tt_config->obj_recv_chan = r;
	    p_tt_config->obj_xmit_chan = x;

	    t = strtok (NULL, " ,\t\n\r");
	    if (t != NULL) {

	      // TODO: Should do some validity checking.
	      strncpy (p_tt_config->obj_xmit_via, t, sizeof(p_tt_config->obj_xmit_via));
	    }
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
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing IGate server name for IGSERVER command.\n", line);
	      continue;
	    }
	    strncpy (p_igate_config->t2_server_name, t, sizeof(p_igate_config->t2_server_name)-1);

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
	    
	    t = strtok (NULL, " ,\t\n\r");
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
	    //printf ("DEBUG  server=%s   port=%d\n", p_igate_config->t2_server_name, p_igate_config->t2_server_port);
	    //exit (0);
	  }

/*
 * IGLOGIN 		- Login callsign and passcode for IGate server
 *
 * IGLOGIN  callsign  passcode
 */

	  else if (strcasecmp(t, "IGLOGIN") == 0) {
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing login callsign for IGLOGIN command.\n", line);
	      continue;
	    }
	    // TODO: Wouldn't hurt to do validity checking of format.
	    strncpy (p_igate_config->t2_login, t, sizeof(p_igate_config->t2_login)-1);

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing passcode for IGLOGIN command.\n", line);
	      continue;
	    }
	    strncpy (p_igate_config->t2_passcode, t, sizeof(p_igate_config->t2_passcode)-1);
	  }

/*
 * IGTXVIA 		- Transmit channel and VIA path for messages from IGate server
 *
 * IGTXVIA  channel  [ path ]
 */

	  else if (strcasecmp(t, "IGTXVIA") == 0) {
	    int n;

	    t = strtok (NULL, " ,\t\n\r");
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

	    t = strtok (NULL, " \t\n\r");
	    if (t != NULL) {
	      char *p;
	      p_igate_config->tx_via[0] = ',';
	      strncpy (p_igate_config->tx_via + 1, t, sizeof(p_igate_config->tx_via)-2);
	      for (p = p_igate_config->tx_via; *p != '\0'; p++) {
	        if (islower(*p)) {
		  *p = toupper(*p);	/* silently force upper case. */
	        }
	      }
	    }
	  }

/*
 * IGFILTER 		- Filter for messages from IGate server
 *
 * IGFILTER  filter-spec ... 
 */

	  else if (strcasecmp(t, "IGFILTER") == 0) {
	    //int n;

	    t = strtok (NULL, "\n\r");		/* Take rest of line as one string. */

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

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing one minute limit for IGTXLIMIT command.\n", line);
	      continue;
	    }
	    
	    /* limits of 20 and 100 are unfriendly but not insane. */

	    n = atoi(t);
            if (n >= 1 && n <= 20) {
	      p_igate_config->tx_limit_1 = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid one minute transmit limit. Using %d.\n", 
				line, p_igate_config->tx_limit_1);
   	    }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing five minute limit for IGTXLIMIT command.\n", line);
	      continue;
	    }
	    
	    n = atoi(t);
            if (n >= 1 && n <= 100) {
	      p_igate_config->tx_limit_5 = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid one minute transmit limit. Using %d.\n", 
				line, p_igate_config->tx_limit_5);
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
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing port number for AGWPORT command.\n", line);
	      continue;
	    }
	    n = atoi(t);
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
 * KISSPORT 		- Port number for KISS over IP. 
 */

	  else if (strcasecmp(t, "KISSPORT") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing port number for KISSPORT command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if ((n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) || n == 0) {
	      p_misc_config->kiss_port = n;
	    }
	    else {
	      p_misc_config->kiss_port = DEFAULT_KISS_PORT;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid port number for KISS TCPIP Socket Interface. Using %d.\n", 
			line, p_misc_config->kiss_port);
   	    }
	  }

/*
 * NULLMODEM		- Device name for our end of the virtual "null modem"
 */
	  else if (strcasecmp(t, "nullmodem") == 0) {
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing device name for my end of the 'null modem' on line %d.\n", line);
	      continue;
	    }
	    else {
	      strncpy (p_misc_config->nullmodem, t, sizeof(p_misc_config->nullmodem)-1);
	    }
	  }

/*
 * NMEA		- Device name for communication with NMEA device.
 */
	  else if (strcasecmp(t, "nmea") == 0) {
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing device name for NMEA port on line %d.\n", line);
	      continue;
	    }
	    else {
	      strncpy (p_misc_config->nmea_port, t, sizeof(p_misc_config->nmea_port)-1);
	    }
	  }

/*
 * LOGDIR	- Directory name for storing log files.  Use "." for current working directory.
 */
	  else if (strcasecmp(t, "logdir") == 0) {
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing directory name for LOGDIR on line %d.\n", line);
	      continue;
	    }
	    else {
	      strncpy (p_misc_config->logdir, t, sizeof(p_misc_config->logdir)-1);
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
	    dw_printf ("Use PBEACON, OBEACON, or CBEACON instead.\n");
  
	  }


/*
 * PBEACON keyword=value ...
 * OBEACON keyword=value ...
 * TBEACON keyword=value ...
 * CBEACON keyword=value ...
 *
 * New style with keywords for options.
 */

	  else if (strcasecmp(t, "PBEACON") == 0 ||
		   strcasecmp(t, "OBEACON") == 0 ||
		   strcasecmp(t, "TBEACON") == 0 ||
		   strcasecmp(t, "CBEACON") == 0) {
#if __WIN32__
	    if (strcasecmp(t, "TBEACON") == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: TBEACON is available only in Linux version.\n", line);
	      continue;
	    }
#endif

#ifndef ENABLE_GPS
	    if (strcasecmp(t, "TBEACON") == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Rebuild with GPS support for TBEACON to be available.\n", line);
	      continue;
	    }
#endif
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
 * SMARTBEACONING fast_speed fast_rate slow_speed slow_rate turn_time turn_angle turn_slope
 */

	  else if (strcasecmp(t, "SMARTBEACON") == 0 ||	
	           strcasecmp(t, "SMARTBEACONING") == 0) {

	    int n;

#define SB_NUM(name,sbvar,minn,maxx,unit)  							\
	    t = strtok (NULL, " ,\t\n\r");							\
	    if (t == NULL) {									\
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
	    t = strtok (NULL, " ,\t\n\r");							\
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
	  }

	  if (p_audio_config->achan[i].valid && strlen(p_igate_config->t2_login) > 0) {

	    if (strcmp(p_audio_config->achan[i].mycall, "NOCALL") == 0  || strcmp(p_audio_config->achan[i].mycall, "N0CALL") == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: MYCALL must be set for receive channel %d before Rx IGate is allowed.\n", i);
	      strcpy (p_igate_config->t2_login, "");
	    }
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

} /* end config_init */


/*
 * Parse the PBEACON or OBEACON options.
 * Returns 1 for success, 0 for serious error.
 */

static int beacon_options(char *cmd, struct beacon_s *b, int line, struct audio_s *p_audio_config)
{
	char options[1000];
	char *o;
	char *t;
	char *p;
	int q;
	char temp_symbol[100];
	int ok;
	char zone[8];
	double easting = G_UNKNOWN;
	double northing = G_UNKNOWN;

	strcpy (temp_symbol, "");
	strcpy (zone, "");

	b->sendto_type = SENDTO_XMIT;
	b->sendto_chan = 0;
	b->delay = 60;
	b->every = 600;
	//b->delay = 6;		// temp test.
	//b->every = 3600;
	b->lat = G_UNKNOWN;
	b->lon = G_UNKNOWN;
	b->alt_m = G_UNKNOWN;
	b->symtab = '/';
	b->symbol = '-';	/* house */

/*
 * cmd should be rest of command line after ?BEACON was removed.
 *
 * Quoting is required for any values containing spaces.
 * This could happen for an object name, comment, symbol description, ...
 * To prevent strtok from stopping at those spaces, change them to 
 * non-breaking space character temporarily.  After spliting everything
 * up at white space, change them back to normal spaces.
 */
 
#define NBSP (' ' + 0x80)

	p = cmd;		/* Process from here. */
	o = options;		/* to here. */
	q = 0;			/* Keep track of whether in quoted part. */

	for ( ; *p != '\0' ; p++) {

	  switch (*p) {

	    case '"':
	      if (!q) {		/* opening quote */
	        if (*(p-1) != '=') {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file: line %d: Suspicious use of \" not after =.\n", line);
	 	  dw_printf ("Suggestion: Double it and quote entire value.\n");
	          *o++ = '"';	/* Treat as regular character. */
	        }
	        else {
	          q = 1;
	        }
	      }
	      else {		/* embedded or closing quote */
	        if (*(p+1) == '"') {
	          *o++ = '"';	/* reduce double to single */
	          p++;
	        }
                else if (isspace(*(p+1)) || *(p+1) == '\0') {
	          q = 0;
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file: line %d: Suspicious use of \" not at end of value.\n", line);
	 	  dw_printf ("Suggestion: Double it and quote entire value.\n");
	          *o++ = '"';	/* Treat as regular character. */
	        }		  
	      }
	      break;

	    case ' ':

	      *o++ = q ? NBSP : ' ';
	      break;

	    default:
	      *o++ = *p;
	      break;
	  }
	}
	*o = '\0';

	for (t = strtok (options, " \t\n\r"); t != NULL; t = strtok (NULL, " \t\n\r")) {

	  char keyword[20];
	  char value[200];
	  char *e;
	  char *p;
	  //int q;


	  e = strchr(t, '=');
	  if (e == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file: No = found in, %s, on line %d.\n", t, line);
	    return (0);
	  }
	  *e = '\0';
	  strcpy (keyword, t);
	  strcpy (value, e+1);

/* Put back normal spaces. */

	  for (p = value; *p != '\0'; p++) {
	    // char is signed for MinGW!
	    if (((int)(*p) & 0xff) == NBSP) *p = ' ';
	  }

	  if (strcasecmp(keyword, "DELAY") == 0) {
	    b->delay = parse_interval(value,line);
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
	       if ( n < 0 || n >= MAX_CHANS || ! p_audio_config->achan[n].valid) {
	         text_color_set(DW_COLOR_ERROR);
	         dw_printf ("Config file, line %d: Send to channel %d is not valid.\n", line, n);
	         continue;
	       }
	       b->sendto_type = SENDTO_RECV;
	       b->sendto_chan = n;
	    }
	    else if (value[0] == 't' || value[0] == 'T' || value[0] == 'x' || value[0] == 'X') {
	      int n = atoi(value+1);
	      if ( n < 0 || n >= MAX_CHANS || ! p_audio_config->achan[n].valid) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file, line %d: Send to channel %d is not valid.\n", line, n);
	        continue;
	      }

	      b->sendto_type = SENDTO_XMIT;
	      b->sendto_chan = n;
	    }
	    else {
	       int n = atoi(value);
	       if ( n < 0 || n >= MAX_CHANS || ! p_audio_config->achan[n].valid) {
	         text_color_set(DW_COLOR_ERROR);
	         dw_printf ("Config file, line %d: Send to channel %d is not valid.\n", line, n);
	         continue;
	       }
	       b->sendto_type = SENDTO_XMIT;
	       b->sendto_chan = n;
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
	    b->via = strdup(value);
	    for (p = b->via; *p != '\0'; p++) {
	      if (islower(*p)) {
	        *p = toupper(*p);	/* silently force upper case. */
	      }
	    }
	  }
	  else if (strcasecmp(keyword, "INFO") == 0) {
	    b->custom_info = strdup(value);
	  }
	  else if (strcasecmp(keyword, "OBJNAME") == 0) {
	    strncpy(b->objname, value, 9);
	  }
	  else if (strcasecmp(keyword, "LAT") == 0) {
	    b->lat = parse_ll (value, LAT, line);
	  }
	  else if (strcasecmp(keyword, "LONG") == 0 || strcasecmp(keyword, "LON") == 0) {
	    b->lon = parse_ll (value, LON, line);
	  }
	  else if (strcasecmp(keyword, "ALT") == 0 || strcasecmp(keyword, "ALTITUDE") == 0) {
	    b->alt_m = atof(value);
	  }
	  else if (strcasecmp(keyword, "ZONE") == 0) {
	    strncpy(zone, value, sizeof(zone));
	  }
	  else if (strcasecmp(keyword, "EAST") == 0 || strcasecmp(keyword, "EASTING") == 0) {
	    easting = atof(value);
	  }
	  else if (strcasecmp(keyword, "NORTH") == 0 || strcasecmp(keyword, "NORTHING") == 0) {
	    northing = atof(value);
	  }
	  else if (strcasecmp(keyword, "SYMBOL") == 0) {
	    /* Defer processing in case overlay appears later. */
	    strcpy (temp_symbol, value);
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
	  else if (strcasecmp(keyword, "HEIGHT") == 0) {
	    b->height = atoi(value);
	  }
	  else if (strcasecmp(keyword, "GAIN") == 0) {
	    b->gain = atoi(value);
	  }
	  else if (strcasecmp(keyword, "DIR") == 0 || strcasecmp(keyword, "DIRECTION") == 0) {
	    strncpy(b->dir, value, 2);
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

/*
 * Convert UTM coordintes to lat / long.
 */
	if (strlen(zone) > 0 || easting != G_UNKNOWN || northing != G_UNKNOWN) {

	  if (strlen(zone) > 0 && easting != G_UNKNOWN && northing != G_UNKNOWN) {

	    long lzone;
	    char hemi;
	    long lerr;
	    double dlat, dlon;

	    lzone = parse_utm_zone (zone, &hemi);

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
	    dw_printf ("Config file, line %d: When any of ZONE, EASTING, NORTHING specifed, they must all be specified.\n", line);
	  }
	}

/*
 * Process symbol now that we have any later overlay.
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

	  if ( b->sendto_chan < 0 || b->sendto_chan >= MAX_CHANS || ! p_audio_config->achan[b->sendto_chan].valid) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: Send to channel %d is not valid.\n", line, b->sendto_chan);
	    return (0);
	  }

	  if ( strcmp(p_audio_config->achan[b->sendto_chan].mycall, "") == 0 || 
	       strcmp(p_audio_config->achan[b->sendto_chan].mycall, "NOCALL") == 0 || 
	       strcmp(p_audio_config->achan[b->sendto_chan].mycall, "N0CALL") == 0 ) {

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file: MYCALL must be set for channel %d before beaconing is allowed.\n", b->sendto_chan); 
	    return (0);
	  }
	}

	return (1);
}

/* end config.c */
