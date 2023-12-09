//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015, 2020, 2022  John Langner, WB2OSZ
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
 * Module:      dwgpsd.c
 *
 * Purpose:   	Interface to location data, i.e. GPS receiver.
 *		
 * Description:	For Linux, we would normally want to use gpsd and libgps.
 *		This allows multiple applications to access the GPS data,
 *		without fighting over the same serial port, and has the 
 *		extra benefit that the system clock can be set from the GPS signal.
 *
 * Reference:	http://www.catb.org/gpsd/
 *
 *---------------------------------------------------------------*/


#include "direwolf.h"


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <stddef.h>

#if __WIN32__
#error Not for Windows
#endif

#if ENABLE_GPSD

#include <gps.h>



// An API incompatibility was introduced with API version 7.
// and again with 9.
// and again with 10.
// We deal with it by using a bunch of conditional code such as:
//	#if GPSD_API_MAJOR_VERSION >= 9


// release	lib version	API	Raspberry Pi OS		Testing status
// 3.22		28		11	bullseye		OK.
// 3.23		29		12				OK.
// 3.25		30		14				OK, Jan. 2023


// Previously the compilation would fail if the API version was later
// than the last one tested.  Now it is just a warning because it changes so
// often but more recent versions have not broken backward compatibility.

#define MAX_TESTED_VERSION 14

#if (GPSD_API_MAJOR_VERSION < 5) || (GPSD_API_MAJOR_VERSION > MAX_TESTED_VERSION)
#pragma message "Your version of gpsd might be incompatible with this application."
#pragma message "The libgps application program interface (API) often"
#pragma message "changes to be incompatible with earlier versions."
// I could not figure out how to do value substitution here.
#pragma message "You have libgpsd API version GPSD_API_MAJOR_VERSION."
#pragma message "The last that has been tested is MAX_TESTED_VERSION."
#pragma message "Even if this builds successfully, it might not run properly."
#endif


/*
 * Information for interface to gpsd daemon.
 */

static struct gps_data_t gpsdata;

#endif   /* ENABLE_GPSD */


#include "textcolor.h"
#include "dwgps.h"
#include "dwgpsd.h"


#if ENABLE_GPSD

static int s_debug = 0;		/* Enable debug output. */
				/* >= 1 show results from dwgps_read. */
				/* >= 2 show updates from GPS. */

static void * read_gpsd_thread (void *arg);

#endif



/*-------------------------------------------------------------------
 *
 * Name:        dwgpsd_init
 *
 * Purpose:    	Initialize the GPS interface.
 *
 * Inputs:	pconfig		Configuration settings.  This includes
 *				host name or address for network connection.
 *
 *		debug	- If >= 1, print results when dwgps_read is called.
 *				(In different file.)
 *
 *			  If >= 2, location updates are also printed.
 *				(In this file.)
 *		
 * Returns:	1 = success
 *		0 = nothing to do  (no host specified in config)
 *		-1 = failure
 *
 * Description:	- Establish socket connection with gpsd.
 *		- Start up thread to process incoming data.
 *		  It reads from the daemon and deposits into
 *		  shared region via dwgps_put_data.
 *
 * 		The application calls dwgps_read to get the most 
 *		recent information.			
 *
 *--------------------------------------------------------------------*/

