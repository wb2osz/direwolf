//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014, 2015, 2016, 2020  John Langner, WB2OSZ
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
 * Module:      waypoint.c
 *
 * Purpose:   	Send NMEA waypoint sentences to GPS display or mapping application.
 *		
 *---------------------------------------------------------------*/


#include "direwolf.h"		// should be first

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#if __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>           // _WIN32_WINNT must be set to 0x0501 before including this
#else
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
//#include <arpa/inet.h>
#include <netdb.h>		// gethostbyname
#endif

#include <assert.h>
#include <string.h>
#include <time.h>


#include "config.h"
#include "textcolor.h"
#include "latlong.h"
#include "waypoint.h"
#include "grm_sym.h"		/* Garmin symbols */
#include "mgn_icon.h"		/* Magellan icons */
#include "dwgpsnmea.h"
#include "serial_port.h"
#include "dwsock.h"


static MYFDTYPE s_waypoint_serial_port_fd = MYFDERROR;
static int s_waypoint_udp_sock_fd = -1;	// ideally INVALID_SOCKET for Windows.
static struct sockaddr_in s_udp_dest_addr;

static int s_waypoint_formats = 0;	/* which formats should we generate? */

static int s_waypoint_debug = 0;	/* Print information flowing to attached device. */



static void append_checksum (char *sentence);
static void send_sentence (char *sent);



void waypoint_set_debug (int n) 
{	
	s_waypoint_debug = n;
}


/*-------------------------------------------------------------------
 *
 * Name:	waypoint_init
 *
 * Purpose:	Initialization for waypoint output port.
 *
 * Inputs:	mc			- Pointer to configuration options.
 *
 *		  ->waypoint_serial_port	- Name of serial port.  COM1, /dev/ttyS0, etc.
 *
 *		  ->waypoint_udp_hostname	- Destination host when using UDP.
 *
 *		  ->waypoint_udp_portnum	- UDP port number.
 *
 *		  (currently none)	- speed, baud.  Default 4800 if not set
 *
 *
 *		  ->waypoint_formats	- Set of formats enabled. 
 *					  If none set, default to generic & Kenwood here.
 *
 * Global output: s_waypoint_serial_port_fd
 *		  s_waypoint_udp_sock_fd
 *
 * Description:	First to see if this is shared with GPS input.
 *		If not, open serial port.
 *		In version 1.6 UDP is added.  It is possible to use both.
 *
 * Restriction:	MUST be done after GPS init because we might be sharing the
 *		same serial port device.
 *
 *---------------------------------------------------------------*/


void waypoint_init (struct misc_config_s *mc)
{

#if DEBUG
	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("waypoint_init() serial device=%s formats=%02x\n", mc->waypoint_serial_port, mc->waypoint_formats);
	dw_printf ("waypoint_init() destination hostname=%s UDP port=%d\n", mc->waypoint_udp_hostname, mc->waypoint_udp_portnum);
#endif

	s_waypoint_udp_sock_fd = -1;

	if (mc->waypoint_udp_portnum > 0) {

	  s_waypoint_udp_sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	  if (s_waypoint_udp_sock_fd != -1) {

	    // Not thread-safe.  Should use getaddrinfo instead.
	    struct hostent *hp = gethostbyname(mc->waypoint_udp_hostname);

	    if (hp != NULL) {
	      memset ((char *)&s_udp_dest_addr, 0, sizeof(s_udp_dest_addr));
	      s_udp_dest_addr.sin_family = AF_INET;
	      memcpy ((char *)&s_udp_dest_addr.sin_addr, (char *)hp->h_addr, hp->h_length);
	      s_udp_dest_addr.sin_port = htons(mc->waypoint_udp_portnum);
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Waypoint: Couldn't get address for %s\n", mc->waypoint_udp_hostname);
	      close (s_waypoint_udp_sock_fd);
	      s_waypoint_udp_sock_fd = -1;
	    }
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Couldn't create socket for waypoint send to %s\n", mc->waypoint_udp_hostname);
	  }
	}

/*
 * TODO:
 * Are we sharing with GPS input?
 * First try to get fd if they have same device name.
 * If that fails, do own serial port open.
 */
	s_waypoint_serial_port_fd = MYFDERROR;

	if (strlen(mc->waypoint_serial_port) > 0) {

	  s_waypoint_serial_port_fd = dwgpsnmea_get_fd (mc->waypoint_serial_port, 4800);

	  if (s_waypoint_serial_port_fd == MYFDERROR) {
	    s_waypoint_serial_port_fd = serial_port_open (mc->waypoint_serial_port, 4800);
	  }
	  else {
	    text_color_set (DW_COLOR_INFO);
	    dw_printf ("Note: Sharing same port for GPS input and waypoint output.\n");
	  }

	  if (s_waypoint_serial_port_fd == MYFDERROR) {
	    text_color_set (DW_COLOR_ERROR);
	    dw_printf ("Unable to open serial port %s for waypoint output.\n", mc->waypoint_serial_port);
	  }
	}

// Set default formats if user did not specify any.

	s_waypoint_formats = mc->waypoint_formats;
	if (s_waypoint_formats == 0) {
	  s_waypoint_formats = WPL_FORMAT_NMEA_GENERIC | WPL_FORMAT_KENWOOD;
	}
	if (s_waypoint_formats & WPL_FORMAT_GARMIN) {
	  s_waypoint_formats |= WPL_FORMAT_NMEA_GENERIC;		/* See explanation below. */
	}

#if DEBUG
	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("end of waypoint_init: s_waypoint_serial_port_fd = %d\n", s_waypoint_serial_port_fd);
	dw_printf ("end of waypoint_init: s_waypoint_udp_sock_fd = %d\n", s_waypoint_udp_sock_fd);
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
 *		Don't add CR/LF at this point.
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

} /* end append_checksum */



