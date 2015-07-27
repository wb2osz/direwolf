//#define DEBUG 1
//#define DEBUG_SIM 1


//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2013,2014  John Langner, WB2OSZ
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
 * Module:      beacon.c
 *
 * Purpose:   	Transmit messages on a fixed schedule.
 *		
 * Description:	Transmit periodic messages as specified in the config file.
 *
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include <time.h>
#if __WIN32__
#include <windows.h>
#endif

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "tq.h"
#include "xmit.h"
#include "config.h"
#include "digipeater.h"
#include "version.h"
#include "encode_aprs.h"
#include "beacon.h"
#include "latlong.h"
#include "dwgps.h"
#include "log.h"



/* 
 * Are we using GPS data?
 * Incremented if tracker beacons configured.  
 * Cleared if dwgps_init fails.
 */

static int g_using_gps = 0;	

/*
 * Save pointers to configuration settings.
 */

static struct misc_config_s *g_misc_config_p;
static struct digi_config_s *g_digi_config_p;



#if __WIN32__
static unsigned __stdcall beacon_thread (void *arg);
#else
static void * beacon_thread (void *arg);
#endif

static int g_tracker_debug_level = 0;	// 1 for data from gps.
					// 2 + Smart Beaconing logic.
					// 3 + Send transmissions to log file.


void beacon_tracker_set_debug (int level)
{
	g_tracker_debug_level = level;
}



/*-------------------------------------------------------------------
 *
 * Name:        beacon_init
 *
 * Purpose:     Initialize the beacon process.
 *
 * Inputs:	pconfig		- misc. configuration from config file.
 *		pdigi		- digipeater configuration from config file.
 *				  Use to obtain "mycall" for each channel.
 *
 *
 * Outputs:	Remember required information for future use.
 *
 * Description:	Initialize the queue to be empty and set up other
 *		mechanisms for sharing it between different threads.
 *
 *		Start up xmit_thread to actually send the packets
 *		at the appropriate time.
 *
 *--------------------------------------------------------------------*/



void beacon_init (struct misc_config_s *pconfig, struct digi_config_s *pdigi)
{
	time_t now;
	int j;
	int count;
#if __WIN32__
	HANDLE beacon_th;
#else
	pthread_t beacon_tid;
#endif



#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("beacon_init ( ... )\n");
#endif



/* 
 * Save parameters for later use.
 */
	g_misc_config_p = pconfig;
	g_digi_config_p = pdigi;

/*
 * Precompute the packet contents so any errors are 
 * Reported once at start up time rather than for each transmission.
 * If a serious error is found, set type to BEACON_IGNORE and that
 * table entry should be ignored later on.
 */
	for (j=0; j<g_misc_config_p->num_beacons; j++) {
	  int chan = g_misc_config_p->beacon[j].sendto_chan;

	  if (chan < 0) chan = 0;	/* For IGate, use channel 0 call. */

	  if (chan < pdigi->num_chans) {

	    if (strlen(pdigi->mycall[chan]) > 0 && strcasecmp(pdigi->mycall[chan], "NOCALL") != 0) {

              switch (g_misc_config_p->beacon[j].btype) {

	        case BEACON_OBJECT:

		  /* Object name is required. */

		  if (strlen(g_misc_config_p->beacon[j].objname) == 0) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Config file, line %d: OBJNAME is required for OBEACON.\n", g_misc_config_p->beacon[j].lineno);
		    g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
		    continue;
		  }
		  /* Fall thru.  Ignore any warning about missing break. */

	        case BEACON_POSITION:

		  /* Location is required. */

		  if (g_misc_config_p->beacon[j].lat == G_UNKNOWN || g_misc_config_p->beacon[j].lon == G_UNKNOWN) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Config file, line %d: Latitude and longitude are required.\n", g_misc_config_p->beacon[j].lineno);
		    g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
		    continue;
		  }	
		  break;

	        case BEACON_TRACKER:

#if defined(ENABLE_GPS) || defined(DEBUG_SIM)
		  g_using_gps++;
#else
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Config file, line %d: GPS tracker feature is not enabled.\n", g_misc_config_p->beacon[j].lineno);
	   	  g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
	   	  continue;
#endif
		  break;

	        case BEACON_CUSTOM:

		  /* INFO is required. */

		  if (g_misc_config_p->beacon[j].custom_info == NULL) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Config file, line %d: INFO is required for custom beacon.\n", g_misc_config_p->beacon[j].lineno);
		    g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
		    continue;
		  }	
		  break;

	        case BEACON_IGNORE:
		  break;
	      }
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: MYCALL must be set for beacon on channel %d. \n", g_misc_config_p->beacon[j].lineno, chan);
	      g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
	    }
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Config file, line %d: Invalid channel number %d for beacon. \n", g_misc_config_p->beacon[j].lineno, chan);
	    g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
	  }
	}