/*
 * Historical notes:
 *
 * Originally, I wanted to use the shared memory interface to gpsd
 * because it is simpler and more efficient.  Just access it when we
 * actually need the data and we don't have a lot of extra unnecessary
 * busy work going on constantly polling it when we don't need the information.
 *
 * The current version of gpsd, supplied with Raspian (Wheezy), is 3.6 from back in 
 * May 2012, is missing support for the shared memory interface.  
 * 
 * I tried to download a newer source and build with shared memory support
 * but ran into a couple other issues.
 * 
 * 	sudo apt-get install libncurses5-dev
 * 	sudo apt-get install scons
 * 	cd ~
 * 	wget http://download-mirror.savannah.gnu.org/releases/gpsd/gpsd-3.11.tar.gz
 * 	tar xfz gpsd-3.11.tar.gz
 * 	cd gpsd-3.11
 * 	scons prefix=/usr libdir=lib/arm-linux-gnueabihf shm_export=True python=False
 * 	sudo scons udev-install
 *
 * For now, we will use the socket interface.  Maybe get back to this again someday.
 *
 * Update:  January 2016.
 *
 *	I'm told that the shared memory interface might work in Raspian, Jessie version.
 *	Haven't tried it yet.
 *
 * June 2020:  This is how to build the most recent.
 *
 * 	Based on https://www.satsignal.eu/raspberry-pi/UpdatingGPSD.html
 *
 * 	git clone https://gitlab.com/gpsd/gpsd.git  gpsd-gitlab
 * 	cd gpsd-gitlab
 * 	scons --config=force
 * 	scons
 * 	sudo scons install
 *
 *	The problem we have here is that the library is put in /usr/local/lib and direwolf
 *	can't find it there.  Solution  is to define environment variable:
 *
 *	export LD_LIBRARY_PATH=/use/local/lib
 *
 * January 2023: Now using 64 bit Raspberry Pi OS, bullseye.
 * See   https://gitlab.com/gpsd/gpsd/-/blob/master/build.adoc
 * Try to install in proper library place so we don't have to mess with LD_LIBRARY_PATH.
 *
 *      (Remove any existing gpsd first so we are not mixing mismatched pieces.)
 *
 * 	sudo apt-get install libncurses5-dev
 *	sudo apt-get install gtk+-3.0
 *
 * 	git clone https://gitlab.com/gpsd/gpsd.git  gpsd-gitlab
 * 	cd gpsd-gitlab
 * 	scons prefix=/usr libdir=lib/aarch64-linux-gnu
 *	[ scons check ]
 *	sudo scons udev-install
 *	
 */



int dwgpsd_init (struct misc_config_s *pconfig, int debug)
{

#if ENABLE_GPSD

	pthread_t read_gps_tid;
	int e;
	int err;
	int arg = 0;
	char sport[12];

	s_debug = debug;

	if (s_debug >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dwgpsd_init()\n");
	}

/* 
 * Socket interface to gpsd.
 */

	if (strlen(pconfig->gpsd_host) == 0) {

	  /* Nothing to do.  Leave initial fix value of error. */
	  return (0);
	}	  

	snprintf (sport, sizeof(sport), "%d", pconfig->gpsd_port);
	err = gps_open (pconfig->gpsd_host, sport, &gpsdata);
	if (err != 0) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unable to connect to GPSD stream at %s:%s.\n", pconfig->gpsd_host, sport);
	  dw_printf ("%s\n", gps_errstr(errno));

	  return (-1);
	}

	gps_stream(&gpsdata, WATCH_ENABLE | WATCH_JSON, NULL);

	e = pthread_create (&read_gps_tid, NULL, read_gpsd_thread, (void *)(ptrdiff_t)arg);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Could not create GPS reader thread");
	  return (-1);
	}

/* success */

	return (1);


#else	/* end ENABLE_GPSD */

	// Shouldn't be here. 

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("GPSD interface not enabled in this version.\n");
	dw_printf ("See documentation on how to rebuild with ENABLE_GPSD.\n");

	return (-1);

#endif

}  /* end dwgps_init */


/*-------------------------------------------------------------------
 *
 * Name:        read_gpsd_thread
 *
 * Purpose:     Read information from GPSD, as it becomes available, and
 *		store in memory shared with dwgps_read.
 *
 * Inputs:	arg		- not used
 *
 *--------------------------------------------------------------------*/

#define TIMEOUT 15

#if ENABLE_GPSD

