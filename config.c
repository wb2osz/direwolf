//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014  John Langner, WB2OSZ
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

#if __WIN32__
#include "pthreads/pthread.h"
#else
#include <pthread.h>
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

static int beacon_options(char *cmd, struct beacon_s *b, int line);


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
	parse_ll ("12°30", LAT);

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
 * Outputs:	p_modem		- Radio channel parameters stored here.
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


void config_init (char *fname, struct audio_s *p_modem, 
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

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("config_init ( %s )\n", fname);
#endif

/* 
 * First apply defaults.
 */

	memset (p_modem, 0, sizeof(struct audio_s));

	strcpy (p_modem->adevice_in, DEFAULT_ADEVICE);
	strcpy (p_modem->adevice_out, DEFAULT_ADEVICE);

	p_modem->num_channels = DEFAULT_NUM_CHANNELS;		/* -2 stereo */
	p_modem->samples_per_sec = DEFAULT_SAMPLES_PER_SEC;	/* -r option */
	p_modem->bits_per_sample = DEFAULT_BITS_PER_SAMPLE;	/* -8 option for 8 instead of 16 bits */
	p_modem->fix_bits = DEFAULT_FIX_BITS;

	for (channel=0; channel<MAX_CHANS; channel++) {

	  p_modem->modem_type[channel] = AFSK;			
	  p_modem->mark_freq[channel] = DEFAULT_MARK_FREQ;		/* -m option */
	  p_modem->space_freq[channel] = DEFAULT_SPACE_FREQ;		/* -s option */
	  p_modem->baud[channel] = DEFAULT_BAUD;			/* -b option */

	  /* None.  Will set default later based on other factors. */
	  strcpy (p_modem->profiles[channel], "");	

	  p_modem->num_freq[channel] = 1;				
	  p_modem->num_subchan[channel] = 1;				
	  p_modem->offset[channel] = 0;

	  //  temp test.
	  // p_modem->num_subchan[channel] = 9;				
	  // p_modem->offset[channel] = 60;

	  p_modem->ptt_method[channel] = PTT_METHOD_NONE;
	  strcpy (p_modem->ptt_device[channel], "");
	  p_modem->ptt_line[channel] = PTT_LINE_RTS;
	  p_modem->ptt_gpio[channel] = 0;
	  p_modem->ptt_invert[channel] = 0;

	  p_modem->slottime[channel] = DEFAULT_SLOTTIME;				
	  p_modem->persist[channel] = DEFAULT_PERSIST;				
	  p_modem->txdelay[channel] = DEFAULT_TXDELAY;				
	  p_modem->txtail[channel] = DEFAULT_TXTAIL;				
	}

	memset (p_digi_config, 0, sizeof(struct digi_config_s));
	p_digi_config->num_chans = p_modem->num_channels;
	p_digi_config->dedupe_time = DEFAULT_DEDUPE;

	memset (p_tt_config, 0, sizeof(struct tt_config_s));	
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
	p_misc_config->num_channels = p_modem->num_channels;
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


/* 
 * Try to extract options from a file.
 * 
 * Windows:  File must be in current working directory.
 *
 * Linux: Search current directory then home directory.
 */


	channel = 0;

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
 * ADEVICE 		- Name of input sound device, and optionally output, if different.
 */

	  /* Note that ALSA name can contain comma such as hw:1,0 */

	  if (strcasecmp(t, "ADEVICE") == 0) {
	    t = strtok (NULL, " \t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing name of audio device for ADEVICE command on line %d.\n", line);
	      continue;
	    }
	    strncpy (p_modem->adevice_in, t, sizeof(p_modem->adevice_in)-1);
	    strncpy (p_modem->adevice_out, t, sizeof(p_modem->adevice_out)-1);

	    t = strtok (NULL, " \t\n\r");
	    if (t != NULL) {
	      strncpy (p_modem->adevice_out, t, sizeof(p_modem->adevice_out)-1);
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
	      p_modem->samples_per_sec = n;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Use a more reasonable audio sample rate in range of %d - %d.\n", 
							line, MIN_SAMPLES_PER_SEC, MAX_SAMPLES_PER_SEC);
   	    }
	  }

