//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014  John Langner, WB2OSZ
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


//#define DEBUG 1

/*------------------------------------------------------------------
 *
 * Module:      nmea.c
 *
 * Purpose:   	Receive NMEA sentences from a GPS receiver.
 *		Send NMEA waypoint sentences to GPS display or mapping application.
 *		
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>

#if __WIN32__
#include <stdlib.h>
#include <windows.h>
#else
#define __USE_XOPEN2KXSI 1
#define __USE_XOPEN 1
//#define __USE_POSIX 1
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#endif

#include <assert.h>
#include <string.h>

#if __WIN32__
char *strsep(char **stringp, const char *delim);
#endif

#include "direwolf.h"
#include "config.h"
#include "ax25_pad.h"
#include "textcolor.h"
//#include "xmit.h"
#include "latlong.h"
#include "nmea.h"
#include "grm_sym.h"		/* Garmin symbols */
#include "mgn_icon.h"		/* Magellan icons */
#include "serial_port.h"


// TODO: receive buffer... static kiss_frame_t kf;	/* Accumulated KISS frame and state of decoder. */

static MYFDTYPE nmea_port_fd = MYFDERROR;

static void nmea_send_sentence (char *sent);

#if __WIN32__
static unsigned __stdcall nmea_listen_thread (void *arg);
#else
static void * nmea_listen_thread (void *arg);
#endif

static void nmea_parse_gps (char *sentence);


static int nmea_debug = 0;		/* Print information flowing from and to attached device. */

void nmea_set_debug (int n) 
{	
	nmea_debug = n;
}


/*-------------------------------------------------------------------
 *
 * Name:	nmea_init
 *
 * Purpose:	Initialization for NMEA communication port.
 *
 * Inputs:	mc->nmea_port	- name of serial port.
 *
 * Global output: nmea_port_fd
 *	
 *
 * Description:	(1) Open serial port device.
 *		(2) Start a new thread to listen for GPS receiver.
 *
 *---------------------------------------------------------------*/


void nmea_init (struct misc_config_s *mc)
{
	
#if __WIN32__
	HANDLE nmea_listen_th;
#else
	pthread_t nmea_listen_tid;
#endif

/*
 * Open serial port connection.
 * 4800 baud is standard for GPS.
 * Should add an option to allow changing someday.
 */
	if (strlen(mc->nmea_port) > 0) {

	  nmea_port_fd = serial_port_open (mc->nmea_port, 4800);

	  if (nmea_port_fd != MYFDERROR) {
#if __WIN32__
	    nmea_listen_th = (HANDLE)_beginthreadex (NULL, 0, nmea_listen_thread, (void*)(long)nmea_port_fd, 0, NULL);
	    if (nmea_listen_th == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not create NMEA listening thread.\n");
	      return;
	    }
#else
	    int e;
	    e = pthread_create (&nmea_listen_tid, NULL, nmea_listen_thread, (void*)(long)nmea_port_fd);
	    if (e != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      perror("Could not create NMEA listening thread.");
	    
	    }
#endif
	  }
	}


#if DEBUG
	text_color_set (DW_COLOR_DEBUG);

	dw_printf ("end of nmea_init: nmea_port_fd = %d\n", nmea_port_fd);
#endif
}



/*-------------------------------------------------------------------
 *
 * Name:        append_checksum
 *
 * Purpose:     Append checksum to the sentence.
 *		
 * In/out:	sentence	- NMEA sentence beginning with '$'.
 *
 * Description:	Checksum is exclusive of characters except leading '$'.
 *		We append '*' and an upper case two hexadecimal value.
 *
 *--------------------------------------------------------------------*/

static void append_checksum (char *sentence)
{
	char *p;
	int cs;

	assert (sentence[0] == '$');

	cs = 0;
	for (p = sentence+1; *p != '\0'; p++) {
	  cs ^= *p;
	}

	sprintf (p, "*%02X", cs & 0xff);

// Add crlf too?

} /* end append_checksum */



