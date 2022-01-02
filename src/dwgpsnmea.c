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
 * Module:      dwgpsnmea.c
 *
 * Purpose:   	process NMEA sentences from a GPS receiver.
 *		
 * Description:	This version is available for all operating systems.
 *
 *
 * TODO:	GPS is no longer the only game in town.
 *		"GNSS" is often seen as a more general term to include
 *		other similar systems.  Some receivers will receive
 *		multiple types at the same time and combine them
 *		for greater accuracy and reliability.
 *
 *		We can now see NMEA sentences with other "Talker IDs."
 *
 *			$GPxxx = GPS
 *			$GLxxx = GLONASS
 *			$GAxxx = Galileo
 *			$GBxxx = BeiDou
 *			$GNxxx = Any combination
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
#include <stddef.h>


#include "textcolor.h"
#include "dwgps.h"
#include "dwgpsnmea.h"
#include "serial_port.h"


static int s_debug = 0;		/* Enable debug output. */
				/* See dwgpsnmea_init description for values. */

static struct misc_config_s *s_save_configp;					



#if __WIN32__
static unsigned __stdcall read_gpsnmea_thread (void *arg);
#else
static void * read_gpsnmea_thread (void *arg);
#endif

/*-------------------------------------------------------------------
 *
 * Name:        dwgpsnmea_init
 *
 * Purpose:    	Open serial port for the GPS receiver.
 *
 * Inputs:	pconfig		Configuration settings.  This includes
 *				serial port name for direct connect.
 *
 *		debug	- If >= 1, print results when dwgps_read is called.
 *				(In different file.)
 *
 *			  If >= 2, location updates are also printed.
 *				(In this file.)
 *				Why not do it in dwgps_set_data() ?
 *				Here, we can prefix it with GPSNMEA to
 *				distinguish it from GPSD.
 *
 *			  If >= 3, Also the NMEA sentences.
 *				(In this file.)
 *		
 * Returns:	1 = success
 *		0 = nothing to do  (no serial port specified in config)
 *		-1 = failure
 *
 * Description:	When talking directly to GPS receiver  (any operating system):
 *
 *			- Open the appropriate serial port.
 *			- Start up thread to process incoming data.
 *			  It reads from the serial port and deposits into
 *			  dwgps_info, above.
 *
 * 		The application calls dwgps_read to get the most recent information.			
 *
 *--------------------------------------------------------------------*/

/* Make this static and available to all functions so term function can access it. */

static MYFDTYPE s_gpsnmea_port_fd = MYFDERROR;   /* Handle for serial port. */


int dwgpsnmea_init (struct misc_config_s *pconfig, int debug)
{
	//dwgps_info_t info;
#if __WIN32__
	HANDLE read_gps_th;
#else
	pthread_t read_gps_tid;
	//int e;
#endif

	s_debug = debug;
	s_save_configp = pconfig;


	if (s_debug >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dwgpsnmea_init()\n");
	}

	if (strlen(pconfig->gpsnmea_port) == 0) {

	  /* Nothing to do.  Leave initial fix value for not init. */
	  return (0);
	}

/*
 * Open serial port connection.
 */

	s_gpsnmea_port_fd = serial_port_open (pconfig->gpsnmea_port, pconfig->gpsnmea_speed);

	if (s_gpsnmea_port_fd != MYFDERROR) {
#if __WIN32__
	  read_gps_th = (HANDLE)_beginthreadex (NULL, 0, read_gpsnmea_thread, (void*)(ptrdiff_t)s_gpsnmea_port_fd, 0, NULL);
	  if (read_gps_th == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Could not create GPS NMEA listening thread.\n");
	    return (-1);
	  }
#else
	  int e;
	  e = pthread_create (&read_gps_tid, NULL, read_gpsnmea_thread, (void*)(ptrdiff_t)s_gpsnmea_port_fd);
	  if (e != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    perror("Could not create GPS NMEA listening thread.");
	    return (-1);
	  }
#endif
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not open serial port %s for GPS receiver.\n", pconfig->gpsnmea_port);
	  return (-1);
	}

/* success */

	return (1);

}  /* end dwgpsnmea_init */


/* Return fd to share if waypoint wants same device. */