/*
 * ACHANNELS 		- Number of audio channels: 1 or 2
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
            if (n >= 1 && n <= MAX_CHANS) {
	      p_modem->num_channels = n;
	      p_digi_config->num_chans = p_modem->num_channels;
	      p_misc_config->num_channels = p_modem->num_channels;
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
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Audio channel number must be 0 or 1.\n", line);
	      channel = 0;
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
	      char *p;

	      strncpy (p_digi_config->mycall[channel], t, sizeof(p_digi_config->mycall[channel])-1);

	      for (p = p_digi_config->mycall[channel]; *p != '\0'; p++) {
	        if (islower(*p)) {
		  *p = toupper(*p);	/* silently force upper case. */
	        }
	      }
	      // TODO: additional checks if valid
	    }
	  }


/*
 * MODEM	- Replaces former HBAUD, MARK, SPACE, and adds new multi modem capability.
 *
 * MODEM  baud [ mark  space  [A][B][C]  [  num-decoders spacing ] ]
 */

	  else if (strcasecmp(t, "MODEM") == 0) {
	    int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing date transmission rate for MODEM command.\n", line);
	      continue;
	    }
	    n = atoi(t);
            if (n >= 100 && n <= 10000) {
	      p_modem->baud[channel] = n;
	      if (n != 300 && n != 1200 && n != 9600) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Warning: Non-standard baud rate.  Are you sure?\n", line);
    	      }
	    }
	    else {
	      p_modem->baud[channel] = DEFAULT_BAUD;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable baud rate. Using %d.\n", 
				 line, p_modem->baud[channel]);
   	    }



	    /* Get mark frequency. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Note: Using scrambled baseband rather than AFSK modem.\n");
	      p_modem->modem_type[channel] = SCRAMBLE;	
	      p_modem->mark_freq[channel] = 0;	
	      p_modem->space_freq[channel] = 0;	
	      continue;
	    }

	    n = atoi(t);
	    /* Originally the upper limit was 3000. */
	    /* Version 1.0 increased to 5000 because someone */
	    /* wanted to use 2400/4800 Hz AFSK. */
	    /* Of course the MIC and SPKR connections won't */
	    /* have enough bandwidth so radios must be modified. */
            if (n >= 300 && n <= 5000) {
	      p_modem->mark_freq[channel] = n;
	    }
	    else {
	      p_modem->mark_freq[channel] = DEFAULT_MARK_FREQ;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable mark tone frequency. Using %d.\n", 
				 line, p_modem->mark_freq[channel]);
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
	      p_modem->space_freq[channel] = n;
	    }
	    else {
	      p_modem->space_freq[channel] = DEFAULT_SPACE_FREQ;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable space tone frequency. Using %d.\n", 
					 line, p_modem->space_freq[channel]);
   	    }

	    /* New feature in 0.9 - Optional filter profile(s). */

	    /* First, set a default based on platform and baud. */

	    if (p_modem->baud[channel] < 600) {

	      /* "D" is a little better at 300 baud. */

	      strcpy (p_modem->profiles[channel], "D");
	    }
	    else {
#if __arm__
	      /* We probably don't have a lot of CPU power available. */

	      if (p_modem->baud[channel] == DEFAULT_BAUD &&
		  p_modem->mark_freq[channel] == DEFAULT_MARK_FREQ && 
		  p_modem->space_freq[channel] == DEFAULT_SPACE_FREQ &&
		  p_modem->samples_per_sec == DEFAULT_SAMPLES_PER_SEC) {

	        strcpy (p_modem->profiles[channel], "F");
	      }
	      else {
	        strcpy (p_modem->profiles[channel], "A");
	      }
#else
	      strcpy (p_modem->profiles[channel], "C");
#endif
	    }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t != NULL) {
	      if (isalpha(t[0])) {
		// TODO: should check all letters.
		strncpy (p_modem->profiles[channel], t, sizeof(p_modem->profiles[channel]));
	        p_modem->num_subchan[channel] = strlen(p_modem->profiles[channel]);
	        t = strtok (NULL, " ,\t\n\r");
		if (strlen(p_modem->profiles[channel]) > 1 && t != NULL) {
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
                dw_printf ("Line %d: Number of modems is out of range. Using 3.\n", line);
		n = 3;
	      }
	      p_modem->num_freq[channel] = n;
	      p_modem->num_subchan[channel] = n;

	      t = strtok (NULL, " ,\t\n\r");
	      if (t != NULL) {
	        n = atoi(t);
                if (n < 5 || n > abs(p_modem->mark_freq[channel] - p_modem->space_freq[channel])/2) {
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf ("Line %d: Unreasonable value for offset between modems.  Using 50 Hz.\n", line);
		  n = 50;
	        }
		p_modem->offset[channel] = n;
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
                dw_printf ("Line %d: Missing frequency offset between modems.  Using 50 Hz.\n", line);
	        p_modem->offset[channel] = 50;
	      }

// TODO: power saver	      
	    }
	  }