/*-------------------------------------------------------------------
 *
 * Name:        nema_send_waypoint
 *
 * Purpose:     Convert APRS position or object into NMEA waypoint sentence
 *		for use by a GPS display or other mapping application.
 *		
 * Inputs:	wname_in	- Name of waypoint.
 *		dlat		- Latitude.
 *		dlong		- Longitude.
 *		symtab		- Symbol table or overlay character.
 *		symbol		- Symbol code.
 *		alt		- Altitude in meters or G_UNKOWN.
 *		course		- Course in degrees or ??? for unknown.
 *		speed		- Speed in knots ?? or ??
 *		comment		- Description or message.
 *		
 *
 * Description:	Currently we send multiple styles.  Maybe someday there might
 * 		be an option to send a selected subset.
 *
 *			$GPWPL		- Generic with only location and name.
 *			$PGRMW		- Garmin, adds altitude, symbol, and comment
 *					  to previously named waypoint.
 *			$PMGNWPL	- Magellan, more complete for stationary objects.
 *			$PKWDWPL	- Kenwood with APRS style symbol but missing comment.
 * 
 *--------------------------------------------------------------------*/


void nmea_send_waypoint (char *wname_in, double dlat, double dlong, char symtab, char symbol, 
			float alt, float course, float speed, char *comment)
{
	char wname[12];		/* Waypoint name.  Any , or * removed. */
	char slat[12];		/* DDMM.mmmm */
	char slat_ns[2];	/* N or S */
	char slong[12];		/* DDDMM.mmmm */
	char slong_ew[2];	/* E or W */	
	char sentence[500];
	char salt[12];		/* altitude as string, empty if unknown */
	char sspeed[12];	/* speed as string, empty if unknown */
	char scourse[12];	/* course as string, empty if unknown */
	int grm_sym;		/* Garmin symbol code. */
	char sicon[4];		/* Magellan icon string */
	char stime[8];
	char sdate[8];
	char *p;



// Remove any comma from name.

// TODO: remove any , or * from comment.

	strcpy (wname, wname_in);
	for (p=wname; *p != '\0'; p++) {
	  if (*p == ',') *p = ' ';
	  if (*p == '*') *p = ' ';
	}

// Convert position to character form.

	latitude_to_nmea (dlat, slat, slat_ns);
	longitude_to_nmea (dlong, slong, slong_ew);

/*
 *	Generic.
 *
 *	$GPWPL,ddmm.mmmm,ns,dddmm.mmmm,ew,wname*99
 *
 *	Where,
 *	 	ddmm.mmmm,ns	is latitude
 *		dddmm.mmmm,ew	is longitude
 *		wname		is the waypoint name
 *		*99		is checksum
 */

	sprintf (sentence, "$GPWPL,%s,%s,%s,%s,%s", slat, slat_ns, slong, slong_ew, wname);
	append_checksum (sentence);
	nmea_send_sentence (sentence);



/*
 *	Garmin - https://www8.garmin.com/support/pdf/NMEA_0183.pdf
 *		 http://gpsinformation.net/mag-proto.htm
 *
 *	$PGRMW,wname,alt,symbol,comment*99
 *
 *	Where,
 *
 *		wname		is waypoint name.  Must match existing waypoint.
 *		alt		is altitude in meters.
 *		symbol		is symbol code.  Hexadecimal up to FFFF.
 *				    See Garmin Device Interface Specification 001-0063-00.
 *		comment		is comment for the waypoint.  
 *		*99		is checksum
 */

	if (alt == G_UNKNOWN) {
	  strcpy (salt, "");
	} 
	else {
	  sprintf (salt, "%.1f", alt);
	}
	grm_sym = 0x1234; 	// TODO

	sprintf (sentence, "$PGRMW,%s,%s,%04X,%s", wname, salt, grm_sym, comment);
	append_checksum (sentence);
	nmea_send_sentence (sentence);

/*
 *	Magellan - http://www.gpsinformation.org/mag-proto-2-11.pdf
 *
 *	$PMGNWPL,ddmm.mmmm,ns,dddmm.mmmm,ew,alt,unit,wname,comment,icon,xx*99
 *
 *	Where,
 *	 	ddmm.mmmm,ns	is latitude
 *		dddmm.mmmm,ew	is longitude
 *		alt		is altitude
 *		unit		is M for meters or F for feet
 *		wname		is the waypoint name
 *		comment		is message or comment
 *		icon		is one or two letters for icon code
 *		xx		is waypoint type which is optional, not well 
 *					defined, and not used in their example.					
 *		*99		is checksum
 */

// TODO: icon

	sprintf (sicon, "??");
	sprintf (sentence, "$PMGNWPL,%s,%s,%s,%s,%s,M,%s,%s,%s",
			slat, slat_ns, slong, slong_ew, salt, wname, comment, sicon);
	append_checksum (sentence);
	nmea_send_sentence (sentence);

/*
 *	Kenwood - Speculation due to no official spec found so far.
 *
 *	$PKWDWPL,hhmmss,v,ddmm.mmmm,ns,dddmm.mmmm,ew,speed,course,ddmmyy,alt,wname,ts*99
 *
 *	Where,
 *		hhmmss		is time in UTC.  Should we supply current
 *					time or only pass along time from
 *					received signal?
 *		v		indicates valid data ?????????????????
 *					Why would we send if not valid?
 *	 	ddmm.mmmm,ns	is latitude
 *		dddmm.mmmm,ew	is longitude
 *		speed		is speed in UNITS ???  knots ?????
 *		course		is course in degrees
 *		ddmmyy		is date.  Same question as time.
 *		alt		is altitude.  in UNITS ??? meters ???
 *		wname		is the waypoint name
 *		ts		are the table and symbol.
 *					Non-standard parsing would be required
 *					to deal with these for symbol:
 *						, Boy Scouts / Girl Scouts
 *						* SnowMobile / Snow				
 *		*99		is checksum
 *
 *	Oddly, there is not place for comment.
 */
 
	if (speed == G_UNKNOWN) {
	  strcpy (sspeed, "");
	} 
	else {
	  sprintf (sspeed, "%.1f", speed);
	}
	if (course == G_UNKNOWN) {
	  strcpy (scourse, "");
	} 
	else {
	  sprintf (scourse, "%.1f", course);
	}

// TODO:  how to handle time & date ???

	strcpy (stime, "123456");
	strcpy (sdate, "123456");

	sprintf (sentence, "$PKWDWPL,%s,V,%s,%s,%s,%s,%s,%s,%s,%s,%s,%c%c",
			stime, slat, slat_ns, slong, slong_ew, 
			sspeed, scourse, sdate, salt, wname, symtab, symbol);
	append_checksum (sentence);
	nmea_send_sentence (sentence);

/*
 *	One application recognizes these.  Not implemented at this time.
 *
 *	$GPTLL,01,ddmm.mmmm,ns,dddmm.mmmm,ew,tname,000000.00,T,R*99
 * 
 *	Where,
 *		ddmm.mmmm,ns	is latitude
 *		dddmm.mmmm,ew	is longitude
 *		tname		is the target name
 *		000000.00	is timestamp ???
 *		T		is target status (S for need help)
 *		R		is reference target ???
 *		*99		is checksum
 * 
 * 
 *	$GPTXT,01,01,tname,message*99
 *
 *	Where,
 *
 *		01		is total number of messages in transmission
 *		01		is message number in this transmission
 *		tname		is target name.  Should match name in WPL or TTL.
 *		message		is the message.  
 *		*99		is checksum
 * 
 */

} /* end nmea_send_waypoint */