/*-------------------------------------------------------------------
 *
 * Name:        nema_send_waypoint
 *
 * Purpose:     Convert APRS position or object into NMEA waypoint sentence
 *		for use by a GPS display or other mapping application.
 *		
 * Inputs:	wname_in	- Name of waypoint.  Max of 9 characters.
 *		dlat		- Latitude.
 *		dlong		- Longitude.
 *		symtab		- Symbol table or overlay character. 
 *		symbol		- Symbol code.
 *		alt		- Altitude in meters or G_UNKNOWN.
 *		course		- Course in degrees or G_UNKNOWN for unknown.
 *		speed		- Speed in knots or G_UNKNOWN.
 *		comment_in	- Description or message.
 *		
 *
 * Description:	Currently we send multiple styles.  Maybe someday there might
 * 		be an option to send a selected subset.
 *
 *			$GPWPL		- NMEA generic with only location and name.
 *			$PGRMW		- Garmin, adds altitude, symbol, and comment
 *					  to previously named waypoint.
 *			$PMGNWPL	- Magellan, more complete for stationary objects.
 *			$PKWDWPL	- Kenwood with APRS style symbol but missing comment.
 *
*
 * AvMap G5 notes:
 *
 *		https://sites.google.com/site/kd7kuj/home/files?pli=1
 *		https://sites.google.com/site/kd7kuj/home/files/AvMapMessaging040810.pdf?attredirects=0&d=1
 *
 *		It sends $GPGGA & $GPRMC with location.
 *		It understands generic $GPWPL and Kenwood $PKWDWPL.
 *
 *		There are some proprietary $PAVP* used only for messaging.
 *		Messaging would be a separate project.
 *
 *--------------------------------------------------------------------*/


