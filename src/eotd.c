
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
 *		converting to text format.
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
 * Convert EOTD binary block (from HDLC frame) to text.
 *
 * In:	Pointer to EOTD binary block and number of bytes.
 * Out:	text.
 *
 *--------------------------------------------------------------------*/

void eotd_to_text (unsigned char *eotd, int eotd_len, char *text, int text_size)
{
	time_t now = time(NULL);
	*text = '\0';
	strcat(text, ctime(&now));
	for (int i = 0; i < eotd_len; i++) {
		char temp[32];
		snprintf(temp, sizeof(temp), " %02x", eotd[i]);
		strlcat(text, temp, text_size);
	}
}