#if 0

*  d710a menu 603   $GPWPL  $PMGNWPL  $PKWDWPL

symbol mapping

https://freepository.com:444/50lItuLQ7fW6s-web/browser/Tracker2/trunk/sources/waypoint.c?rev=108



Data Transmission Protocol For Magellan Products - version 2.11






$PMGNWPL,4651.529,N,07111.425,W,0000000,M,GC5A5F, GC. The straight line,a*13
 $PMGNWPL,3549.499,N,08650.827,W,0000257,M,HOME,HOME,c*4D

http://gpsbabel.sourcearchive.com/documentation/1.3.7~cvs1/magproto_8c-source.html

      sscanf(trkmsg,"$PMGNWPL,%lf,%c,%lf,%c,%d,%c,%[^,],%[^,]", 
            &latdeg,&latdir,
            &lngdeg,&lngdir,
            &alt,&altunits,shortname,descr);  then icon

   sprintf(obuf, "PMGNWPL,%4.3f,%c,%09.3f,%c,%07.0f,M,%-.*s,%-.46s,%s",
            lat, ilat < 0 ? 'S' : 'N',
            lon, ilon < 0 ? 'W' : 'E',
            waypointp->altitude == unknown_alt ?
                  0 : waypointp->altitude,
            wpt_len,
            owpt,
            odesc,
            icon_token);