MYFDTYPE dwgpsnmea_get_fd(char *wp_port_name, int speed)
{
	if (strcmp(s_save_configp->gpsnmea_port, wp_port_name) == 0 && speed == s_save_configp->gpsnmea_speed) {
	  return (s_gpsnmea_port_fd);
	}
	return (MYFDERROR);
}


/*-------------------------------------------------------------------
 *
 * Name:        read_gpsnmea_thread
 *
 * Purpose:     Read information from GPS, as it becomes available, and
 *		store it for later retrieval by dwgps_read.
 *
 * Inputs:	fd	- File descriptor for serial port.
 *
 * Description:	This version reads from serial port and parses the 
 *		NMEA sentences.
 *
 *--------------------------------------------------------------------*/

#define TIMEOUT 5


#if __WIN32__
static unsigned __stdcall read_gpsnmea_thread (void *arg)
#else
static void * read_gpsnmea_thread (void *arg)
#endif
{
	MYFDTYPE fd = (MYFDTYPE)(ptrdiff_t)arg;

// Maximum length of message from GPS receiver is 82 according to some people.  
// Make buffer considerably larger to be safe.

#define NMEA_MAX_LEN 160	

	char gps_msg[NMEA_MAX_LEN];
	int gps_msg_len = 0;
	dwgps_info_t info;


	if (s_debug >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("read_gpsnmea_thread (%d)\n", (int)(ptrdiff_t)arg);
	}

	dwgps_clear (&info);
	info.fix = DWFIX_NOT_SEEN;	/* clear not init state. */
	if (s_debug >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dwgps_print ("GPSNMEA: ", &info);
	}
	dwgps_set_data (&info);


	while (1) {
	  int ch;

	  ch = serial_port_get1(fd);

	  if (ch < 0) {

	    /* This might happen if a USB  device is unplugged. */
	    /* I can't imagine anything that would cause it with */
	    /* a normal serial port. */

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("----------------------------------------------\n");
	    dw_printf ("GPSNMEA: Lost communication with GPS receiver.\n");
	    dw_printf ("----------------------------------------------\n");

	    info.fix = DWFIX_ERROR;
	    if (s_debug >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      dwgps_print ("GPSNMEA: ", &info);
	    }
	    dwgps_set_data (&info);

	    serial_port_close(s_gpsnmea_port_fd);
	    s_gpsnmea_port_fd = MYFDERROR;

	    // TODO: If the open() was in this thread, we could wait a while and
	    // try to open again.  That would allow recovery if the USB GPS device
	    // is unplugged and plugged in again.
	    break;	/* terminate thread. */
	  }

	  if (ch == '$') {
	    // Start of new sentence.
	    gps_msg_len = 0;
	    gps_msg[gps_msg_len++] = ch;
	    gps_msg[gps_msg_len] = '\0';
	  }
	  else if (ch == '\r' || ch == '\n') {
	    if (gps_msg_len >= 6 && gps_msg[0] == '$') {

	      dwfix_t f;

	      if (s_debug >= 3) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("%s\n", gps_msg);
	      }

/* Process sentence. */
// TODO: More general: Ignore the second letter rather than recognizing only GP... and GN...

	      if (strncmp(gps_msg, "$GPRMC", 6) == 0 ||
		  strncmp(gps_msg, "$GNRMC", 6) == 0) {

	        // Here we just tuck away the course and speed.
	        // Fix and location will be updated by GxGGA.

	        double ignore_dlat;
	        double ignore_dlon;

		f = dwgpsnmea_gprmc (gps_msg, 0, &ignore_dlat, &ignore_dlon, &info.speed_knots, &info.track);

	        if (f == DWFIX_ERROR) {
		    /* Parse error.  Shouldn't happen.  Better luck next time. */
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("GPSNMEA: Error parsing $GPRMC sentence.\n");
	            dw_printf ("%s\n", gps_msg);
	        }
	      }

	      else if (strncmp(gps_msg, "$GPGGA", 6) == 0 ||
		       strncmp(gps_msg, "$GNGGA", 6) == 0) {
		int nsat;

		f = dwgpsnmea_gpgga (gps_msg, 0, &info.dlat, &info.dlon, &info.altitude, &nsat);

	        if (f == DWFIX_ERROR) {
		    /* Parse error.  Shouldn't happen.  Better luck next time. */
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("GPSNMEA: Error parsing $GPGGA sentence.\n");
	            dw_printf ("%s\n", gps_msg);
	        }
	        else  {
	            if (f != info.fix) {		// Print change in location fix.
		       text_color_set(DW_COLOR_INFO);
	               if (f == DWFIX_NO_FIX) dw_printf ("GPSNMEA: Location fix has been lost.\n");
	               if (f == DWFIX_2D)     dw_printf ("GPSNMEA: Location fix is now 2D.\n");
	               if (f == DWFIX_3D)     dw_printf ("GPSNMEA: Location fix is now 3D.\n");
	               info.fix = f;
	          }
	          info.timestamp = time(NULL);
	          if (s_debug >= 2) {
	            text_color_set(DW_COLOR_DEBUG);
	            dwgps_print ("GPSNMEA: ", &info);
	          }
	          dwgps_set_data (&info);
	        }	
	      }
	    }

	    gps_msg_len = 0;
	    gps_msg[gps_msg_len] = '\0';
	  }
	  else {	
	    if (gps_msg_len < NMEA_MAX_LEN-1) {
	      gps_msg[gps_msg_len++] = ch;
	      gps_msg[gps_msg_len] = '\0';
	    }
	  }
	}	/* while (1) */