/*
 * Calculate next time for each beacon.
 */

	now = time(NULL);

	for (j=0; j<g_misc_config_p->num_beacons; j++) {
#if DEBUG

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("beacon[%d] chan=%d, delay=%d, every=%d\n",
		j,
		g_misc_config_p->beacon[j].chan,
		g_misc_config_p->beacon[j].delay,
		g_misc_config_p->beacon[j].every);
#endif
	  g_misc_config_p->beacon[j].next = now + g_misc_config_p->beacon[j].delay;
	}


/*
 * Connect to GPS receiver if any tracker beacons are configured.
 * If open fails, disable all tracker beacons.
 */

#if DEBUG_SIM

	g_using_gps = 1;
	
#elif ENABLE_GPS

	if (g_using_gps > 0) {
	  int err;

	  err = dwgps_init();
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("All tracker beacons disabled.\n");
	    g_using_gps = 0;

	    for (j=0; j<g_misc_config_p->num_beacons; j++) {
              if (g_misc_config_p->beacon[j].btype == BEACON_TRACKER) {
		g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
	      }
	    }
	  }

	}
#endif


/* 
 * Start up thread for processing only if at least one is valid.
 */

	count = 0;
	for (j=0; j<g_misc_config_p->num_beacons; j++) {
          if (g_misc_config_p->beacon[j].btype != BEACON_IGNORE) {
	    count++;
	  }
	}

	if (count >= 1) {

#if __WIN32__
	  beacon_th = (HANDLE)_beginthreadex (NULL, 0, &beacon_thread, NULL, 0, NULL);
	  if (beacon_th == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Could not create beacon thread\n");
	    return;
	  }
#else
	  int e;

	  e = pthread_create (&beacon_tid, NULL, beacon_thread, (void *)0);
	  if (e != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    perror("Could not create beacon thread");
	    return;
	  }
#endif
	}


} /* end beacon_init */





/*-------------------------------------------------------------------
 *
 * Name:        beacon_thread
 *
 * Purpose:     Transmit beacons when it is time.
 *
 * Inputs:	g_misc_config_p->beacon
 *
 * Outputs:	g_misc_config_p->beacon[].next_time
 *
 * Description:	Go to sleep until it is time for the next beacon.
 *		Transmit any beacons scheduled for now.
 *		Repeat.
 *
 *--------------------------------------------------------------------*/

#define MIN(x,y) ((x) < (y) ? (x) : (y))


/* Difference between two angles. */

static inline float heading_change (float a, float b)
{
	float diff;

	diff = fabs(a - b);
	if (diff <= 180.)
	  return (diff);
	else
	  return (360. - diff);
}