https://freepository.com:444/50lItuLQ7fW6s-web/changeset/108/Tracker2/trunk/sources/waypoint.c

$PMGNWPL,4106.003,S,14640.214,E,0000069,M,KISSING SPOT,,a*3C





 *
https://freepository.com:444/50lItuLQ7fW6s-web/changeset/325


#endif


static void nmea_send_sentence (char *sent)
{

	int err;
	int len = strlen(sent);


	if (nmea_port_fd == MYFDERROR) {
	  return;
	}

	text_color_set(DW_COLOR_XMIT);
	dw_printf ("%s\n", sent);

	if (nmea_debug) {
	  // TODO: debugg out... nmea_debug_print (TO_CLIENT, NULL, nmea_buff+1, nmea_len-2);
	}

// TODO:  need to append CR LF.

	serial_port_write (nmea_port_fd, sent, len);

} /* nmea_send_sentence */



/*-------------------------------------------------------------------
 *
 * Name:        nmea_listen_thread
 *
 * Purpose:     Wait for messages from GPS receiver.
 *
 * Inputs:	arg		- File descriptor for reading.
 *
 * Outputs:	pt_slave_fd	- File descriptor for communicating with client app.
 *
 * Description:	Process messages from the client application.
 *
 *--------------------------------------------------------------------*/


// Maximum length of message from GPS receiver.
// 82 according to some people.  Larger to be safe.

#define NMEA_MAX_LEN 120	

static char gps_msg[NMEA_MAX_LEN];
int gps_msg_len = 0;