void waypoint_send_sentence (char *name_in, double dlat, double dlong, char symtab, char symbol, 
			float alt, float course, float speed, char *comment_in)
{
	char wname[12];		/* Waypoint name.  Any , or * removed. */
	char slat[12];		/* DDMM.mmmm */
	char slat_ns[2];	/* N or S */
	char slong[12];		/* DDDMM.mmmm */
	char slong_ew[2];	/* E or W */
	char wcomment[256];	/* Comment.  Any , or * removed. */
	char salt[12];		/* altitude as string, empty if unknown */
	char sspeed[12];	/* speed as string, empty if unknown */
	char scourse[12];	/* course as string, empty if unknown */
	char *p;

	char sentence[500];

#if DEBUG
	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("waypoint_send_sentence (\"%s\", \"%c%c\")\n", name_in, symtab, symbol);
#endif

// Don't waste time if no destinations specified.

	if (s_waypoint_serial_port_fd == MYFDERROR &&
	    s_waypoint_udp_sock_fd == -1) {
	  return;
	}

/*
 * We need to remove any , or * from name, symbol, or comment because they are field delimiters.
 * Follow precedent of Geosat AvMap $PAVPMSG sentence and make the following substitutions:
 *
 *		,  ->  |
 *		*  ->  ~
 *
 * The system on the other end would need to change them back after extracting the 
 * fields delimited by , or *.
 * We will deal with the symbol in the Kenwood section.
 * Needs to be left intact for other icon/symbol conversions.
 */

	strlcpy (wname, name_in, sizeof(wname));
	for (p=wname; *p != '\0'; p++) {
	  if (*p == ',') *p = '|';
	  if (*p == '*') *p = '~';
	}

	strlcpy (wcomment, comment_in, sizeof(wcomment));
	for (p=wcomment; *p != '\0'; p++) {
	  if (*p == ',') *p = '|';
	  if (*p == '*') *p = '~';
	}


/*
 * Convert numeric values to character form.
 * G_UNKNOWN value will result in an empty string.
 */

	latitude_to_nmea (dlat, slat, slat_ns);
	longitude_to_nmea (dlong, slong, slong_ew);


	if (alt == G_UNKNOWN) {
	  strcpy (salt, "");
	} 
	else {
	  snprintf (salt, sizeof(salt), "%.1f", alt);
	}

	if (speed == G_UNKNOWN) {
	  strcpy (sspeed, "");
	} 
	else {
	  snprintf (sspeed, sizeof(sspeed), "%.1f", speed);
	}

	if (course == G_UNKNOWN) {
	  strcpy (scourse, "");
	} 
	else {
	  snprintf (scourse, sizeof(scourse), "%.1f", course);
	}


/*
 *	NMEA Generic.
 *
 *	Has only location and name.  Rather disappointing. 
 *
 *	$GPWPL,ddmm.mmmm,ns,dddmm.mmmm,ew,wname*99
 *
 *	Where,
 *	 	ddmm.mmmm,ns	is latitude
 *		dddmm.mmmm,ew	is longitude
 *		wname		is the waypoint name
 *		*99		is checksum
 */

	if (s_waypoint_formats & WPL_FORMAT_NMEA_GENERIC) {

	  snprintf (sentence, sizeof(sentence), "$GPWPL,%s,%s,%s,%s,%s", slat, slat_ns, slong, slong_ew, wname);
	  append_checksum (sentence);
	  send_sentence (sentence);
	}


/*
 *	Garmin
 *
 *	https://www8.garmin.com/support/pdf/NMEA_0183.pdf
 *
 *	No location!  Adds altitude, symbol, and comment to existing waypoint.
 *	So, we should always send the NMEA generic waypoint before this one.
 *	The init function should take care of that.
 *
 *	$PGRMW,wname,alt,symbol,comment*99
 *
 *	Where,
 *
 *		wname		is waypoint name.  Must match existing waypoint.
 *		alt		is altitude in meters.
 *		symbol		is symbol code.  Hexadecimal up to FFFF.
 *					See Garmin Device Interface Specification
 *					001-0063-00 for values of "symbol_type."
 *		comment		is comment for the waypoint.  
 *		*99		is checksum
 */

	if (s_waypoint_formats & WPL_FORMAT_GARMIN) {

	  int i = symbol - ' ';
	  int grm_sym;		/* Garmin symbol code. */

	  if (i >= 0 && i < SYMTAB_SIZE) {	  
	    if (symtab == '/') {
	      grm_sym = grm_primary_symtab[i];
	    }
	    else {
	      grm_sym = grm_alternate_symtab[i];
	    }
	  }
	  else {
	    grm_sym = sym_default;
	  }

	  snprintf (sentence, sizeof(sentence), "$PGRMW,%s,%s,%04X,%s", wname, salt, grm_sym, wcomment);
	  append_checksum (sentence);
	  send_sentence (sentence);
	}


/*
 *	Magellan
 *
 *	http://www.gpsinformation.org/mag-proto-2-11.pdf	Rev 2.11, Mar 2003, P/N 21-00091-000
 *	http://gpsinformation.net/mag-proto.htm			Rev 1.0,  Aug 1999, P/N 21-00091-000
 *
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
 *					defined, and not used in their example
 *					so we won't use it.					
 *		*99		is checksum
 *
 * Possible enhancement:  If the "object report" has the kill option set, use $PMGNDWP
 * to delete that specific waypoint.
 */

	if (s_waypoint_formats & WPL_FORMAT_MAGELLAN) {

	  int i = symbol - ' ';
	  char sicon[3];		/* Magellan icon string.  Currently 1 or 2 characters. */

	  if (i >= 0 && i < SYMTAB_SIZE) {	  
	    if (symtab == '/') {
	      strlcpy (sicon, mgn_primary_symtab[i], sizeof(sicon));
	    }
	    else {
	      strlcpy (sicon, mgn_alternate_symtab[i], sizeof(sicon));
	    }
	  }
	  else {
	    strlcpy (sicon, MGN_default, sizeof(sicon));
	  }

	  snprintf (sentence, sizeof(sentence), "$PMGNWPL,%s,%s,%s,%s,%s,M,%s,%s,%s",
			slat, slat_ns, slong, slong_ew, salt, wname, wcomment, sicon);
	  append_checksum (sentence);
	  send_sentence (sentence);
	}


/*
 *	Kenwood
 *
 *
 *	$PKWDWPL,hhmmss,v,ddmm.mm,ns,dddmm.mm,ew,speed,course,ddmmyy,alt,wname,ts*99
 *
 *	Where,
 *		hhmmss		is time in UTC from the clock in the transceiver.
 *
 *					This will be bogus if the clock was not set properly.
 *					It does not use the timestamp from a position
 *					report which could be useful.
 *
 *		GPS Status	A = active, V = void.
 *					It looks like this might be modeled after the GPS status values
 *					we see in $GPRMC.  i.e. Does the transceiver know its location?
 *					I don't see how that information would be relevant in this context.
 *					I've observed this under various conditions (No GPS, GPS with/without 
 *					fix) and it has always been "V."
 *					(There is some information out there indicating this field
 *					can contain "I" for invalid but I don't think that is accurate.)
 *	
 *	 	ddmm.mm,ns	is latitude. N or S.
 *		dddmm.mm,ew	is longitude.  E or W.
 *
 *					The D710 produces two fractional digits for minutes.
 *					This is the same resolution most often used
 *					in APRS packets.  Any additional resolution offered by
 *					the compressed format or the DAO option is not conveyed here.
 *					We will provide greater resolution.
 * 
 *		speed		is speed over ground, knots.
 *		course		is course over ground, degrees.
 *
 *					Empty if not available. 
 *
 *		ddmmyy		is date.  See comments for time.
 *
 *		alt		is altitude, meters above mean sea level.
 *
 *					Empty if no altitude is available. 
 *
 *		wname		is the waypoint name.  For an Object Report, the id is the object name.
 *					For a position report, it is the call of the sending station.
 *
 *					An Object name can contain any printable characters.
 *					What if object name contains , or * characters?
 *					Those are field delimiter characters and it would be unfortunate
 *					if they appeared in a NMEA sentence data field.
 *
 *					If there is a comma in the name, such as "test,5" the D710A displays
 *					it fine but we end up with an extra field.
 *
 *						$PKWDWPL,150803,V,4237.14,N,07120.83,W,,,190316,,test,5,/'*30
 *
 *					If the name contains an asterisk, it doesn't show up on the 
 *					display and no waypoint sentence is generated.
 *					We will substitute these two characters following the AvMap precedent.
 *
 *						$PKWDWPL,204714,V,4237.1400,N,07120.8300,W,,,200316,,test|5,/'*61
 *						$PKWDWPL,204719,V,4237.1400,N,07120.8300,W,,,200316,,test~6,/'*6D
 *
 *		ts		are the table and symbol.
 *
 *					What happens if the symbol is comma or asterisk?
 *						, Boy Scouts / Girl Scouts
 *						* SnowMobile / Snow
 *
 *					the D710A just pushes them thru without checking.
 *					These would not be parsed properly:
 *
 *						$PKWDWPL,150753,V,4237.14,N,07120.83,W,,,190316,,test3,/,*1B
 *						$PKWDWPL,150758,V,4237.14,N,07120.83,W,,,190316,,test4,/ **3B
 *
 *					We perform the usual substitution and the other end would
 *					need to change them back after extracting from NMEA sentence.
 *
 *						$PKWDWPL,204704,V,4237.1400,N,07120.8300,W,,,200316,,test3,/|*41
 *						$PKWDWPL,204709,V,4237.1400,N,07120.8300,W,,,200316,,test4,/~*49
 *					
 *			
 *		*99		is checksum
 *
 *	Oddly, there is no place for comment.
 */


 
	if (s_waypoint_formats & WPL_FORMAT_KENWOOD) {

	  time_t now;
	  struct tm tm;
	  char stime[8];
	  char sdate[8];
	  char ken_sym;		/* APRS symbol with , or * substituted. */

	  now = time(NULL);
	  (void)gmtime_r (&now, &tm);
	  strftime (stime, sizeof(stime), "%H%M%S", &tm);
	  strftime (sdate, sizeof(sdate), "%d%m%y", &tm);

	  // A symbol code of , or * would not be good because 
	  // they are field delimiters for NMEA sentences.

	  // The AvMap G5 to Kenwood protocol description performs a substitution
	  // for these characters that appear in message text.
	  //		,	->	|
	  //		*	->	~

	  // Those two are listed as "TNC Stream Switch" and are not used for symbols.
	  // It might be reasonable assumption that this same substitution might be
 	  // used for the symbol code.   

	  if (symbol == ',') ken_sym = '|';
	  else if (symbol == '*') ken_sym = '~';
	  else ken_sym = symbol;

	  snprintf (sentence, sizeof(sentence), "$PKWDWPL,%s,V,%s,%s,%s,%s,%s,%s,%s,%s,%s,%c%c",
			stime, slat, slat_ns, slong, slong_ew, 
			sspeed, scourse, sdate, salt, wname, symtab, ken_sym);
	  append_checksum (sentence);
	  send_sentence (sentence);
	}


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


} /* end waypoint_send_sentence */


