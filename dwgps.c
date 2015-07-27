//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013  John Langner, WB2OSZ
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
 * Description:	Tracker beacons need to know the current location.
 *		At this time, I can't think of any other reason why
 *		we would need this information.
 *
 *		For Linux, we use gpsd and libgps.
 *		This has the extra benefit that the system clock can
 *		be set from the GPS signal.
 *
 *		Not yet implemented for Windows.  Not sure how yet.
 *		The Windows location API is new in Windows 7.
 *		At the end of 2013, about 1/3 of Windows users are
 *		still using XP so that still needs to be supported.	
 *
 * Reference:	
 *
 *---------------------------------------------------------------*/

#if TEST
#define ENABLE_GPS 1
#endif


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

#if __WIN32__
#include <windows.h>
#else
#if ENABLE_GPS
#include <gps.h>

#if GPSD_API_MAJOR_VERSION != 5
#error libgps API version might be incompatible.
#endif

#endif
#endif

#include "direwolf.h"
#include "textcolor.h"
#include "dwgps.h"


/* Was init successful? */

static enum { INIT_NOT_YET, INIT_SUCCESS, INIT_FAILED } init_status = INIT_NOT_YET;

#if __WIN32__
#include <windows.h>
#else
#if ENABLE_GPS

static struct gps_data_t gpsdata;

#endif
#endif


/*-------------------------------------------------------------------
 *
 * Name:        dwgps_init
 *
 * Purpose:    	Intialize the GPS interface.
 *
 * Inputs:	none.
 *		
 * Returns:	0 = success
 *		-1 = failure
 *
 * Description:	For Linux, this maps into gps_open.
 *		Not yet implemented for Windows.
 *
 *--------------------------------------------------------------------*/

int dwgps_init (void)
{

#if __WIN32__

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("GPS interface not yet available in Windows version.\n");
	init_status = INIT_FAILED;
	return (-1);

#elif ENABLE_GPS

	int err;

	err = gps_open (GPSD_SHARED_MEMORY, NULL, &gpsdata);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unable to connect to GPS receiver.\n");
	  if (err == NL_NOHOST) {
	    dw_printf ("Shared memory interface is not enabled in libgps.\n");
	    dw_printf ("Download the gpsd source and build with 'shm_export=True' option.\n");
	  }
	  else {
	    dw_printf ("%s\n", gps_errstr(errno));
	  }
	  init_status = INIT_FAILED;
	  return (-1);
	}
	init_status = INIT_SUCCESS;
	return (0);
#else

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("GPS interface not enabled in this version.\n");
	dw_printf ("See documentation on how to rebuild with ENABLE_GPS.\n");
	init_status = INIT_FAILED;
	return (-1);

#endif

}  /* end dwgps_init */



/*-------------------------------------------------------------------
 *
 * Name:        dwgps_read
 *
 * Purpose:    	Obtain current location from GPS receiver.
 *
 * Outputs:	*plat		- Latitude.
 *		*plon		- Longitude.
 *		*pspeed		- Speed, knots.
 *		*pcourse	- Course over ground, degrees.
 *		*palt		- Altitude, meters.
 *
 * Returns:	-1 = error
 *		0 = data not available (no fix)
 *		2 = 2D fix, lat/lon, speed, and course are set.
 *		3 - 3D fix, altitude is also set.
 *
 *--------------------------------------------------------------------*/

int dwgps_read (double *plat, double *plon, float *pspeed, float *pcourse, float *palt)
{
#if __WIN32__

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Internal error, dwgps_read, shouldn't be here.\n");
	return (-1);

#elif ENABLE_GPS

	int err;

	if (init_status != INIT_SUCCESS) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error, dwgps_read without successful init.\n");
	  return (-1);
	}

	err = gps_read (&gpsdata);

#if DEBUG
	dw_printf ("gps_read returns %d bytes\n", err);
#endif
	if (err > 0) {
	  if (gpsdata.status >= STATUS_FIX && gpsdata.fix.mode >= MODE_2D) {

	     *plat = gpsdata.fix.latitude;
	     *plon = gpsdata.fix.longitude;
	     *pcourse = gpsdata.fix.track;		
	     *pspeed = MPS_TO_KNOTS * gpsdata.fix.speed; /* libgps uses meters/sec */

	     if (gpsdata.fix.mode >= MODE_3D) {
	       *palt = gpsdata.fix.altitude;
	       return (3);
	     }
	     return (2);
	   }

	   /* No fix.  Probably temporary condition. */
	   return (0);
	}
	else if (err == 0) {
	   /* No data available */
	   return (0);
	}
	else {
	  /* More serious error. */
	  return (-1);
	}
#else 

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Internal error, dwgps_read, shouldn't be here.\n");
	return (-1);
#endif

} /* end dwgps_read */


/*-------------------------------------------------------------------
 *
 * Name:        dwgps_term
 *
 * Purpose:    	Shut down GPS interface before exiting from application.
 *
 * Inputs:	none.
 *
 * Returns:	none.
 *
 *--------------------------------------------------------------------*/

void dwgps_term (void) {

#if __WIN32__
	
#elif ENABLE_GPS

	if (init_status == INIT_SUCCESS) {
	  gps_close (&gpsdata);
	}
#else 

#endif

} /* end dwgps_term */




/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:    	Simple unit test for other functions in this file.
 *
 * Description: Compile with -DTEST option.
 *
 *			gcc -DTEST dwgps.c textcolor.c -lgps
 *
 *--------------------------------------------------------------------*/

#if TEST

int main (int argc, char *argv[])
{

#if __WIN32__

	printf ("Not in win32 version yet.\n");

#elif ENABLE_GPS
	int err;
	int fix;
	double lat;
	double lon;
	float speed;
	float course;
	float alt;

	err = dwgps_init ();

	if (err != 0) exit(1);

	while (1) {
	  fix = dwgps_read (&lat, &lon, &speed, &course, &alt);
	  switch (fix) {
	    case 3:
	    case 2:
	      dw_printf ("%.6f  %.6f", lat, lon);
	      dw_printf ("  %.1f knots  %.0f degrees", speed, course);
	      if (fix==3) dw_printf ("  altitude = %.1f meters", alt);
	      dw_printf ("\n");
	      break;
	    case 0:
	      dw_printf ("location currently not available.\n");
	      break;
	    default:
	      dw_printf ("ERROR getting GPS information.\n");
	  }
	  sleep (3);
	}


#else 

	printf ("Test: Shouldn't be here.\n");
#endif

} /* end main */


#endif



/* end dwgps.c */