#if __WIN32__
static unsigned __stdcall nmea_listen_thread (void *arg)
#else
static void * nmea_listen_thread (void *arg)
#endif
{
	MYFDTYPE fd = (MYFDTYPE)(long)arg;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("nmea_listen_thread ( %d )\n", fd);
#endif


	while (1) {
	  int ch;

	  ch = serial_port_get1(fd);


// TODO:  if ch < 0, terminate thread ...

      //CloseHandle (fd);
	      //fd = MYFDERROR;
	      //pthread_exit (NULL);

	  //text_color_set(DW_COLOR_ERROR);
	  //dw_printf ("\nError trying to read from GPS receiver.  Closing connection %d.\n\n", fd);

	  //close (fd);  -> serial_port_close(fd)

	  //fd = MYFDERROR;
	  //pthread_exit (NULL);


	  if (ch == '$') {
	    // Start of new sentence.
	    gps_msg_len = 0;
	    gps_msg[gps_msg_len++] = ch;
	    gps_msg[gps_msg_len] = '\0';
	  }
	  else if (ch == '\r' || ch == '\n') {
	    if (gps_msg_len >= 6 && gps_msg[0] == '$') {
	      nmea_parse_gps (gps_msg);
	      text_color_set(DW_COLOR_REC);
	      dw_printf ("%s\n", gps_msg);
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

	return (NULL);	/* Unreachable but avoids compiler warning. */
}


/*-------------------------------------------------------------------
 *
 * Name:        ...
 *
 * Purpose:     ...
 *
 * Inputs:	...
 *
 * Outputs:	...
 *
 * Description:	...
 *
 *--------------------------------------------------------------------*/


static void remove_checksum (char *sent)
{
        char *p;
        char *next;
        unsigned char cs;


// Do we have valid checksum?

        cs = 0;
        for (p = sent+1; *p != '*' && *p != '\0'; p++) {
          cs ^= *p;
        }

        p = strchr (sent, '*');
        if (p == NULL) {
	  text_color_set (DW_COLOR_INFO);
          dw_printf("Missing GPS checksum.\n");
          return;
        }
        if (cs != strtoul(p+1, NULL, 16)) {
	  text_color_set (DW_COLOR_ERROR);
          dw_printf("GPS checksum error. Expected %02x but found %s.\n", cs, p+1);
          return;
        }
        *p = '\0';      // Remove the checksum.
}

static void nmea_parse_gps (char *sentence)
{

	char stemp[NMEA_MAX_LEN];
	char *ptype;
	char *next;
	double g_lat, g_lon;
	float g_speed, g_course;
	static float g_alt = G_UNKNOWN;
	int fix;		/* 0=none, 2=2D, 3=3D */

	strcpy (stemp, sentence);

	// TODO: process only if good.
	remove_checksum (stemp);

	next = stemp;
	ptype = strsep(&next, ",");


// $GPRMC has everything we care about except altitude.
//
// Examples: $GPRMC,212404.000,V,4237.1505,N,07120.8602,W,,,150614,,*0B
//	     $GPRMC,000029.020,V,,,,,,,080810,,,N*45
//	     $GPRMC,003413.710,A,4237.1240,N,07120.8333,W,5.07,291.42,160614,,,A*7F

	if (strcmp(ptype, "$GPRMC") == 0)
	{

	  char *ptime;			/* Time, hhmmss[.sss] */
	  char *pstatus;		/* Status, A=Active (valid position), V=Void */
	  char *plat;			/* Latitude */
	  char *pns;			/* North/South */
	  char *plon;			/* Longitude */
	  char *pew;			/* East/West */
	  char *pknots;			/* Speed over ground, knots. */
	  char *pcourse;		/* True course, degrees. */
	  char *pdate;			/* Date, ddmmyy */
					/* Magnetic variation */
					/* In version 3.00, mode is added: A D E N (see below) */
					/* Checksum */

	  ptime = strsep(&next, ",");
	  pstatus = strsep(&next, ",");	
	  plat = strsep(&next, ",");
	  pns = strsep(&next, ",");
	  plon = strsep(&next, ",");
	  pew = strsep(&next, ",");
	  pknots = strsep(&next, ",");
	  pcourse = strsep(&next, ",");
	  pdate = strsep(&next, ",");	


	  g_lat = G_UNKNOWN;
	  g_lon = G_UNKNOWN;
	  g_speed = G_UNKNOWN;
	  g_course = G_UNKNOWN;

	  if (plat != NULL && strlen(plat) > 0) {
	    g_lat = latitude_from_nmea(plat, pns);
	  }
	  if (plon != NULL && strlen(plon) > 0) {
	    g_lon = longitude_from_nmea(plon, pew);
	  }
	  if (pknots != NULL && strlen(pknots) > 0) {
	    g_speed = atof(pknots);
	  }
	  if (pcourse != NULL && strlen(pcourse) > 0) {
	    g_course = atof(pcourse);
	  }

	  if (*pstatus == 'A') {
	    if (g_alt != G_UNKNOWN) {
	      fix = 3;
	    }
	    else {
	      fix = 2;
	    }
	  }
	  else {
	    fix = 0;
	  }

	  text_color_set (DW_COLOR_INFO);
          dw_printf("%d %.6f %.6f %.1f %.0f %.1f\n", fix, g_lat, g_lon, g_speed, g_course, g_alt);

#if WALK96
	  // TODO: Need to design a proper interface.

	  extern void walk96 (int fix, double lat, double lon, float knots, float course, float alt);

          walk96 (fix, g_lat, g_lon, g_speed, g_course, g_alt);
#endif	  
	}

// $GPGGA has altitude.
//
// Examples: $GPGGA,212407.000,4237.1505,N,07120.8602,W,0,00,,,M,,M,,*58
//	     $GPGGA,000409.392,,,,,0,00,,,M,0.0,M,,0000*53
//	     $GPGGA,003518.710,4237.1250,N,07120.8327,W,1,03,5.9,33.5,M,-33.5,M,,0000*5B

	else if (strcmp(ptype, "$GPGGA") == 0)
	{

	  char *ptime;			/* Time, hhmmss[.sss] */
	  char *plat;			/* Latitude */
	  char *pns;			/* North/South */
	  char *plon;			/* Longitude */
	  char *pew;			/* East/West */
	  char *pfix;			/* 0=invalid, 1=GPS fix, 2=DGPS fix */
	  char *pnum_sat;		/* Number of satellites */
	  char *phdop;			/* Horiz. Dilution fo Precision */
	  char *paltitude;		/* Altitude, above mean sea level */
	  char *palt_u;			/* Units for Altitude, typically M for meters. */
	  char *pheight;		/* Height above ellipsoid */
	  char *pheight_u;		/* Units for height, typically M for meters. */
	  char *psince;			/* Time since last DGPS update. */
	  char *pdsta;			/* DGPS reference station id. */

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

	  g_alt = G_UNKNOWN;

	  if (paltitude != NULL && strlen(paltitude) > 0) {
	    g_alt = atof(paltitude);
	  }
	}


} /* end  nmea_parse_gps */

/* end nmea.c */
