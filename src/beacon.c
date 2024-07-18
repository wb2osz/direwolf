//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2015, 2016, 2017  John Langner, WB2OSZ
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

//#define DEBUG 1

#include "direwolf.h"


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <time.h>


#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "tq.h"
#include "xmit.h"
#include "config.h"
#include "version.h"
#include "encode_aprs.h"
#include "beacon.h"
#include "latlong.h"
#include "dwgps.h"
#include "log.h"
#include "dlq.h"
#include "aprs_tt.h"		// for dw_run_cmd - should relocate someday.
#include "mheard.h"


/*
 * Save pointers to configuration settings.
 */

static struct audio_s        *g_modem_config_p;
static struct misc_config_s  *g_misc_config_p;
static struct igate_config_s *g_igate_config_p;


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

static time_t sb_calculate_next_time (time_t now,
			float current_speed_mph, float current_course,
			time_t last_xmit_time, float last_xmit_course);

static void beacon_send (int j, dwgps_info_t *gpsinfo);


/*-------------------------------------------------------------------
 *
 * Name:        beacon_init
 *
 * Purpose:     Initialize the beacon process.
 *
 * Inputs:	pmodem		- Audio device and modem configuration.
 *				  Used only to find valid channels.
 *
 *		pconfig		- misc. configuration from config file.
 *				  Beacon stuff ended up here.
 *
 *		pigate		- IGate configuration.
 *				  Need this for calculating IGate statistics.
 *
 *
 * Outputs:	Remember required information for future use.
 *
 * Description:	Do some validity checking on the beacon configuration.
 *
 *		Start up beacon_thread to actually send the packets
 *		at the appropriate time.
 *
 *--------------------------------------------------------------------*/