/*
 * (deprecated) HBAUD 		- Set data bits per second.  Standard values are 300 & 1200 for AFSK
 *				and 9600 for baseband with scrambling.
 */

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
	      p_modem->baud[channel] = n;
	      if (n != 300 && n != 1200 && n != 9600) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Warning: Non-standard baud rate.  Are you sure?\n", line);
    	      }
	      if (n == 9600) {
		/* TODO: should be separate option to keep it more general. */
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: Note: Using scrambled baseband for 9600 baud.\n", line);
	        p_modem->modem_type[channel] = SCRAMBLE;		
    	      }
	    }
	    else {
	      p_modem->baud[channel] = DEFAULT_BAUD;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable baud rate. Using %d.\n", 
				 line, p_modem->baud[channel]);
   	    }
	  }

/*
 * (deprecated) MARK 		- Mark tone frequency.
 */

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
	      p_modem->mark_freq[channel] = n;
	    }
	    else {
	      p_modem->mark_freq[channel] = DEFAULT_MARK_FREQ;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable mark tone frequency. Using %d.\n", 
				 line, p_modem->mark_freq[channel]);
   	    }
	  }

/*
 * (deprecated) SPACE 		- Space tone frequency.
 */

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
	      p_modem->space_freq[channel] = n;
	    }
	    else {
	      p_modem->space_freq[channel] = DEFAULT_SPACE_FREQ;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Unreasonable space tone frequency. Using %d.\n", 
					 line, p_modem->space_freq[channel]);
   	    }
	  }

/*
 * PTT 		- Push To Talk signal line.
 *
 * PTT  serial-port [-]rts-or-dtr
 * PTT  GPIO  [-]gpio-num
 */

	  else if (strcasecmp(t, "PTT") == 0) {
	    //int n;
	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: Missing serial port name for PTT command.\n", 
			line);
	      continue;
	    }

	    if (strcasecmp(t, "GPIO") != 0) {

/* serial port case. */

	      strncpy (p_modem->ptt_device[channel], t, sizeof(p_modem->ptt_device[channel]));

	      t = strtok (NULL, " ,\t\n\r");
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing RTS or DTR after PTT device name.\n", 
			line);
	        continue;
	      }

	      if (strcasecmp(t, "rts") == 0) {
	        p_modem->ptt_line[channel] = PTT_LINE_RTS;
		p_modem->ptt_invert[channel] = 0;
	      }
	      else if (strcasecmp(t, "dtr") == 0) {
	        p_modem->ptt_line[channel] = PTT_LINE_DTR;
		p_modem->ptt_invert[channel] = 0;
	      }
	      else if (strcasecmp(t, "-rts") == 0) {
	        p_modem->ptt_line[channel] = PTT_LINE_RTS;
		p_modem->ptt_invert[channel] = 1;
	      }
	      else if (strcasecmp(t, "-dtr") == 0) {
	        p_modem->ptt_line[channel] = PTT_LINE_DTR;
		p_modem->ptt_invert[channel] = 1;
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Expected RTS or DTR after PTT device name.\n", 
			line);
	        continue;
	      }

	      p_modem->ptt_method[channel] = PTT_METHOD_SERIAL;
	    }
	    else {

/* GPIO case, Linux only. */

// TODO:
#if 0
//#if __WIN32__
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file line %d: PTT with GPIO is only available on Linux.\n", line);
#else		
	      t = strtok (NULL, " ,\t\n\r");
	      if (t == NULL) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file line %d: Missing GPIO number.\n", line);
	        continue;
	      }

	      if (*t == '-') {
	        p_modem->ptt_gpio[channel] = atoi(t+1);
		p_modem->ptt_invert[channel] = 1;
	      }
	      else {
	        p_modem->ptt_gpio[channel] = atoi(t);
		p_modem->ptt_invert[channel] = 0;
	      }
	      p_modem->ptt_method[channel] = PTT_METHOD_GPIO;
