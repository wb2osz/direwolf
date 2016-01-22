//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015  John Langner, WB2OSZ
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
 * Module:      dwgps.c
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


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#if __WIN32__
#error Not for Windows
#endif

#if ENABLE_GPSD
#include <gps.h>

// Debian bug report:  direwolf (1.2-1) FTBFS with libgps22 as part of the gpsd transition (#803605):
// dwgps.c claims to only support GPSD_API_MAJOR_VERSION 5, but also builds successfully with
// GPSD_API_MAJOR_VERSION 6 provided by libgps22 when the attached patch is applied.
#if GPSD_API_MAJOR_VERSION < 5 || GPSD_API_MAJOR_VERSION > 6
#error libgps API version might be incompatible.
#endif

#endif


#include "direwolf.h"
#include "textcolor.h"
#include "dwgps.h"
#include "dwgpsd.h"



static int s_debug = 0;		/* Enable debug output. */
				/* >= 1 show results from dwgps_read. */
				/* >= 2 show updates from GPS. */

static void * read_gpsd_thread (void *arg);

/*
 * Information for interface to gpsd daemon. 
 */

static struct gps_data_t gpsdata;


/*-------------------------------------------------------------------
 *
 * Name:        dwgpsd_init
 *
 * Purpose:    	Intialize the GPS interface.
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
 8		recent information.			
 *
 *--------------------------------------------------------------------*/

/*
 * Historical notes:
 *
 * Originally, I wanted to use the shared memory interface to gpsd
 * because it is simpler and more efficient.  Just access it when we
 * actually need the data and we don't have a lot of extra unnecessary
 * busy work going on.
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
 *	I'm told that it might work in Raspian, Jessie version.
 *	Haven't tried it yet.
 */



int dwgpsd_init (struct misc_config_s *pconfig, int debug)
{

#if ENABLE_GPSD

	pthread_t read_gps_tid;
	int e;
	int err;
	int arg = 0;
	char sport[12];
	dwgps_info_t info;

	s_debug = debug;

	if (s_debug >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dwgpsd_init()\n");
	}

/* 
 * Socket interface to gpsd.
 */

	if (strlen(pconfig->gpsd_host) == 0) {

	  /* Nothing to do.  Leave initial fix value of errror. */
	  return (0);
	}	  

	snprintf (sport, sizeof(sport), "%d", pconfig->gpsd_port);
	err = gps_open (pconfig->gpsd_host, sport, &gpsdata);
	if (err != 0) {
	  dwgps_info_t info;

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unable to connect to GPSD stream at %s:%s.\n", pconfig->gpsd_host, sport);
	  dw_printf ("%s\n", gps_errstr(errno));

	  return (-1);
	}

	gps_stream(&gpsdata, WATCH_ENABLE | WATCH_JSON, NULL);

	e = pthread_create (&read_gps_tid, NULL, read_gpsd_thread, (void *)(long)arg);
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

#define TIMEOUT 30

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

          if ( ! gps_waiting(&gpsdata, TIMEOUT * 1000000)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("GPSD: Timeout waiting for GPS data.\n");
	    /* Fall thru to read which should get error and bail out. */
	  }

	  if (gps_read (&gpsdata) == -1) {
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

	  switch (gpsdata.fix.mode) {
	    default:
	    case MODE_NOT_SEEN:
	      if (info.fix >= DWFIX_2D) {
		text_color_set(DW_COLOR_INFO);
	        dw_printf ("GPSD: Lost location fix.\n");
	      }
	      info.fix = DWFIX_NOT_SEEN;
	      break;

	    case MODE_NO_FIX:
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

	    /* Data is available. */
	    // TODO:  what is gpsdata.status?


	  if (gpsdata.status >= STATUS_FIX && gpsdata.fix.mode >= MODE_2D) {

	    info.dlat = isnan(gpsdata.fix.latitude) ? G_UNKNOWN : gpsdata.fix.latitude;
	    info.dlon = isnan(gpsdata.fix.longitude) ? G_UNKNOWN : gpsdata.fix.longitude;
	    info.track = isnan(gpsdata.fix.track) ? G_UNKNOWN : gpsdata.fix.track;
	    info.speed_knots = isnan(gpsdata.fix.speed) ? G_UNKNOWN : (MPS_TO_KNOTS * gpsdata.fix.speed);

	    if (gpsdata.fix.mode >= MODE_3D) {
	      info.altitude = isnan(gpsdata.fix.altitude) ? G_UNKNOWN : gpsdata.fix.altitude;
	    }
	  }

	  info.timestamp = time(NULL);
	  if (s_debug >= 2) {
	    text_color_set(DW_COLOR_DEBUG);
	    dwgps_print ("GPSD: ", &info);
	  }
	  dwgps_set_data (&info);
	}

	return(0);	// Terminate thread on serious error.

} /* end read_gps_thread */

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