static void * read_gpsd_thread (void *arg)
{
	dwgps_info_t info;

	if (s_debug >= 1) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("starting read_gpsd_thread (%d)\n", (int)(long)arg);
	}

	dwgps_clear (&info);
	info.fix = DWFIX_NOT_SEEN;	/* clear not init state. */
	if (s_debug >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dwgps_print ("GPSD: ", &info);
	}
	dwgps_set_data (&info);

	while (1) {

// Example code found here:
// https://lists.nongnu.org/archive/html/gpsd-dev/2017-11/msg00001.html

          if ( ! gps_waiting(&gpsdata, TIMEOUT * 1000000)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("------------------------------------------\n");
	    dw_printf ("dwgpsd: Timeout waiting for GPS data.\n");
	    dw_printf ("Is GPSD daemon running?\n");
	    dw_printf ("Troubleshooting tip:  Try running cgps or xgps.\n");
	    dw_printf ("------------------------------------------\n");
	    info.fix = DWFIX_ERROR;
	    SLEEP_MS(5000);
	    continue;
	  }

// https://github.com/wb2osz/direwolf/issues/196
// https://bugzilla.redhat.com/show_bug.cgi?id=1674812

// gps_read has two new parameters in API version 7.
// It looks like this could be used to obtain the JSON message from the daemon.
// Specify NULL, instead of message buffer space, if this is not desired.
// Why couldn't they add a new function instead of introducing incompatibility?

#if GPSD_API_MAJOR_VERSION >= 7
	  if (gps_read (&gpsdata, NULL, 0) == -1) {
#else
	  if (gps_read (&gpsdata) == -1) {
#endif
	    text_color_set(DW_COLOR_ERROR);

	    dw_printf ("------------------------------------------\n");
	    dw_printf ("GPSD: Lost communication with gpsd server.\n");
	    dw_printf ("------------------------------------------\n");

	    info.fix = DWFIX_ERROR;
	    if (s_debug >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      dwgps_print ("GPSD: ", &info);
	    }
	    dwgps_set_data (&info);

	    break;   // Jump out of loop and terminate thread.
	  }

#if GPSD_API_MAJOR_VERSION >= 9

// The gps.h revision history says:
//	 *       mark altitude in gps_fix_t as deprecated and undefined
// This seems really stupid to me.
// If it is deprecated and undefined then take it out.  Someone trying to use
// it would get a compile error and know that something needs to be done.
// Instead we all just go merrily on our way using a field that is [allegedly] undefined.
// Why not simply add more variables with different definitions of altitude
// and keep the original variable working as it always did?
// If it is truly undefined, as the comment would have us believe, numerous
// people will WASTE VAST AMOUNTS OF TIME pondering why altitude is now broken in
// their applications.

#define stupid_altitude altMSL
#else
#define stupid_altitude altitude
#endif

#if GPSD_API_MAJOR_VERSION >= 10

// They did it again.  Whimsical incompatibilities that cause
// pain and aggravation for everyone trying to use this library.
//
//	error: ‘struct gps_data_t’ has no member named ‘status’
//
// Yes, I can understand that it is a more logical place but it breaks
// all existing code that uses this.
// I'm really getting annoyed about wasting so much time on keeping up with all
// of these incompatibilities that are completely unnecessary.

#define stupid_status fix.status
#else
#define stupid_status status
#endif


	  if (s_debug >= 3) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("gpsdata: status=%d, mode=%d, lat=%.6f, lon=%.6f, track=%.1f, speed=%.1f, alt=%.0f\n",
	       gpsdata.stupid_status, gpsdata.fix.mode,
	       gpsdata.fix.latitude, gpsdata.fix.longitude,
	       gpsdata.fix.track, gpsdata.fix.speed, gpsdata.fix.stupid_altitude);
	  }

	  // Inform user about change in fix status.

	  switch (gpsdata.fix.mode) {
	    default:
	    case MODE_NOT_SEEN:
	    case MODE_NO_FIX:
	      if (info.fix <= DWFIX_NOT_SEEN) {
		text_color_set(DW_COLOR_INFO);
	        dw_printf ("GPSD: No location fix.\n");
	      }
	      if (info.fix >= DWFIX_2D) {
		text_color_set(DW_COLOR_INFO);
	        dw_printf ("GPSD: Lost location fix.\n");
	      }
	      info.fix = DWFIX_NO_FIX;
	      break;

	    case MODE_2D:
	      if (info.fix != DWFIX_2D) {
		text_color_set(DW_COLOR_INFO);
	        dw_printf ("GPSD: Location fix is now 2D.\n");
	      }
	      info.fix = DWFIX_2D;
	      break;

	    case MODE_3D:
	      if (info.fix != DWFIX_3D) {
		text_color_set(DW_COLOR_INFO);
	        dw_printf ("GPSD: Location fix is now 3D.\n");
	      }
	      info.fix = DWFIX_3D;
	      break;
	  }


// Oct. 2020 - 'status' is always zero for latest version of libgps so we can't use that anymore.

	  if (/*gpsdata.stupid_status >= STATUS_FIX &&*/ gpsdata.fix.mode >= MODE_2D) {

#define GOOD(x) (isfinite(x) && ! isnan(x))

	    info.dlat = GOOD(gpsdata.fix.latitude) ? gpsdata.fix.latitude : G_UNKNOWN;
	    info.dlon = GOOD(gpsdata.fix.longitude) ? gpsdata.fix.longitude : G_UNKNOWN;
	    // When stationary, track is NaN which is not finite.
	    info.track = GOOD(gpsdata.fix.track) ? gpsdata.fix.track : G_UNKNOWN;
	    info.speed_knots = GOOD(gpsdata.fix.speed) ? (MPS_TO_KNOTS * gpsdata.fix.speed) : G_UNKNOWN;
	    if (gpsdata.fix.mode >= MODE_3D) {
	      info.altitude = GOOD(gpsdata.fix.stupid_altitude) ? gpsdata.fix.stupid_altitude : G_UNKNOWN;
	    }
	    // Otherwise keep last known altitude when we downgrade from 3D to 2D fix.
	    // Caller knows altitude is outdated if info.fix == DWFIX_2D.
	  }
	  // Otherwise keep the last known location which is better than totally lost.
	  // Caller knows location is outdated if info.fix == DWFIX_NO_FIX.


	  info.timestamp = time(NULL);
	  if (s_debug >= 2) {
	    text_color_set(DW_COLOR_DEBUG);
	    dwgps_print ("GPSD: ", &info);
	  }
	  dwgps_set_data (&info);
	}

	return(0);	// Terminate thread on serious error.

} /* end read_gpsd_thread */