#endif
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
	      p_modem->slottime[channel] = n;
	    }
	    else {
	      p_modem->slottime[channel] = DEFAULT_SLOTTIME;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid delay time for persist algorithm. Using %d.\n", 
			line, p_modem->slottime[channel]);
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
	      p_modem->persist[channel] = n;
	    }
	    else {
	      p_modem->persist[channel] = DEFAULT_PERSIST;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid probability for persist algorithm. Using %d.\n", 
			line, p_modem->persist[channel]);
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
	      p_modem->txdelay[channel] = n;
	    }
	    else {
	      p_modem->txdelay[channel] = DEFAULT_TXDELAY;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid time for transmit delay. Using %d.\n", 
			line, p_modem->txdelay[channel]);
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
	      p_modem->txtail[channel] = n;
	    }
	    else {
	      p_modem->txtail[channel] = DEFAULT_TXTAIL;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid time for transmit timing. Using %d.\n", 
			line, p_modem->txtail[channel]);
   	    }
	  }

/*
 * ==================== Digipeater parameters ==================== 
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
	    if (from_chan < 0 || from_chan > p_digi_config->num_chans-1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: FROM-channel must be in range of 0 to %d on line %d.\n", 
							p_digi_config->num_chans-1, line);
	      continue;
	    }

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Missing TO-channel on line %d.\n", line);
	      continue;
	    }
	    to_chan = atoi(t);
	    if (to_chan < 0 || to_chan > p_digi_config->num_chans-1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: TO-channel must be in range of 0 to %d on line %d.\n", 
							p_digi_config->num_chans-1, line);
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
	      }
	      else if (strcasecmp(t, "DROP") == 0) {
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_DROP;
	      }
	      else if (strcasecmp(t, "MARK") == 0) {
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_MARK;
	      }
	      else if (strcasecmp(t, "TRACE") == 0) {
	        p_digi_config->preempt[from_chan][to_chan] = PREEMPT_TRACE;
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Expected OFF, DROP, MARK, or TRACE on line %d.\n", line);
	      }

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
	    strcpy(tl->utm.zone, "");
	    tl->utm.scale = 1;
	    tl->utm.x_offset = 0;
	    tl->utm.y_offset = 0;

	    /* Pattern: B [digit] x... y... */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTUTM command.\n", line);
	      continue;
	    }
	    strcpy (tl->pattern, t);

	    if (t[0] != 'B') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: TTUTM pattern must begin with upper case 'B'.\n", line);
	    }
	    for (j=1; j<strlen(t); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'x' && t[j] != 'y') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTUTM pattern must be B, optional digit, xxx, yyy.\n", line);
	      }
	    }

	    /* Zone 1 - 60 and optional latitudinal letter. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing zone for TTUTM command.\n", line);
	      continue;
	    }
	    memset (tl->utm.zone, 0, sizeof (tl->utm.zone));
	    strncpy (tl->utm.zone, t, sizeof (tl->utm.zone) - 1);

            znum = strtoul(tl->utm.zone, &zlet, 10);

            if (znum < 1 || znum > 60) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Zone number is out of range.\n\n", line);
              continue;
            }

            if (*zlet != '\0' && strchr ("CDEFGHJKLMNPQRSTUVWX", *zlet) == NULL) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Latitudinal band must be one of CDEFGHJKLMNPQRSTUVWX.\n\n", line);
              continue;
            }

	    /* Optional scale. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      continue;
	    }
	    tl->utm.scale = atof(t);

	    /* Optional x offset. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      continue;
	    }
	    tl->utm.x_offset = atof(t);

	    /* Optional y offset. */

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      continue;
	    }
	    tl->utm.y_offset = atof(t);
	  }

/*
 * TTMACRO 		- Define compact message format with full expansion
 *
 * TTMACRO   pattern  definition
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

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing pattern for TTMACRO command.\n", line);
	      continue;
	    }
	    strcpy (tl->pattern, t);

	    p_count[0] = p_count[1] = p_count[2] = 0;

	    for (j=0; j<strlen(t); j++) {
	      if ( ! isdigit(t[j]) && t[j] != 'x' && t[j] != 'y' && t[j] != 'z') {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Line %d: TTMACRO pattern can contain only digits and lower case x, y, or z.\n", line);
	        continue;
	      }
	      if (t[j] >= 'x' && t[j] <= 'z') {
		p_count[t[j]-'x']++;
	      }
	    }

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
 * TTOBJ  xmit-chan  header 
 */


