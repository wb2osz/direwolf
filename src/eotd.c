
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2022  David Tiller, K4DET
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


/********************************************************************************
 *
 * File:	eotd.c
 *
 * Purpose:	Functions for processing received EOTD transmissions and
 *		converting to NMEA sentence representation.
 *
 * References:	AIVDM/AIVDO protocol decoding by Eric S. Raymond
 *		https://gpsd.gitlab.io/gpsd/AIVDM.html
 *
 *		Sample recording with about 100 messages.  Test with "atest -B AIS xxx.wav"
 *		https://github.com/freerange/ais-on-sdr/wiki/example-data/long-beach-160-messages.wav
 *
 *		Useful on-line decoder for AIS NMEA sentences.
 *		https://www.aggsoft.com/ais-decoder.htm
 *
 * Future?	Add an interface to feed AIS data into aprs.fi.
 *		https://aprs.fi/page/ais_feeding
 *		
 *******************************************************************************/

#include "direwolf.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "textcolor.h"
#include "eotd.h"

/*-------------------------------------------------------------------
 *
 * Convert EOTD binary block (from HDLC frame) to NMEA sentence. 
 *
 * In:	Pointer to EOTD binary block and number of bytes.
 * Out:	NMEA sentence.  Provide size to avoid string overflow.
 *
 *--------------------------------------------------------------------*/

void eotd_to_nmea (unsigned char *eotd, int eotd_len, char *nmea, int nmea_size)
{
	time_t now = time(NULL);
	*nmea = '\0';
	strcat(nmea, ctime(&now));
	for (int i = 0; i < eotd_len; i++) {
		char temp[32];
		snprintf(temp, sizeof(temp), " %02x", eotd[i]);
		strlcat(nmea, temp, nmea_size);
	}
}
