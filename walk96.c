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


//#define DEBUG 1

/*------------------------------------------------------------------
 *
 * Module:      walk96.c
 *
 * Purpose:   	Quick hack to read GPS location and send very frequent 
 *		position reports frames to a KISS TNC.
 *
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

#include "direwolf.h"
#include "config.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "latlong.h"
#include "nmea.h"
#include "encode_aprs.h"
#include "serial_port.h"


#define MYCALL "WB2OSZ"			/************ Change this if you use it!!!  ***************/

static MYFDTYPE tnc;


main (int argc, char *argv[])
{
	struct misc_config_s config;
	char cmd[100];


	// Look for Silicon Labs CP210x
	// Just happens to be same on desktop & laptop.

	tnc = serial_port_open ("COM5", 9600);	
	if (tnc == MYFDERROR) {
	  text_color_set (DW_COLOR_ERROR);
          dw_printf ("Can't open serial port to KISS TNC.\n");
	  exit (EXIT_FAILURE);	// defined in stdlib.h
  	}

	strcpy (cmd, "\r\rhbaud 9600\rkiss on\rrestart\r");

	serial_port_write (tnc, cmd, strlen(cmd));
	SLEEP_MS(500);

	memset (&config, 0, sizeof(config));
	strcpy (config.nmea_port, "COM1");
	nmea_init (&config);

	SLEEP_SEC(20);

	// Exit out of KISS mode.

	serial_port_write (tnc, "\xc0\xff\c0", 3);

	SLEEP_MS(100);

}


/* Should be called once per second. */

void walk96 (int fix, double lat, double lon, float knots, float course, float alt)
{
	static int sequence = 0;
	char comment[50];

	sequence++;
	sprintf (comment, "Sequence number %04d", sequence);


/*
 * Construct the packet in normal monitoring format.
 */

	int messaging = 0;
	int compressed = 0;

	char info[AX25_MAX_INFO_LEN];
	int info_len;

	char position_report[AX25_MAX_PACKET_LEN];

	info_len = encode_position (messaging, compressed,
		lat, lon, (int)(DW_METERS_TO_FEET(alt)), 
		'/', '?',		// TODO: look up code for person.
		G_UNKNOWN, G_UNKNOWN, G_UNKNOWN, "",	// PHG
		(int)course, (int)knots, 
		445.925, 0, 0,
		comment,
		info, sizeof(info));

	sprintf (position_report, "%s>WALK96:%s", MYCALL, info);

	text_color_set (DW_COLOR_XMIT);
        dw_printf ("%s\n", position_report);

/*
 * Convert it into AX.25 frame.
 */
	packet_t pp;
	unsigned char ax25_frame[AX25_MAX_PACKET_LEN];
	int frame_len;

	pp = ax25_from_text (position_report, 1);

	if (pp == NULL) {
	  text_color_set (DW_COLOR_ERROR);
          dw_printf ("Unexpected error in ax25_from_text.  Quitting.\n");
	  exit (EXIT_FAILURE);	// defined in stdlib.h
	}

	ax25_frame[0] = 0;	// Insert channel before KISS encapsulation. 

	frame_len = ax25_pack (pp, ax25_frame+1);
	ax25_delete (pp);

/*
 * Encapsulate as KISS and send to TNC.
 */
	
	unsigned char kiss_frame[AX25_MAX_PACKET_LEN*2];
	int kiss_len;

	kiss_len = kiss_encapsulate (ax25_frame, frame_len+1, (unsigned char *)kiss_frame);

	//text_color_set (DW_COLOR_DEBUG);
        //dw_printf ("AX.25 frame length = %d, KISS frame length = %d\n", frame_len, kiss_len);

	//kiss_debug_print (1, NULL, kiss_frame, kiss_len);

	serial_port_write (tnc, kiss_frame, kiss_len);

}

/* end walk96.c */