// TODO:  header can be generated automatically.  Should not be in config file.


	  else if (strcasecmp(t, "TTOBJ") == 0) {
	    int n;

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing transmit channel for TTOBJ command.\n", line);
	      continue;
	    }

	    n = atoi(t);
	    if (n < 0 || n > p_digi_config->num_chans-1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Transmit channel must be in range of 0 to %d on line %d.\n", 
							p_digi_config->num_chans-1, line);
	      continue;
	    }
	    p_tt_config->obj_xmit_chan = n;

	    t = strtok (NULL, " ,\t\n\r");
	    if (t == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Line %d: Missing object header for TTOBJ command.\n", line);
	      continue;
	    }
	    // TODO: Should do some validity checking.

	    strncpy (p_tt_config->obj_xmit_header, t, sizeof(p_tt_config->obj_xmit_header));
	
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
	    if (n < 0 || n > p_digi_config->num_chans-1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: Transmit channel must be in range of 0 to %d on line %d.\n", 
							p_digi_config->num_chans-1, line);
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
            if (n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) {
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
            if (n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) {
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
 * FIX_BITS 		- Attempt to fix frames with bad FCS. 
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
            if (n >= RETRY_NONE && n <= RETRY_TWO_SEP) {
	      p_modem->fix_bits = (retry_t)n;
	    }
	    else {
	      p_modem->fix_bits = DEFAULT_FIX_BITS;
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Line %d: Invalid value for FIX_BITS. Using %d.\n", 
			line, p_modem->fix_bits);
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

	      if (beacon_options(t + strlen("xBEACON") + 1, &(p_misc_config->beacon[p_misc_config->num_beacons]), line)) {
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

#define SB_NUM(name,sbvar,minn,maxx,unit)  								\
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

	for (i=0; i<p_digi_config->num_chans; i++) {
	  for (j=0; j<p_digi_config->num_chans; j++) {

	    if (p_digi_config->enabled[i][j]) {

	      if (strcmp(p_digi_config->mycall[i], "NOCALL") == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: MYCALL must be set for receive channel %d before digipeating is allowed.\n", i);
	        p_digi_config->enabled[i][j] = 0;
	      }

	      if (strcmp(p_digi_config->mycall[j], "NOCALL") == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: MYCALL must be set for transmit channel %d before digipeating is allowed.\n", i); 
	        p_digi_config->enabled[i][j] = 0;
	      }

	      b = 0;
	      for (k=0; k<p_misc_config->num_beacons; k++) {
	        if (p_misc_config->beacon[p_misc_config->num_beacons].chan == j) b++;
	      }
	      if (b == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Beaconing should be configured for channel %d when digipeating is enabled.\n", i); 
	        p_digi_config->enabled[i][j] = 0;
	      }
	    }
	  }

	  if (strlen(p_igate_config->t2_login) > 0) {

	    if (strcmp(p_digi_config->mycall[i], "NOCALL") == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file: MYCALL must be set for receive channel %d before Rx IGate is allowed.\n", i);
	      strcpy (p_igate_config->t2_login, "");
	    }
	    if (p_igate_config->tx_chan >= 0 && 
			strcmp(p_digi_config->mycall[p_igate_config->tx_chan], "NOCALL") == 0) {
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

static int beacon_options(char *cmd, struct beacon_s *b, int line)
{
	char options[1000];
	char *o;
	char *t;
	char *p;
	int q;
	char temp_symbol[100];
	int ok;
	
	strcpy (temp_symbol, "");

	b->chan = 0;
	b->delay = 60;
	b->every = 600;
	//b->delay = 6;		// TODO: temp. remove
	//b->every = 3600;
	b->lat = G_UNKNOWN;
	b->lon = G_UNKNOWN;
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
	       b->chan = -1;
	    }
	    else {
	       b->chan = atoi(value);
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
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: Invalid option keyword, %s.\n", line, keyword);
	    return (0);
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

	return (1);
}

/* end config.c */