void beacon_init (struct audio_s *pmodem, struct misc_config_s *pconfig, struct igate_config_s *pigate)
{
	time_t now;
	struct tm tm;
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
	g_modem_config_p = pmodem;
	g_misc_config_p = pconfig;
	g_igate_config_p = pigate;

/*
 * Precompute the packet contents so any errors are 
 * Reported once at start up time rather than for each transmission.
 * If a serious error is found, set type to BEACON_IGNORE and that
 * table entry should be ignored later on.
 */

// TODO: Better checking.
// We should really have a table for which keywords are are required,
// optional, or not allowed for each beacon type.  Options which
// are not applicable are often silently ignored, causing confusion.

	for (j=0; j<g_misc_config_p->num_beacons; j++) {
	  int chan = g_misc_config_p->beacon[j].sendto_chan;

	  if (chan < 0) chan = 0;	/* For IGate, use channel 0 call. */
	  if (chan >= MAX_TOTAL_CHANS) chan = 0;	// For ICHANNEL, use channel 0 call.

	  if (g_modem_config_p->chan_medium[chan] == MEDIUM_RADIO ||
	      g_modem_config_p->chan_medium[chan] == MEDIUM_NETTNC) {

	    if (strlen(g_modem_config_p->mycall[chan]) > 0 &&
			 strcasecmp(g_modem_config_p->mycall[chan], "N0CALL") != 0 &&
			 strcasecmp(g_modem_config_p->mycall[chan], "NOCALL") != 0) {

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

		  /* INFO and INFOCMD are only for Custom Beacon. */

		  if (g_misc_config_p->beacon[j].custom_info != NULL || g_misc_config_p->beacon[j].custom_infocmd != NULL) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Config file, line %d: INFO or INFOCMD are allowed only for custom beacon.\n", g_misc_config_p->beacon[j].lineno);
	            dw_printf ("INFO and INFOCMD allow you to specify contents of the Information field so it\n");
	            dw_printf ("so it would not make sense to use these with other beacon types which construct\n");
	            dw_printf ("the Information field. Perhaps you want to use COMMENT or COMMENTCMD option.\n");
		    //g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
		    continue;
		  }	
		  break;

	        case BEACON_TRACKER:

	          {
	            dwgps_info_t gpsinfo;
	            dwfix_t fix;

	            fix = dwgps_read (&gpsinfo);
		    if (fix == DWFIX_NOT_INIT) {

	              text_color_set(DW_COLOR_ERROR);
	              dw_printf ("Config file, line %d: GPS must be configured to use TBEACON.\n", g_misc_config_p->beacon[j].lineno);
	              g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
#if __WIN32__
	              dw_printf ("You must specify the GPSNMEA command in your configuration file.\n");
	              dw_printf ("This contains the name of the serial port where the receiver is connected.\n");
#else
	              dw_printf ("You must specify the source of the GPS data in your configuration file.\n");
	              dw_printf ("It can be either GPSD, meaning the gpsd daemon, or GPSNMEA for\n");
	              dw_printf ("for a serial port connection with exclusive use.\n");
#endif

	            }
	          }

		  /* INFO and INFOCMD are only for Custom Beacon. */

		  if (g_misc_config_p->beacon[j].custom_info != NULL || g_misc_config_p->beacon[j].custom_infocmd != NULL) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Config file, line %d: INFO or INFOCMD are allowed only for custom beacon.\n", g_misc_config_p->beacon[j].lineno);
	            dw_printf ("INFO and INFOCMD allow you to specify contents of the Information field so it\n");
	            dw_printf ("so it would not make sense to use these with other beacon types which construct\n");
	            dw_printf ("the Information field. Perhaps you want to use COMMENT or COMMENTCMD option.\n");
		    //g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
		    continue;
		  }	
		  break;

	        case BEACON_CUSTOM:

		  /* INFO or INFOCMD is required. */

		  if (g_misc_config_p->beacon[j].custom_info == NULL && g_misc_config_p->beacon[j].custom_infocmd == NULL) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Config file, line %d: INFO or INFOCMD is required for custom beacon.\n", g_misc_config_p->beacon[j].lineno);
		    g_misc_config_p->beacon[j].btype = BEACON_IGNORE;
		    continue;
		  }	
		  break;

	        case BEACON_IGATE:

		  /* Doesn't make sense if IGate is not configured. */

	          if (strlen(g_igate_config_p->t2_server_name) == 0 ||
	              strlen(g_igate_config_p->t2_login) == 0 ||
	              strlen(g_igate_config_p->t2_passcode) == 0) {

	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Config file, line %d: Doesn't make sense to use IBEACON without IGate Configured.\n", g_misc_config_p->beacon[j].lineno);
	            dw_printf ("IBEACON has been disabled.\n");
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
 * Calculate first time for each beacon from the 'slot' or 'delay' value.
 */

	now = time(NULL);
	localtime_r (&now, &tm);

	for (j=0; j<g_misc_config_p->num_beacons; j++) {
	  struct beacon_s *bp = & (g_misc_config_p->beacon[j]);
#if DEBUG

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("beacon[%d] chan=%d, delay=%d, slot=%d, every=%d\n",
		j,
		bp->sendto_chan,
		bp->delay,
		bp->slot,
		bp->every);
#endif

/*
 * If timeslots, there must be a full number of beacon intervals per hour.
 */
#define IS_GOOD(x) ((3600/(x))*(x) == 3600)

	  if (bp->slot != G_UNKNOWN) {

	    if ( ! IS_GOOD(bp->every)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: When using timeslots, there must be a whole number of beacon intervals per hour.\n", bp->lineno);

	      // Try to make it valid by adjusting up or down.

	      int n;
	      for (n=1; ; n++) {
	        int e;
	        e = bp->every + n;
	        if (e > 3600) {
	          bp->every = 3600;
	          break;
	        }
	        if (IS_GOOD(e)) {
	          bp->every = e;
	          break;
	        }
	        e = bp->every - n;
	        if (e < 1) {
	          bp->every = 1;	// Impose a larger minimum?
	          break;
	        }
	        if (IS_GOOD(e)) {
	          bp->every = e;
	          break;
	        }
	      }
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Config file, line %d: Time between slotted beacons has been adjusted to %d seconds.\n", bp->lineno, bp->every);
	    }
/*
 * Determine when next slot time will arrive.
 */
	    bp->delay = bp->slot - (tm.tm_min * 60 + tm.tm_sec);
	    while (bp->delay > bp->every) bp->delay -= bp->every;
	    while (bp->delay < 5) bp->delay += bp->every;
	  }

	  g_misc_config_p->beacon[j].next = now + g_misc_config_p->beacon[j].delay;
	}


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

	  e = pthread_create (&beacon_tid, NULL, beacon_thread, NULL);
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




#if __WIN32__
static unsigned __stdcall beacon_thread (void *arg)
#else
static void * beacon_thread (void *arg)
#endif
{
	int j;				/* Index into array of beacons. */
	time_t earliest;
	time_t now;			/* Current time. */
	int number_of_tbeacons;		/* Number of tracker beacons. */


/*
 * SmartBeaconing state.
 */
	time_t sb_prev_time = 0;	/* Time of most recent transmission. */
	float sb_prev_course = 0;	/* Most recent course reported. */


#if DEBUG
	struct tm tm;
	char hms[20];

	now = time(NULL);

	localtime_r (&now, &tm);

	strftime (hms, sizeof(hms), "%H:%M:%S", &tm);
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("beacon_thread: started %s\n", hms);
#endif

/*
 * See if any tracker beacons are configured.
 * No need to obtain GPS data if none.
 */

	number_of_tbeacons = 0;
	for (j=0; j<g_misc_config_p->num_beacons; j++) {
	  if (g_misc_config_p->beacon[j].btype == BEACON_TRACKER) {
	    number_of_tbeacons++;
	  }
	}

	now = time(NULL);

	while (1) {

	  dwgps_info_t gpsinfo;

/* 
 * Sleep until time for the earliest scheduled or
 * the soonest we could transmit due to corner pegging.
 */
	  
	  earliest = now + 60 * 60;
	  for (j=0; j<g_misc_config_p->num_beacons; j++) {
	    if (g_misc_config_p->beacon[j].btype != BEACON_IGNORE) {
	      earliest = MIN(g_misc_config_p->beacon[j].next, earliest);
	    }
	  }

	  if (g_misc_config_p->sb_configured && number_of_tbeacons > 0) {
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

	  if (number_of_tbeacons > 0) {

	    dwfix_t fix = dwgps_read (&gpsinfo);
	    float my_speed_mph = DW_KNOTS_TO_MPH(gpsinfo.speed_knots);

	    if (g_tracker_debug_level >= 1) {
	      struct tm tm;
	      char hms[20];


	      localtime_r (&now, &tm);
	      strftime (hms, sizeof(hms), "%H:%M:%S", &tm);
	      text_color_set(DW_COLOR_DEBUG);
	      if (fix == 3) {
	        dw_printf ("%s  3D, %.6f, %.6f, %.1f mph, %.0f\xc2\xb0, %.1f m\n", hms, gpsinfo.dlat, gpsinfo.dlon, my_speed_mph, gpsinfo.track, gpsinfo.altitude);
	      }
	      else if (fix == 2) {
	        dw_printf ("%s  2D, %.6f, %.6f, %.1f mph, %.0f\xc2\xb0\n", hms, gpsinfo.dlat, gpsinfo.dlon, my_speed_mph, gpsinfo.track);
	      }
	      else {
	        dw_printf ("%s  No GPS fix\n", hms);
	      }
	    }

	    /* Don't complain here for no fix. */
	    /* Possibly at the point where about to transmit. */

/*
 * Run SmartBeaconing calculation if configured and GPS data available.
 */
	    if (g_misc_config_p->sb_configured && fix >= DWFIX_2D) {

	      time_t tnext = sb_calculate_next_time (now, 
			DW_KNOTS_TO_MPH(gpsinfo.speed_knots), gpsinfo.track,
			sb_prev_time, sb_prev_course);

	      for (j=0; j<g_misc_config_p->num_beacons; j++) {
	        if (g_misc_config_p->beacon[j].btype == BEACON_TRACKER) {
	          /* Haven't thought about the consequences of SmartBeaconing */
	          /* and having more than one tbeacon configured. */
	          if (tnext < g_misc_config_p->beacon[j].next) {
	             g_misc_config_p->beacon[j].next = tnext;
	          }
	        }
	      }  /* Update next time if sooner. */
	    }  /* apply SmartBeaconing */
	  }  /* tbeacon(s) configured. */

/*
 * Send if the time has arrived.
 */
	  for (j=0; j<g_misc_config_p->num_beacons; j++) {

	    struct beacon_s *bp = & (g_misc_config_p->beacon[j]);

	    if (bp->btype == BEACON_IGNORE)
	      continue;

	    if (bp->next <= now) {

	      /* Send the beacon. */

	      beacon_send (j, &gpsinfo);

	      /* Calculate when the next one should be sent. */
	      /* Easy for fixed interval.  SmartBeaconing takes more effort. */

	      if (bp->btype == BEACON_TRACKER) {

	        if (gpsinfo.fix < DWFIX_2D) {
	          /* Fix not available so beacon was not sent. */

		  if (g_misc_config_p->sb_configured) {
	            /* Try again in a couple seconds. */
	            bp->next = now + 2;
	          }
	          else {
	            /* Stay with the schedule. */
	            /* Important for slotted.  Might reconsider otherwise. */
	            bp->next += bp->every;
	          }
	        }
	        else if (g_misc_config_p->sb_configured) {

		  /* Remember most recent tracker beacon. */
	          /* Compute next time if not turning. */

		  sb_prev_time = now;
		  sb_prev_course = gpsinfo.track;

	          bp->next = sb_calculate_next_time (now,
			DW_KNOTS_TO_MPH(gpsinfo.speed_knots), gpsinfo.track,
			sb_prev_time, sb_prev_course);
	        }
	        else {
	          /* Tracker beacon, fixed spacing. */
	          bp->next += bp->every;
	        }
	      }
	      else {
	        /* Non-tracker beacon, fixed spacing. */
		/* Increment by 'every' so slotted times come out right. */
	        /* i.e. Don't take relative to now in case there was some delay. */

	        bp->next += bp->every;

	        // https://github.com/wb2osz/direwolf/pull/301
	        // https://github.com/wb2osz/direwolf/pull/301
	        // This happens with a portable system with no Internet connection.
	        // On reboot, the time is in the past.
	        // After time gets set from GPS, all beacons from that interval are sent.
	        // FIXME:  This will surely break time slotted scheduling.
		// TODO: The correct fix will be using monotonic, rather than clock, time.

	        /* craigerl: if next beacon is scheduled in the past, then set next beacon relative to now (happens when NTP pushes clock AHEAD) */
	        /* fixme: if NTP sets clock BACK an hour, this thread will sleep for that hour */
	        if ( bp->next < now ) {
	            bp->next = now + bp->every;
	            text_color_set(DW_COLOR_INFO);
	            dw_printf("\nSystem clock appears to have jumped forward.  Beacon schedule updated.\n\n");
	        }
	      }

	    }  /* if time to send it */

	  }  /* for each configured beacon */

	}  /* do forever */

#if __WIN32__
	return(0);	/* unreachable but warning if not here. */
#else 
	return(NULL);
#endif

} /* end beacon_thread */


/*-------------------------------------------------------------------
 *
 * Name:        sb_calculate_next_time
 *
 * Purpose:     Calculate next transmission time using the SmartBeaconing algorithm.
 *
 * Inputs:	now			- Current time.
 *
 *		current_speed_mph	- Current speed from GPS.
 *				  	  Not expecting G_UNKNOWN but should check for it.
 *
 *		current_course		- Current direction of travel.
 *				  	  Could be G_UNKNOWN if stationary.
 *
 *		last_xmit_time		- Time of most recent transmission.
 *
 *		last_xmit_course	- Direction included in most recent transmission.
 *
 * Global In:	g_misc_config_p->
 *			sb_configured	TRUE if SmartBeaconing is configured.
 *			sb_fast_speed	MPH
 *			sb_fast_rate	seconds
 *			sb_slow_speed	MPH
 *			sb_slow_rate	seconds
 *			sb_turn_time	seconds
 *			sb_turn_angle	degrees
 *			sb_turn_slope	degrees * MPH
 *
 * Returns:	Time of next transmission.
 *		Could vary from now to sb_slow_rate in the future.
 *
 * Caution:	The algorithm is defined in MPH units.    GPS uses knots.
 *		The caller must be careful about using the proper conversions.
 *
 *--------------------------------------------------------------------*/

/* Difference between two angles. */

static float heading_change (float a, float b)
{
	float diff;

	diff = fabs(a - b);
	if (diff <= 180.)
	  return (diff);
	else
	  return (360. - diff);
}

static time_t sb_calculate_next_time (time_t now,
			float current_speed_mph, float current_course,
			time_t last_xmit_time, float last_xmit_course)
{
	int beacon_rate;
	time_t next_time;

/*
 * Compute time between beacons for travelling in a straight line.
 */

	if (current_speed_mph == G_UNKNOWN) {
	  beacon_rate = (int)roundf((g_misc_config_p->sb_fast_rate + g_misc_config_p->sb_slow_rate) / 2.);
	}
	else if (current_speed_mph > g_misc_config_p->sb_fast_speed) {
	  beacon_rate = g_misc_config_p->sb_fast_rate;
	}
	else if (current_speed_mph < g_misc_config_p->sb_slow_speed) {
	  beacon_rate = g_misc_config_p->sb_slow_rate;
	}
	else {
	  /* Can't divide by 0 assuming sb_slow_speed > 0. */
	  beacon_rate = (int)roundf(( g_misc_config_p->sb_fast_rate * g_misc_config_p->sb_fast_speed ) / current_speed_mph);
	}

	if (g_tracker_debug_level >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("SmartBeaconing: Beacon Rate = %d seconds for %.1f MPH\n", beacon_rate, current_speed_mph);
	}

	next_time = last_xmit_time + beacon_rate;

/*
 * Test for "Corner Pegging" if moving.
 */
	if (current_speed_mph != G_UNKNOWN && current_speed_mph >= 1.0 &&
		current_course != G_UNKNOWN && last_xmit_course != G_UNKNOWN) {

	  float change = heading_change(current_course, last_xmit_course);
	  float turn_threshold = g_misc_config_p->sb_turn_angle +
			g_misc_config_p->sb_turn_slope / current_speed_mph;

	  if (change > turn_threshold &&
		  now >= last_xmit_time + g_misc_config_p->sb_turn_time) {

	    if (g_tracker_debug_level >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("SmartBeaconing: Send now for heading change of %.0f\n", change);
	    }

	    next_time = now;
	  }
	}

	return (next_time);

} /* end sb_calculate_next_time */


/*-------------------------------------------------------------------
 *
 * Name:        beacon_send
 *
 * Purpose:     Transmit one beacon after it was determined to be time.
 *
 * Inputs:	j			Index into beacon configuration array below.
 *
 *		gpsinfo			Information from GPS.  Used only for TBEACON.
 *
 * Global In:	g_misc_config_p->beacon		Array of beacon configurations.
 *
 * Outputs:	Destination(s) specified:
 *		 - Transmit queue.
 *		 - IGate.
 *		 - Simulated reception.
 *
 * Description:	Prepare text in monitor format.
 *		Convert to packet object.
 *		Send to desired destination(s).
 *
 *--------------------------------------------------------------------*/

static void beacon_send (int j, dwgps_info_t *gpsinfo)
{

	struct beacon_s *bp = & (g_misc_config_p->beacon[j]);

	      int strict = 1;	/* Strict packet checking because they will go over air. */
	      char stemp[20];
	      char info[AX25_MAX_INFO_LEN];
	      char beacon_text[AX25_MAX_PACKET_LEN];
	      packet_t pp = NULL;
	      char mycall[AX25_MAX_ADDR_LEN];

	      char super_comment[AX25_MAX_INFO_LEN];	// Fixed part + any dynamic part.

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
	      strlcpy (mycall, "NOCALL", sizeof(mycall));

	      assert (bp->sendto_chan >= 0);

	      if (g_modem_config_p->chan_medium[bp->sendto_chan] == MEDIUM_IGATE) {	// ICHANNEL uses chan 0 mycall.
									// TODO: Maybe it should be allowed to have own.
	        strlcpy (mycall, g_modem_config_p->mycall[0], sizeof(mycall));
	      }
	      else {
	        strlcpy (mycall, g_modem_config_p->mycall[bp->sendto_chan], sizeof(mycall));
	      }
	      
	      if (strlen(mycall) == 0 || strcmp(mycall, "NOCALL") == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("MYCALL not set for beacon to chan %d in config file line %d.\n", bp->sendto_chan, bp->lineno);
		return;
	      }

/* 
 * Prepare the monitor format header. 
 *
 * 	src > dest [ , via ]
 */

	      if (bp->source != NULL) {
	        strlcpy (beacon_text, bp->source, sizeof(beacon_text));
	      }
	      else {
	        strlcpy (beacon_text, mycall, sizeof(beacon_text));
	      }
	      strlcat (beacon_text, ">", sizeof(beacon_text));

	      if (bp->dest != NULL) {
	        strlcat (beacon_text, bp->dest, sizeof(beacon_text));
	      } 
	      else {
	         snprintf (stemp, sizeof(stemp), "%s%1d%1d", APP_TOCALL, MAJOR_VERSION, MINOR_VERSION);
	         strlcat (beacon_text, stemp, sizeof(beacon_text));
	      }

	      if (bp->via != NULL) {
	        strlcat (beacon_text, ",", sizeof(beacon_text));
	        strlcat (beacon_text, bp->via, sizeof(beacon_text));
	      }
	      strlcat (beacon_text, ":", sizeof(beacon_text));


/*
 * If the COMMENTCMD option was specified, run specified command to get variable part.
 * Result is any fixed part followed by any variable part.
 */

// TODO: test & document.

	      strlcpy (super_comment, "", sizeof(super_comment));
	      if (bp->comment != NULL) {
	        strlcpy (super_comment, bp->comment, sizeof(super_comment));
	      }

	      if (bp->commentcmd != NULL) {
	        char var_comment[AX25_MAX_INFO_LEN];
	        int k;

	        /* Run given command to get variable part of comment. */

	        k = dw_run_cmd (bp->commentcmd, 2, var_comment, sizeof(var_comment));
	        if (k > 0) {
	          strlcat (super_comment, var_comment, sizeof(super_comment));
	        }
	        else {
		  text_color_set(DW_COLOR_ERROR);
	          dw_printf ("xBEACON, config file line %d, COMMENTCMD failure.\n", bp->lineno);
	        }
	      }


/* 
 * Add the info part depending on beacon type. 
 */
	      switch (bp->btype) {

		case BEACON_POSITION:

		  encode_position (bp->messaging, bp->compress,
			bp->lat, bp->lon, bp->ambiguity,
			(int)roundf(DW_METERS_TO_FEET(bp->alt_m)),
			bp->symtab, bp->symbol,
			bp->power, bp->height, bp->gain, bp->dir,
			G_UNKNOWN, G_UNKNOWN, /* course, speed */
			bp->freq, bp->tone, bp->offset,
			super_comment,
			info, sizeof(info));
		  strlcat (beacon_text, info, sizeof(beacon_text));
		  break;

		case BEACON_OBJECT:

		  encode_object (bp->objname, bp->compress, 1, bp->lat, bp->lon, bp->ambiguity,
			bp->symtab, bp->symbol,
			bp->power, bp->height, bp->gain, bp->dir,
			G_UNKNOWN, G_UNKNOWN, /* course, speed */
			bp->freq, bp->tone, bp->offset, super_comment,
			info, sizeof(info));
		  strlcat (beacon_text, info, sizeof(beacon_text));
		  break;

		case BEACON_TRACKER:

		  if (gpsinfo->fix >= DWFIX_2D) {

		    int coarse;		/* Round to nearest integer. retaining unknown state. */
	            int my_alt_ft;

	            /* Transmit altitude only if user asked for it. */
		    /* A positive altitude in the config file enables */
	            /* transmission of altitude from GPS. */

	            my_alt_ft = G_UNKNOWN;
	            if (gpsinfo->fix >= 3 && gpsinfo->altitude != G_UNKNOWN && bp->alt_m > 0) {
	              my_alt_ft = (int)roundf(DW_METERS_TO_FEET(gpsinfo->altitude));
	            }

		    coarse = G_UNKNOWN;
		    if (gpsinfo->track != G_UNKNOWN) {
	              coarse = (int)roundf(gpsinfo->track);
	            }

		    encode_position (bp->messaging, bp->compress,
			gpsinfo->dlat, gpsinfo->dlon, bp->ambiguity, my_alt_ft,
			bp->symtab, bp->symbol,
			bp->power, bp->height, bp->gain, bp->dir,
			coarse, (int)roundf(gpsinfo->speed_knots),
			bp->freq, bp->tone, bp->offset,
			super_comment,
			info, sizeof(info));
		    strlcat (beacon_text, info, sizeof(beacon_text));

		    /* Write to log file for testing. */
		    /* The idea is to run log2gpx and map the result rather than */
		    /* actually transmitting and relying on someone else to receive */
		    /* the signals. */

	            if (g_tracker_debug_level >= 3) {
		
		      decode_aprs_t A;
		      alevel_t alevel;

		      memset (&A, 0, sizeof(A));
	  	      A.g_freq   = G_UNKNOWN;
	  	      A.g_offset = G_UNKNOWN;
	  	      A.g_tone   = G_UNKNOWN;
	  	      A.g_dcs    = G_UNKNOWN;

		      strlcpy (A.g_src, mycall, sizeof(A.g_src));
		      A.g_symbol_table = bp->symtab;
		      A.g_symbol_code = bp->symbol;
		      A.g_lat = gpsinfo->dlat;
		      A.g_lon = gpsinfo->dlon;
		      A.g_speed_mph = DW_KNOTS_TO_MPH(gpsinfo->speed_knots);
		      A.g_course = coarse;
		      A.g_altitude_ft = DW_METERS_TO_FEET(gpsinfo->altitude);

		      /* Fake channel of 999 to distinguish from real data. */
		      memset (&alevel, 0, sizeof(alevel));
		      log_write (999, &A, NULL, alevel, 0);
		    }
	 	  }
	          else {
	            return;   /* No fix.  Skip this time. */
		  }
		  break;

		case BEACON_CUSTOM:

		  if (bp->custom_info != NULL) {

		    /* Fixed handcrafted text. */

	            strlcat (beacon_text, bp->custom_info, sizeof(beacon_text));
		  }
		  else if (bp->custom_infocmd != NULL) {
		    char info_part[AX25_MAX_INFO_LEN];
		    int k;

	            /* Run given command to obtain the info part for packet. */

		    k = dw_run_cmd (bp->custom_infocmd, 2, info_part, sizeof(info_part));
		    if (k > 0) {
	              strlcat (beacon_text, info_part, sizeof(beacon_text));
	            }
	            else {
		      text_color_set(DW_COLOR_ERROR);
	              dw_printf ("CBEACON, config file line %d, INFOCMD failure.\n", bp->lineno);
		      strlcpy (beacon_text, "", sizeof(beacon_text));  // abort!
	            }
		  }
		  else {
		    text_color_set(DW_COLOR_ERROR);
	    	    dw_printf ("Internal error. custom_info is null. %s %d\n", __FILE__, __LINE__);
		    strlcpy (beacon_text, "", sizeof(beacon_text));  // abort!
	          }
		  break;

		case BEACON_IGATE:

	          {
	            int last_minutes = 30;
	            char stuff[256];

		    snprintf (stuff, sizeof(stuff), "<IGATE,MSG_CNT=%d,PKT_CNT=%d,DIR_CNT=%d,LOC_CNT=%d,RF_CNT=%d,UPL_CNT=%d,DNL_CNT=%d",
						igate_get_msg_cnt(),
						igate_get_pkt_cnt(),
						mheard_count(0,last_minutes),
						mheard_count(g_igate_config_p->max_digi_hops,last_minutes),
						mheard_count(8,last_minutes),
						igate_get_upl_cnt(),
						igate_get_dnl_cnt());

		    strlcat (beacon_text, stuff, sizeof(beacon_text));
	          }
		  break;

		case BEACON_IGNORE:		
	        default:
		  break;

	      } /* switch beacon type. */

/*
 * Parse monitor format into form for transmission.
 */	
	      if (strlen(beacon_text) == 0) {
		return;
	      }
	      
	      pp = ax25_from_text (beacon_text, strict);

              if (pp != NULL) {

		/* Send to desired destination. */

	        alevel_t alevel;


	        switch (bp->sendto_type) {

	          case SENDTO_IGATE:

	  	    text_color_set(DW_COLOR_XMIT);
	  	    dw_printf ("[ig] %s\n", beacon_text);

		    igate_send_rec_packet (-1, pp);	// Channel -1 to avoid RF>IS filtering.
		    ax25_delete (pp);
	            break;

		  case SENDTO_XMIT:
		  default:

	            tq_append (bp->sendto_chan, TQ_PRIO_1_LO, pp);
		    break;

		  case SENDTO_RECV:

	            /* Simulated reception from radio. */

		    memset (&alevel, 0xff, sizeof(alevel));
	            dlq_rec_frame (bp->sendto_chan, 0, 0, pp, alevel, 0, 0, "");
	            break; 
		}
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Config file: Failed to parse packet constructed from line %d.\n", bp->lineno);
	        dw_printf ("%s\n", beacon_text);
	      }

} /* end beacon_send */


/* end beacon.c */