#if __WIN32__
	return (0);
#else
	return (NULL);	
#endif

} /* end read_gpsnmea_thread */




/*-------------------------------------------------------------------
 *
 * Name:	remove_checksum
 *
 * Purpose:	Validate checksum and remove before further processing.
 *
 * Inputs:	sentence
 *		quiet		suppress printing of error messages.
 *
 * Outputs:	sentence	modified in place.
 *
 * Returns:	0 = good checksum.
 *		-1 = error.  missing or wrong.
 *
 *--------------------------------------------------------------------*/


static int remove_checksum (char *sent, int quiet)
{
        char *p;
        unsigned char cs;


// Do we have valid checksum?

        cs = 0;
        for (p = sent+1; *p != '*' && *p != '\0'; p++) {
          cs ^= *p;
        }

        p = strchr (sent, '*');
        if (p == NULL) {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_INFO);
            dw_printf("Missing GPS checksum.\n");
	  }
          return (-1);
        }
        if (cs != strtoul(p+1, NULL, 16)) {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("GPS checksum error. Expected %02x but found %s.\n", cs, p+1);
	  }
          return (-1);
        }
        *p = '\0';      // Remove the checksum.
	return (0);
}


/*-------------------------------------------------------------------
 *
 * Name:        dwgpsnmea_gprmc
 *
 * Purpose:    	Parse $GPRMC sentence and extract interesting parts.
 *
 * Inputs:	sentence	NMEA sentence.
 *
 *		quiet		suppress printing of error messages.
 *
 * Outputs:	odlat		latitude
 *		odlon		longitude
 *		oknots		speed
 *		ocourse		direction of travel.
 *
 *					Left undefined if not valid.
 *
 * Note:	RMC does not contain altitude.
 *
 * Returns:	DWFIX_ERROR	Parse error.
 *		DWFIX_NO_FIX	GPS is there but Position unknown.  Could be temporary.
 *		DWFIX_2D	Valid position.   We don't know if it is really 2D or 3D.
 *
 * Examples:	$GPRMC,001431.00,V,,,,,,,121015,,,N*7C
 *		$GPRMC,212404.000,V,4237.1505,N,07120.8602,W,,,150614,,*0B
 *		$GPRMC,000029.020,V,,,,,,,080810,,,N*45
 *		$GPRMC,003413.710,A,4237.1240,N,07120.8333,W,5.07,291.42,160614,,,A*7F
 *
 *--------------------------------------------------------------------*/