/*-------------------------------------------------------------------
 *
 * Name:        nema_send_ais
 *
 * Purpose:     Send NMEA AIS sentence to GPS display or other mapping application.
 *		
 * Inputs:	sentence	- should look something like this, with checksum, and no CR LF.
 *
 *			!AIVDM,1,1,,A,35NO=dPOiAJriVDH@94E84AJ0000,0*4B
 *
 *--------------------------------------------------------------------*/

void waypoint_send_ais (char *sentence)
{
	if (s_waypoint_serial_port_fd == MYFDERROR &&
	    s_waypoint_udp_sock_fd == -1) {
	  return;
	}

	if (s_waypoint_formats & WPL_FORMAT_AIS) {
	  send_sentence (sentence);
	}
}


/*
 * Append CR LF and send it.
 */

static void send_sentence (char *sent)
{
	char final[256];

	if (s_waypoint_debug) {
	  text_color_set(DW_COLOR_XMIT);
	  dw_printf ("waypoint send sentence: \"%s\"\n", sent);
	}

	strlcpy (final, sent, sizeof(final));
	strlcat (final, "\r\n", sizeof(final));
	int final_len = strlen(final);

	if (s_waypoint_serial_port_fd != MYFDERROR) {
	  serial_port_write (s_waypoint_serial_port_fd, final, final_len);
	}

	if (s_waypoint_udp_sock_fd != -1) {
	  int n = sendto(s_waypoint_udp_sock_fd, final, final_len, 0, (struct sockaddr*)(&s_udp_dest_addr), sizeof(struct sockaddr_in));
	  if (n != final_len) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Failed to send waypoint via UDP, errno=%d\n", errno);
	  }
	}

} /* send_sentence */



void waypoint_term (void)
{
	if (s_waypoint_serial_port_fd != MYFDERROR) {
	  //serial_port_close (s_waypoint_port_fd);
	  s_waypoint_serial_port_fd = MYFDERROR;
	}
	if (s_waypoint_udp_sock_fd != -1) {
	  close (s_waypoint_udp_sock_fd);
	  s_waypoint_udp_sock_fd = -1;
	}
}


/* end waypoint.c */
