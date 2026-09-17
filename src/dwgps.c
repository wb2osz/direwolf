//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2015  John Langner, WB2OSZ
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
 * Purpose:   	Interface for obtaining location from GPS.
 *		
 * Description:	This is a wrapper for two different implementations:
 *
 *		(1) Read NMEA sentences from a serial port (or USB
 *		    that looks line one).  Available for all platforms.
 *
 *		(2) Read from gpsd.  Not available for Windows.
 *		    Including this is optional because it depends
 *		    on another external software component.
 *
 *
 * API:		dwgps_init	Connect to data stream at start up time.
 *
 *		dwgps_read	Return most recent location to application.
 *
 *		dwgps_print	Print contents of structure for debugging.
 *
 *		dwgps_term	Shutdown on exit.
 *
 *
 * from below:	dwgps_set_data	Called from other two implementations to
 *				save data until it is needed.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "textcolor.h"
#include "dwgps.h"
#include "dwgpsnmea.h"
#include "dwgpsd.h"


static int s_dwgps_debug = 0;		/* Enable debug output. */
					/* >= 2 show updates from GPS. */
					/* >= 1 show results from dwgps_read. */

/*
 * The GPS reader threads deposit current data here when it becomes available.
 * dwgps_read returns it to the requesting application.
 *
 * A critical region to avoid inconsistency between fields.
 */

static dwgps_info_t s_dwgps_info = {
	.timestamp = 0,
	.fix = DWFIX_NOT_INIT,			/* to detect read without init. */
	.dlat = G_UNKNOWN,
	.dlon = G_UNKNOWN,
	.speed_knots = G_UNKNOWN,
	.track = G_UNKNOWN,
	.altitude = G_UNKNOWN
};

static dw_mutex_t s_gps_mutex;

static void dwgps_term (void);


/*-------------------------------------------------------------------
 *
 * Name:        dwgps_init
 *
 * Purpose:    	Initialize the GPS interface.
 *
 * Inputs:	pconfig		Configuration settings.  This might include
 *				serial port name for direct connect and host
 *				name or address for network connection.
 *
 *		debug	- If >= 1, print results when dwgps_read is called.
 *				(In this file.)
 *
 *			  If >= 2, location updates are also printed.
 *				(In other two related files.)
 *
 * Returns:	none
 *
 * Description:	Call corresponding functions for implementations.
 * 		Normally we would expect someone to use either GPSNMEA or
 *		GPSD but there is nothing to prevent use of both at the
 *		same time.
 *
 *--------------------------------------------------------------------*/


void dwgps_init (struct misc_config_s *pconfig, int debug)
{

	s_dwgps_debug = debug;

	dw_mutex_init (&s_gps_mutex);

	dwgpsnmea_init (pconfig, debug);

#if ENABLE_GPSD

	dwgpsd_init (pconfig, debug);

#endif

	SLEEP_MS(500);		/* So receive thread(s) can clear the */
				/* not init status before it gets checked. */

// Clean shutdown at exit.

	atexit (dwgps_term);

} /* end dwgps_init */


/*-------------------------------------------------------------------
 *
 * Name:        dwgps_clear
 *
 * Purpose:    	Clear the gps info structure.
 *
 *--------------------------------------------------------------------*/

void dwgps_clear (dwgps_info_t *gpsinfo)
{
	gpsinfo->timestamp = 0;
	gpsinfo->fix = DWFIX_NOT_SEEN;
	gpsinfo->dlat = G_UNKNOWN;
	gpsinfo->dlon = G_UNKNOWN;
	gpsinfo->speed_knots = G_UNKNOWN;
	gpsinfo->track = G_UNKNOWN;
	gpsinfo->altitude = G_UNKNOWN;
}


/*-------------------------------------------------------------------
 *
 * Name:        dwgps_read
 *
 * Purpose:     Return most recent location data available.
 *
 * Outputs:	gpsinfo		- Structure with latitude, longitude, etc.
 *
 * Returns:	Position fix quality.  Same as in structure.
 *
 *
 *--------------------------------------------------------------------*/

dwfix_t dwgps_read (dwgps_info_t *gpsinfo)
{

	dw_mutex_lock (&s_gps_mutex);

	memcpy (gpsinfo, &s_dwgps_info, sizeof(*gpsinfo));

	dw_mutex_unlock (&s_gps_mutex);

	if (s_dwgps_debug >= 1) {
	  text_color_set (DW_COLOR_DEBUG);
	  dwgps_print ("gps_read: ", gpsinfo);
	}

	// TODO: Should we check timestamp and complain if very stale?
	// or should we leave that up to the caller?

	return (s_dwgps_info.fix);
} 


/*-------------------------------------------------------------------
 *
 * Name:        dwgps_print
 *
 * Purpose:     Print gps information for debugging.
 *
 * Inputs:	msg		- Message for prefix on line.
 *		gpsinfo		- Structure with latitude, longitude, etc.
 *
 * Description:	Caller is responsible for setting text color.
 *
 *--------------------------------------------------------------------*/

void dwgps_print (char *msg, dwgps_info_t *gpsinfo)
{

	dw_printf ("%stime=%d fix=%d lat=%.6f lon=%.6f trk=%.0f spd=%.1f alt=%.0f\n",
			msg,
			(int)gpsinfo->timestamp, (int)gpsinfo->fix,
			gpsinfo->dlat, gpsinfo->dlon,
			gpsinfo->track, gpsinfo->speed_knots,
			gpsinfo->altitude);

}  /* end dwgps_set_data */


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

static void dwgps_term (void) {

	dwgpsnmea_term ();

#if ENABLE_GPSD
	dwgpsd_term ();
#endif

} /* end dwgps_term */




/*-------------------------------------------------------------------
 *
 * Name:        dwgps_set_data
 *
 * Purpose:     Called by the GPS interfaces when new data is available.
 *
 * Inputs:	gpsinfo		- Structure with latitude, longitude, etc.
 *
 *--------------------------------------------------------------------*/

void dwgps_set_data (dwgps_info_t *gpsinfo)
{

	/* Debug print is handled by the two callers so */
	/* we can distinguish the source. */

	dw_mutex_lock (&s_gps_mutex);

	memcpy (&s_dwgps_info, gpsinfo, sizeof(s_dwgps_info));

	dw_mutex_unlock (&s_gps_mutex);

}  /* end dwgps_set_data */


/* end dwgps.c */