dwfix_t dwgpsnmea_gprmc (char *sentence, int quiet, double *odlat, double *odlon, float *oknots, float *ocourse)
{
	char stemp[NMEA_MAX_LEN];	/* Make copy because parsing is destructive. */

	char *next;

	char *ptype;			/* Should be $GPRMC */
	char *ptime;			/* Time, hhmmss[.sss] */
	char *pstatus;			/* Status, A=Active (valid position), V=Void */
	char *plat;			/* Latitude */
	char *pns;			/* North/South */
	char *plon;			/* Longitude */
	char *pew;			/* East/West */
	char *pknots;			/* Speed over ground, knots. */
	char *pcourse;			/* True course, degrees. */
	char *pdate;			/* Date, ddmmyy */
					/* Magnetic variation */
					/* In version 3.00, mode is added: A D E N (see below) */
					/* Checksum */

	strlcpy (stemp, sentence, sizeof(stemp));

	if (remove_checksum (stemp, quiet) < 0) {
	  return (DWFIX_ERROR);
	}

	next = stemp;
	ptype = strsep(&next, ",");
	ptime = strsep(&next, ",");
	pstatus = strsep(&next, ",");	
	plat = strsep(&next, ",");
	pns = strsep(&next, ",");
	plon = strsep(&next, ",");
	pew = strsep(&next, ",");
	pknots = strsep(&next, ",");
	pcourse = strsep(&next, ",");
	pdate = strsep(&next, ",");	

	/* Suppress the 'set but not used' warnings. */
	/* Alternatively, we might use __attribute__((unused)) */

	(void)(ptype);
	(void)(ptime);
	(void)(pdate);

	if (pstatus != NULL && strlen(pstatus) == 1) {
	  if (*pstatus != 'A') {
	    return (DWFIX_NO_FIX);		/* Not "Active." Don't parse. */
	  }
	}
	else {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("No status in GPRMC sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}


	if (plat != NULL && strlen(plat) > 0 && pns != NULL && strlen(pns) > 0) {
	  *odlat = latitude_from_nmea(plat, pns);
	}
	else {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Can't get latitude from GPRMC sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}


	if (plon != NULL && strlen(plon) > 0 && pew != NULL && strlen(pew) > 0) {
	  *odlon = longitude_from_nmea(plon, pew);
	}
	else {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Can't get longitude from GPRMC sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}


	if (pknots != NULL && strlen(pknots) > 0) {
	  *oknots = atof(pknots);
	}
	else {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Can't get speed from GPRMC sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}


	if (pcourse != NULL) {
	  if (strlen(pcourse) > 0) {
	    *ocourse = atof(pcourse);
	  }
	  else {
	    /* When stationary, this field might be empty. */
	    *ocourse = G_UNKNOWN;
	  }
	}
	else {
	  if ( ! quiet) {

	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Can't get course from GPRMC sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}

	//text_color_set (DW_COLOR_INFO);
        //dw_printf("%.6f %.6f %.1f %.0f\n", *odlat, *odlon, *oknots, *ocourse);

	return (DWFIX_2D);

} /* end dwgpsnmea_gprmc */


/*-------------------------------------------------------------------
 *
 * Name:        dwgpsnmea_gpgga
 *
 * Purpose:    	Parse $GPGGA sentence and extract interesting parts.
 *
 * Inputs:	sentence	NMEA sentence.
 *
 *		quiet		suppress printing of error messages.
 *
 * Outputs:	odlat		latitude
 *		odlon		longitude
 *		oalt		altitude in meters
 *		onsat		number of satellites.
 *		
 *					Left undefined if not valid.
 *
 * Note:	GGA has altitude but not course and speed so we need to use both.
 *
 * Returns:	DWFIX_ERROR	Parse error.
 *		DWFIX_NO_FIX	GPS is there but Position unknown.  Could be temporary.
 *		DWFIX_2D	Valid position.   We don't know if it is really 2D or 3D.
 *				Take more cautious value so we don't try using altitude.
 *		DWFIX_3D	Valid 3D position.
 *
 * Examples:	$GPGGA,001429.00,,,,,0,00,99.99,,,,,,*68
 *		$GPGGA,212407.000,4237.1505,N,07120.8602,W,0,00,,,M,,M,,*58
 *		$GPGGA,000409.392,,,,,0,00,,,M,0.0,M,,0000*53
 *		$GPGGA,003518.710,4237.1250,N,07120.8327,W,1,03,5.9,33.5,M,-33.5,M,,0000*5B
 *
 *--------------------------------------------------------------------*/

dwfix_t dwgpsnmea_gpgga (char *sentence, int quiet, double *odlat, double *odlon, float *oalt, int *onsat)
{
	char stemp[NMEA_MAX_LEN];	/* Make copy because parsing is destructive. */

	char *next;

	char *ptype;			/* Should be $GPGGA */
	char *ptime;			/* Time, hhmmss[.sss] */
	char *plat;			/* Latitude */
	char *pns;			/* North/South */
	char *plon;			/* Longitude */
	char *pew;			/* East/West */
	char *pfix;			/* 0=invalid, 1=GPS fix, 2=DGPS fix */
	char *pnum_sat;			/* Number of satellites */
	char *phdop;			/* Horiz. Dilution fo Precision */
	char *paltitude;		/* Altitude, above mean sea level */
	char *palt_u;			/* Units for Altitude, typically M for meters. */
	char *pheight;			/* Height above ellipsoid */
	char *pheight_u;		/* Units for height, typically M for meters. */
	char *psince;			/* Time since last DGPS update. */
	char *pdsta;			/* DGPS reference station id. */


	strlcpy (stemp, sentence, sizeof(stemp));

	if (remove_checksum (stemp, quiet) < 0) {
	  return (DWFIX_ERROR);
	}

	next = stemp;
	ptype = strsep(&next, ",");
	ptime = strsep(&next, ",");
	plat = strsep(&next, ",");
	pns = strsep(&next, ",");
	plon = strsep(&next, ",");
	pew = strsep(&next, ",");
	pfix = strsep(&next, ",");	
	pnum_sat = strsep(&next, ",");
	phdop = strsep(&next, ",");
	paltitude = strsep(&next, ",");
	palt_u = strsep(&next, ",");
	pheight = strsep(&next, ",");
	pheight_u = strsep(&next, ",");
	psince = strsep(&next, ",");
	pdsta = strsep(&next, ",");

	/* Suppress the 'set but not used' warnings. */
	/* Alternatively, we might use __attribute__((unused)) */

	(void)(ptype);
	(void)(ptime);
	(void)(pnum_sat);
	(void)(phdop);
	(void)(palt_u);
	(void)(pheight);
	(void)(pheight_u);
	(void)(psince);
	(void)(pdsta);

	if (pfix != NULL && strlen(pfix) == 1) {
	  if (*pfix == '0') {
	    return (DWFIX_NO_FIX);		/* No Fix. Don't parse the rest. */
	  }
	}
	else {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("No fix in GPGGA sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}


	if (plat != NULL && strlen(plat) > 0 && pns != NULL && strlen(pns) > 0) {
	  *odlat = latitude_from_nmea(plat, pns);
	}
	else {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Can't get latitude from GPGGA sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}


	if (plon != NULL && strlen(plon) > 0 && pew != NULL && strlen(pew) > 0) {
	  *odlon = longitude_from_nmea(plon, pew);
	}
	else {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Can't get longitude from GPGGA sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}

	// TODO: num sat...  Why would we care?

/* 
 * We can distinguish between 2D & 3D fix by presence 
 * of altitude or an empty field.
 */

	if (paltitude != NULL) {

	  if (strlen(paltitude) > 0) {
	    *oalt = atof(paltitude);
	    return (DWFIX_3D);
	  }
	  else {
	    return (DWFIX_2D);
	  }
	}
	else {
	  if ( ! quiet) {
	    text_color_set (DW_COLOR_ERROR);
            dw_printf("Can't get altitude from GPGGA sentence.\n");
	  }
	  return (DWFIX_ERROR);
	}

} /* end dwgpsnmea_gpgga */



/*-------------------------------------------------------------------
 *
 * Name:        dwgpsnmea_term
 *
 * Purpose:    	Shut down GPS interface before exiting from application.
 *
 * Inputs:	none.
 *
 * Returns:	none.
 *
 *--------------------------------------------------------------------*/


void dwgpsnmea_term (void) {

	// Should probably kill reader thread before closing device to avoid
	// message about read error.

	// serial_port_close (s_gpsnmea_port_fd); 

} /* end dwgps_term */




/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:    	Simple unit test for other functions in this file.
 *
 * Description: Compile with -DGPSTEST option.
 *
 *		Windows:
 *			gcc  -DGPSTEST -Iregex dwgpsnmea.c dwgps.c textcolor.o serial_port.o latlong.o misc.a
 *			a.exe
 *
 *		Linux:
 *			 gcc -DGPSTEST dwgpsnmea.c dwgps.c textcolor.o serial_port.o latlong.o misc.a -lm -lpthread
 *			./a.out
 *
 *--------------------------------------------------------------------*/

#if GPSTEST

int main (int argc, char *argv[])
{

	struct misc_config_s config;
	dwgps_info_t info;


	memset (&config, 0, sizeof(config));
	strlcpy (config.gpsnmea_port, "COM22", sizeof(config.gpsnmea_port));

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



/* end dwgpsnmea.c */