#if __WIN32__
static unsigned __stdcall beacon_thread (void *arg)
#else
static void * beacon_thread (void *arg)
#endif
{
	int j;
	time_t earliest;
	time_t now;

/*
 * Information from GPS.
 */
	int fix = 0;			/* 0 = none, 2 = 2D, 3 = 3D */
	double my_lat = 0;		/* degrees */
	double my_lon = 0;
	float  my_course = 0;		/* degrees */
	float  my_speed_knots = 0;
	float  my_speed_mph = 0;
	float  my_alt_m = G_UNKNOWN;		/* meters */
	int    my_alt_ft = G_UNKNOWN;

/*
 * SmartBeaconing state.
 */
	time_t sb_prev_time = 0;	/* Time of most recent transmission. */
	float sb_prev_course = 0;	/* Most recent course reported. */
	//float sb_prev_speed_mph;	/* Most recent speed reported. */
	int sb_every;			/* Calculated time between transmissions. */


#if DEBUG
	struct tm tm;
	char hms[20];

	now = time(NULL);
	localtime_r (&now, &tm);
	strftime (hms, sizeof(hms), "%H:%M:%S", &tm);
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("beacon_thread: started %s\n", hms);
#endif
	now = time(NULL);

	while (1) {

	  assert (g_misc_config_p->num_beacons >= 1);

/* 
 * Sleep until time for the earliest scheduled or
 * the soonest we could transmit due to corner pegging.
 */
	  
	  earliest = g_misc_config_p->beacon[0].next;
	  for (j=1; j<g_misc_config_p->num_beacons; j++) {
	    if (g_misc_config_p->beacon[j].btype == BEACON_IGNORE)
	      continue;
	    earliest = MIN(g_misc_config_p->beacon[j].next, earliest);
	  }

	  if (g_misc_config_p->sb_configured && g_using_gps) {
	    earliest = MIN(now + g_misc_config_p->sb_turn_time, earliest);
            earliest = MIN(now + g_misc_config_p->sb_fast_rate, earliest);
	  }

	  if (earliest > now) {
	    SLEEP_SEC (earliest - now);
	  }

/*
 * Woke up.  See what needs to be done.
 */
	  now = time(NULL);

#if DEBUG
	  localtime_r (&now, &tm);
	  strftime (hms, sizeof(hms), "%H:%M:%S", &tm);
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("beacon_thread: woke up %s\n", hms);
#endif

/*
 * Get information from GPS if being used.
 * This needs to be done before the next scheduled tracker
 * beacon because corner pegging make it sooner. 
 */

#if DEBUG_SIM
	  FILE *fp;
	  char cs[40];

	  fp = fopen ("c:\\cygwin\\tmp\\cs", "r");
	  if (fp != NULL) {
	    fscanf (fp, "%f %f", &my_course, &my_speed_knots);
	    fclose (fp);
	  }
	  else {
	    fprintf (stderr, "Can't read /tmp/cs.\n");
	  }
	  fix = 3;
	  my_speed_mph = DW_KNOTS_TO_MPH(my_speed_knots);
	  my_lat = 42.99;
	  my_lon = 71.99;
	  my_alt_m = 100;
#else
	  if (g_using_gps) {

	    fix = dwgps_read (&my_lat, &my_lon, &my_speed_knots, &my_course, &my_alt_m);
	    my_speed_mph = DW_KNOTS_TO_MPH(my_speed_knots);

	    if (g_tracker_debug_level >= 1) {
	      struct tm tm;
	      char hms[20];

	      localtime_r (&now, &tm);
	      strftime (hms, sizeof(hms), "%H:%M:%S", &tm);
	      text_color_set(DW_COLOR_DEBUG);
	      if (fix == 3) {
	        dw_printf ("%s  3D, %.6f, %.6f, %.1f mph, %.0f\xc2\xb0, %.1f m\n", hms, my_lat, my_lon, my_speed_mph, my_course, my_alt_m);
	      }
	      else if (fix == 2) {
	        dw_printf ("%s  2D, %.6f, %.6f, %.1f mph, %.0f\xc2\xb0\n", hms, my_lat, my_lon, my_speed_mph, my_course);
	      }
	      else {
	        dw_printf ("%s  No GPS fix\n", hms);
	      }
	    }

	    /* Transmit altitude only if 3D fix and user asked for it. */

	    my_alt_ft = G_UNKNOWN;
	    if (fix >= 3 && my_alt_m != G_UNKNOWN && g_misc_config_p->beacon[j].alt_m != G_UNKNOWN) {
	      my_alt_ft = DW_METERS_TO_FEET(my_alt_m);
	    }

	    /* Don't complain here for no fix. */
	    /* Possibly at the point where about to transmit. */
	  }
#endif

/*
 * Run SmartBeaconing calculation if configured and GPS data available.
 */
	  if (g_misc_config_p->sb_configured && g_using_gps && fix >= 2) {

	    if (my_speed_mph > g_misc_config_p->sb_fast_speed) {
	      sb_every = g_misc_config_p->sb_fast_rate;
	      if (g_tracker_debug_level >= 2) {
	      	 text_color_set(DW_COLOR_DEBUG);
		 dw_printf ("my speed %.1f > fast %d mph, interval = %d sec\n", my_speed_mph, g_misc_config_p->sb_fast_speed, sb_every);
	      } 
	    }
	    else if (my_speed_mph < g_misc_config_p->sb_slow_speed) {
	      sb_every = g_misc_config_p->sb_slow_rate;
	      if (g_tracker_debug_level >= 2) {
	      	 text_color_set(DW_COLOR_DEBUG);
		 dw_printf ("my speed %.1f < slow %d mph, interval = %d sec\n", my_speed_mph, g_misc_config_p->sb_slow_speed, sb_every);
	      } 
	    }
	    else {
	      /* Can't divide by 0 assuming sb_slow_speed > 0. */
	      sb_every = ( g_misc_config_p->sb_fast_rate * g_misc_config_p->sb_fast_speed ) / my_speed_mph;
	      if (g_tracker_debug_level >= 2) {
	      	 text_color_set(DW_COLOR_DEBUG);
		 dw_printf ("my speed %.1f mph, interval = %d sec\n", my_speed_mph, sb_every);
	      } 
	    }

#if DEBUG_SIM
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("SB: fast %d %d slow %d %d speed=%.1f every=%d\n",
			g_misc_config_p->sb_fast_speed, g_misc_config_p->sb_fast_rate,
			g_misc_config_p->sb_slow_speed, g_misc_config_p->sb_slow_rate,
			my_speed_mph, sb_every);
#endif 
	
/*
 * Test for "Corner Pegging" if moving.
 */
	    if (my_speed_mph >= 1.0) {
	      int turn_threshold = g_misc_config_p->sb_turn_angle + 
			g_misc_config_p->sb_turn_slope / my_speed_mph;

#if DEBUG_SIM
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("SB-moving: course %.0f  prev %.0f  thresh %d\n",
		my_course, sb_prev_course, turn_threshold);
#endif 
	      if (heading_change(my_course, sb_prev_course) > turn_threshold &&
		  now >= sb_prev_time + g_misc_config_p->sb_turn_time) {

	        if (g_tracker_debug_level >= 2) {
	      	   text_color_set(DW_COLOR_DEBUG);
		   dw_printf ("heading change (%.0f, %.0f) > threshold %d and %d since last >= turn time %d\n",
				my_course, sb_prev_course, turn_threshold, 
				(int)(now - sb_prev_time), g_misc_config_p->sb_turn_time);
	        } 

		/* Send it now. */
	        for (j=0; j<g_misc_config_p->num_beacons; j++) {
                  if (g_misc_config_p->beacon[j].btype == BEACON_TRACKER) {
		    g_misc_config_p->beacon[j].next = now;
	          }
	        }
	      }  /* significant change in direction */
	    }  /* is moving */
	  }  /* apply SmartBeaconing */
	    
      
	  for (j=0; j<g_misc_config_p->num_beacons; j++) {

	    if (g_misc_config_p->beacon[j].btype == BEACON_IGNORE)
	      continue;

	    if (g_misc_config_p->beacon[j].next <= now) {

	      int strict = 1;	/* Strict packet checking because they will go over air. */
	      char stemp[20];
	      char info[AX25_MAX_INFO_LEN];
	      char beacon_text[AX25_MAX_PACKET_LEN];
	      packet_t pp = NULL;
	      char mycall[AX25_MAX_ADDR_LEN];
	      int alt_ft;

/*
 * Obtain source call for the beacon.
 * This could potentially be different on different channels.
 * When sending to IGate server, use call from first radio channel.
 *
 * Check added in version 1.0a.  Previously used index of -1.
 *
 * Version 1.1 - channel should now be 0 for IGate.  
 * Type of destination is encoded separately.
 */
	      strcpy (mycall, "NOCALL");

	      assert (g_misc_config_p->beacon[j].sendto_chan >= 0);

	      strcpy (mycall, g_digi_config_p->mycall[g_misc_config_p->beacon[j].sendto_chan]);
	      
	      if (strlen(mycall) == 0 || strcmp(mycall, "NOCALL") == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("MYCALL not set for beacon in config file line %d.\n", g_misc_config_p->beacon[j].lineno);
		continue;
	      }

/* 
 * Prepare the monitor format header. 
 *
 * 	src > dest [ , via ]
 */

	      strcpy (beacon_text, mycall);
	      strcat (beacon_text, ">");

	      if (g_misc_config_p->beacon[j].dest != NULL) {
	        strcat (beacon_text, g_misc_config_p->beacon[j].dest);
	      } 
	      else {
	         sprintf (stemp, "%s%1d%1d", APP_TOCALL, MAJOR_VERSION, MINOR_VERSION);
	         strcat (beacon_text, stemp);
	      }

	      if (g_misc_config_p->beacon[j].via != NULL) {
	        strcat (beacon_text, ",");
	        strcat (beacon_text, g_misc_config_p->beacon[j].via);
	      }
	      strcat (beacon_text, ":");

/* 
 * Add the info part depending on beacon type. 
 */
	      switch (g_misc_config_p->beacon[j].btype) {

		case BEACON_POSITION:

		  alt_ft = DW_METERS_TO_FEET(g_misc_config_p->beacon[j].alt_m);

		  encode_position (g_misc_config_p->beacon[j].messaging, 
			g_misc_config_p->beacon[j].compress, g_misc_config_p->beacon[j].lat, g_misc_config_p->beacon[j].lon, alt_ft,
			g_misc_config_p->beacon[j].symtab, g_misc_config_p->beacon[j].symbol, 
			g_misc_config_p->beacon[j].power, g_misc_config_p->beacon[j].height, g_misc_config_p->beacon[j].gain, g_misc_config_p->beacon[j].dir,
			0, 0, /* course, speed */	
			g_misc_config_p->beacon[j].freq, g_misc_config_p->beacon[j].tone, g_misc_config_p->beacon[j].offset,
			g_misc_config_p->beacon[j].comment,
			info);
		  strcat (beacon_text, info);
	          g_misc_config_p->beacon[j].next = now + g_misc_config_p->beacon[j].every;
		  break;

		case BEACON_OBJECT:

		  encode_object (g_misc_config_p->beacon[j].objname, g_misc_config_p->beacon[j].compress, 0, g_misc_config_p->beacon[j].lat, g_misc_config_p->beacon[j].lon, 
			g_misc_config_p->beacon[j].symtab, g_misc_config_p->beacon[j].symbol, 
			g_misc_config_p->beacon[j].power, g_misc_config_p->beacon[j].height, g_misc_config_p->beacon[j].gain, g_misc_config_p->beacon[j].dir,
			0, 0, /* course, speed */
			g_misc_config_p->beacon[j].freq, g_misc_config_p->beacon[j].tone, g_misc_config_p->beacon[j].offset, g_misc_config_p->beacon[j].comment,
			info);
		  strcat (beacon_text, info);
	          g_misc_config_p->beacon[j].next = now + g_misc_config_p->beacon[j].every;
		  break;

		case BEACON_TRACKER:

		  if (fix >= 2) {
		    int coarse;		/* APRS encoder wants 1 - 360.  */
					/* 0 means none or unknown. */

		    coarse = (int)roundf(my_course);
		    if (coarse == 0) {
		      coarse = 360;
		    }
		    encode_position (g_misc_config_p->beacon[j].messaging, 
			g_misc_config_p->beacon[j].compress, 
			my_lat, my_lon, my_alt_ft,
			g_misc_config_p->beacon[j].symtab, g_misc_config_p->beacon[j].symbol, 
			g_misc_config_p->beacon[j].power, g_misc_config_p->beacon[j].height, g_misc_config_p->beacon[j].gain, g_misc_config_p->beacon[j].dir,
			coarse, (int)roundf(my_speed_knots),	
			g_misc_config_p->beacon[j].freq, g_misc_config_p->beacon[j].tone, g_misc_config_p->beacon[j].offset,
			g_misc_config_p->beacon[j].comment,
			info);
		    strcat (beacon_text, info);

		    /* Remember most recent tracker beacon. */

		    sb_prev_time = now;
		    sb_prev_course = my_course;
		    //sb_prev_speed_mph = my_speed_mph;

		    /* Calculate time for next transmission. */
	            if (g_misc_config_p->sb_configured) {
	              g_misc_config_p->beacon[j].next = now + sb_every;
	            }
	            else {
	              g_misc_config_p->beacon[j].next = now + g_misc_config_p->beacon[j].every;
	            }

		    /* Write to log file for testing. */
		    /* The idea is to run log2gpx and map the result rather than */
		    /* actually transmitting and relying on someone else to receive */
		    /* the signals. */

	            if (g_tracker_debug_level >= 3) {
		
		      decode_aprs_t A;

		      memset (&A, 0, sizeof(A));
	  	      A.g_freq   = G_UNKNOWN;
	  	      A.g_offset = G_UNKNOWN;
	  	      A.g_tone   = G_UNKNOWN;
	  	      A.g_dcs    = G_UNKNOWN;

		      strcpy (A.g_src, mycall);
		      A.g_symbol_table = g_misc_config_p->beacon[j].symtab;
		      A.g_symbol_code = g_misc_config_p->beacon[j].symbol;
		      A.g_lat = my_lat;
		      A.g_lon = my_lon;
		      A.g_speed = DW_KNOTS_TO_MPH(my_speed_knots);
		      A.g_course = coarse;
		      A.g_altitude = my_alt_ft;

		      /* Fake channel of 999 to distinguish from real data. */
		      log_write (999, &A, NULL, 0, 0);
		    }
	 	  }
	          else {
		    g_misc_config_p->beacon[j].next = now + 2;
	            continue;   /* No fix.  Try again in a couple seconds. */
		  }
		  break;

		case BEACON_CUSTOM:

		  if (g_misc_config_p->beacon[j].custom_info != NULL) {
	            strcat (beacon_text, g_misc_config_p->beacon[j].custom_info);
		  }
		  else {
		    text_color_set(DW_COLOR_ERROR);
	    	    dw_printf ("Internal error. custom_info is null. %s %d\n", __FILE__, __LINE__);
	            continue;
	          }
	          g_misc_config_p->beacon[j].next = now + g_misc_config_p->beacon[j].every;
		  break;

		case BEACON_IGNORE:		
	        default:
		  break;

	      } /* switch beacon type. */

/*
 * Parse monitor format into form for transmission.
 */	
	      pp = ax25_from_text (beacon_text, strict);

              if (pp != NULL) {

		/* Send to desired destination. */

	        switch (g_misc_config_p->beacon[j].sendto_type) {

	          case SENDTO_IGATE:


#if 1
	  	    text_color_set(DW_COLOR_XMIT);
	  	    dw_printf ("[ig] %s\n", beacon_text);
#endif
		    igate_send_rec_packet (0, pp);
		    ax25_delete (pp);
	            break;

		  case SENDTO_XMIT:
		  default:

	            tq_append (g_misc_config_p->beacon[j].sendto_chan, TQ_PRIO_1_LO, pp);
		    break;

		  case SENDTO_RECV:

		    // TODO:  Put into receive queue rather than calling directly.

		    app_process_rec_packet (g_misc_config_p->beacon[j].sendto_chan, 0, pp, -1, 0, "");
	            break; 
		}
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Failed to parse packet constructed from line %d.\n", g_misc_config_p->beacon[j].lineno);
	        dw_printf ("%s\n", beacon_text);
	      }

	    }  /* if time to send it */

	  }  /* for each configured beacon */

	}  /* do forever */

} /* end beacon_thread */

/* end beacon.c */