#endif


/*-------------------------------------------------------------------
 *
 * Name:        dwgpsd_term
 *
 * Purpose:    	Shut down GPS interface before exiting from application.
 *
 * Inputs:	none.
 *
 * Returns:	none.
 *
 *--------------------------------------------------------------------*/


void dwgpsd_term (void) {

#if ENABLE_GPSD

	gps_stream (&gpsdata, WATCH_DISABLE, NULL);
	gps_close (&gpsdata);

#endif

} /* end dwgpsd_term */




/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:    	Simple unit test for other functions in this file.
 *
 * Description: Compile with -DGPSTEST option.
 *
 *		 gcc  -DGPSTEST -DENABLE_GPSD dwgpsd.c dwgps.c textcolor.o  latlong.o misc.a -lm -lpthread -lgps
 *		./a.out
 *
 *--------------------------------------------------------------------*/

#if GPSTEST


int dwgpsnmea_init (struct misc_config_s *pconfig, int debug)
{
	return (0);
}
void dwgpsnmea_term (void)
{
	return;
}


int main (int argc, char *argv[])
{
	struct misc_config_s config;
	dwgps_info_t info;


	memset (&config, 0, sizeof(config));
	strlcpy (config.gpsd_host, "localhost", sizeof(config.gpsd_host));
	config.gpsd_port = atoi(DEFAULT_GPSD_PORT);

	dwgps_init (&config, 3);

	while (1) {
	  dwfix_t fix;

	  fix = dwgps_read (&info);
	  text_color_set (DW_COLOR_INFO);
	  switch (fix) {
	    case DWFIX_2D:
	    case DWFIX_3D:
	      dw_printf ("%.6f  %.6f", info.dlat, info.dlon);
	      dw_printf ("  %.1f knots  %.0f degrees", info.speed_knots, info.track);
	      if (fix==3) dw_printf ("  altitude = %.1f meters", info.altitude);
	      dw_printf ("\n");
	      break;
	    case DWFIX_NOT_SEEN:
	    case DWFIX_NO_FIX:
	      dw_printf ("Location currently not available.\n");
	      break;
	    case DWFIX_NOT_INIT:
	      dw_printf ("GPS Init failed.\n");
	      exit (1);
	    case DWFIX_ERROR:
	    default:
	      dw_printf ("ERROR getting GPS information.\n");
	      break;
	  }
	  SLEEP_SEC (3);
	}

} /* end main */


#endif



/* end dwgpsd.c */



