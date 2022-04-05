
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
#include <sys/time.h>

#include "textcolor.h"
#include "eotd_defs.h"
#include "eotd.h"

#undef EOTD_RAW
#define EOTD_TIMESTAMP
#define EOTD_APPEND_HEX

/*-------------------------------------------------------------------
 *
 * Convert EOTD binary block (from HDLC frame) to text.
 *
 * In:	Pointer to EOTD binary block and number of bytes.
 * Out:	text.
 *
 *--------------------------------------------------------------------*/

void add_comma(char *text, int text_size) {
	strlcat(text, ",", text_size);
}

void get_r2f_chain(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	val = pkt & 0x03ULL;

	strlcat(text, "chain=", text_size);

	switch(val) {
		case 0:
		  strlcat(text, "MIDDLE", text_size);
		  break;
		case 1:
		  strlcat(text, "LAST", text_size);
		  break;
		case 2:
		  strlcat(text, "FIRST", text_size);
		  break;
		case 3:
		  strlcat(text, "ONLY", text_size);
		  break;
	}
}

void get_r2f_dev_batt_stat(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	pkt >>= 2;
	val = pkt & 0x03ULL;

	strlcat(text, "devbat=", text_size);

	switch(val) {
	  case 3:
	    strlcat(text, "OK", text_size);
	    break;
	  case 2:
	    strlcat(text, "WEAK", text_size);
	    break;
	  case 1:
	    strlcat(text, "VERY_WEAK", text_size);
	    break;
	  case 0:
	    strlcat(text, "NOT_MONITORED", text_size);
	    break;
	}
}

void get_r2f_msg_id_type(uint64_t pkt, char *text, int text_size) {
	uint32_t val;
	char temp[32];

	pkt >>= 4;
	val = pkt & 0x07ULL;

	strlcat(text, "msgid=", text_size);

	switch(val) {
	  case 0:
	    strlcat(text, "ONEWAY", text_size);
	    break;

	  default:
	    sprintf(temp, "CUSTOM(%d)", val);
	    strlcat(text, temp, text_size);
	    break;
	}
}

void get_r2f_unit_addr_code(uint64_t pkt, char *text, int text_size) {
	uint32_t val;
	char temp[32];

	pkt >>= 7;
	val = pkt & 0x1ffffULL;
	strlcat(text, "unit_addr=", text_size);
	sprintf(temp, "%d", val);
	strlcat(text, temp, text_size);
}

void get_r2f_brake_pressure(uint64_t pkt, char *text, int text_size) {
	uint32_t val;
	char temp[32];

	pkt >>= 24;
	val = pkt & 0x7fULL;

	strlcat(text, "brake_status=", text_size);

	switch (val) {
	  case 127:
	    strlcat(text, "GO", text_size);
	    break;

	  case 126:
	    strlcat(text, "NO-GO", text_size);
	    break;

	  default:
	    if (val < 45) {
		sprintf(temp, "NO-GO(%d psig)", val);
	    } else {
	    	sprintf(temp, "GO(%d psig)", val);
	    }

	    strlcat(text, temp, text_size);
	    break;
	}
}

void get_r2f_disc_bits(uint64_t pkt, char *text, int text_size) {
	uint32_t val;
	char temp[32];

	pkt >>= 31;
	val = pkt & 0xffULL;

	strlcat(text, "disc_bits=", text_size);
	sprintf(temp, "%02x", val);
	strlcat(text, temp, text_size);
}

void get_r2f_valve_bit(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	pkt >>= 39;
	val = pkt & 0x01;

	strlcat(text, "valve=", text_size);
	strlcat(text, val == 0 ? "FAILED" : "OPERATIONAL", text_size); 
}

void get_r2f_confirm_bit(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	pkt >>= 40;
	val = pkt & 0x01;

	strlcat(text, "confirm=", text_size);
	strlcat(text, val == 0 ? "UPDATE" : "RESPONSE", text_size); 
}

void get_r2f_disc_bit1(uint64_t pkt, char *text, int text_size) {
	uint32_t val;
	char temp[32];

	pkt >>= 41;
	val = pkt & 0x01;

	strlcat(text, "disc_bit_1=", text_size);
	sprintf(temp, "%d", val);
	strlcat(text, temp, text_size); 
}

void get_r2f_motion_bit(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	pkt >>= 42;
	val = pkt & 0x01;

	strlcat(text, "motion=", text_size);
	strlcat(text, val == 0 ? "STOPPED/NOT_MONITORED" : "IN_MOTION", text_size); 
}

void get_r2f_mkr_light_batt_bit(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	pkt >>= 43;
	val = pkt & 0x01;

	strlcat(text, "light_batt=", text_size);
	strlcat(text, val == 0 ? "OK/NOT_MONITORED" : "WEAK", text_size);
}

void get_r2f_mkr_light_bit(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	pkt >>= 44;
	val = pkt & 0x01;

	strlcat(text, "light=", text_size);
	strlcat(text, val == 0 ? "OFF/NOT_MONITORED" : "ON", text_size);
}

void decode_basic_r2f(uint64_t pkt, char *text, int text_size) {

	get_r2f_chain(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_dev_batt_stat(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_msg_id_type(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_unit_addr_code(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_brake_pressure(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_disc_bits(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_valve_bit(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_confirm_bit(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_disc_bit1(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_motion_bit(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_mkr_light_batt_bit(pkt, text, text_size);
	add_comma(text, text_size);
	get_r2f_mkr_light_bit(pkt, text, text_size);
}

void get_f2r_chain(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	val = pkt & 0x03;

	strlcat(text, "chain=", text_size);

	if (val == 3) {
		strlcat(text, "VALID", text_size);
	} else {
		strlcat(text, "INVALID", text_size);
	}
}

void get_f2r_msg_id_type(uint64_t pkt, char *text, int text_size) {
	uint32_t val;

	pkt >>= 2;
	val = pkt & 0x07ULL;

	strlcat(text, "msgid=", text_size);
	strlcat(text, val == 0 ? "VALID" : "INVALID", text_size);
}

void get_f2r_unit_addr_code(uint64_t pkt, char *text, int text_size) {
	uint32_t val;
	char temp[32];

	pkt >>= 5;
	val = pkt & 0x1ffffULL;
	strlcat(text, "unit_addr=", text_size);
	sprintf(temp, "%d", val);
	strlcat(text, temp, text_size);
}

void get_f2r_command(uint64_t pkt, char *text, int text_size) {
	uint32_t val;
	char temp[32];

	pkt >>= 22;
	val = pkt & 0xff;
	strlcat(text, "cmd=", text_size);
	switch(val) {
	  case 0x55:
	    strlcat(text, "STATUS_REQ", text_size);
	    break;

	  case 0xaa:
	    strlcat(text, "APPLY_BRAKES", text_size);
	    break;

	  default:
	    sprintf(temp, "UNKNOWN(%d)", val);
	    strlcat(text, temp, text_size);
	    break;
	}
}

void decode_basic_f2r(uint64_t pkt, char *text, int text_size) {

	get_f2r_chain(pkt, text, text_size);
	add_comma(text, text_size);
	get_f2r_msg_id_type(pkt, text, text_size);
	add_comma(text, text_size);
	get_f2r_unit_addr_code(pkt, text, text_size);
	add_comma(text, text_size);
	get_f2r_command(pkt, text, text_size);
}

void eotd_to_text (unsigned char *eotd, int eotd_len, char *text, int text_size)
{
	assert (eotd_len == EOTD_LENGTH + 1);

	uint64_t pkt = 0ULL;

	for (int i = 0; i < EOTD_LENGTH; i++) {
		pkt <<= 8;
		pkt |= eotd[i];
	}

	*text = '\0';

	char eotd_type = eotd[EOTD_LENGTH];

#ifndef EOTD_RAW
	if (eotd_type == EOTD_TYPE_F2R) {
		strlcat(text, "FRONT>REAR:", text_size);
	} else {
		strlcat(text, "REAR>FRONT:", text_size);
	}
		
#ifdef EOTD_TIMESTAMP
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm *now = localtime(&tv.tv_sec);
	char date_buffer[32];
	strlcat(text, "ts=", text_size);
	sprintf(date_buffer, "%d-%02d-%02dT%02d:%02d:%02d.%03d,",
		now->tm_year + 1900, now->tm_mon + 1, now->tm_mday,
		now->tm_hour, now->tm_min, now->tm_sec, tv.tv_usec / 1000);
	strlcat(text, date_buffer, text_size);
#endif

	if (eotd_type == EOTD_TYPE_R2F) {
		decode_basic_r2f(pkt, text, text_size);
	} else {
		decode_basic_f2r(pkt, text, text_size);
	}

#ifdef EOTD_APPEND_HEX
	char hex[64];
	add_comma(text, text_size);
	snprintf(hex, sizeof(hex), "%llx", pkt);
	strlcat(text, "hex=", text_size);
	for (int i = 56; i >= 0; i -= 8) {
		sprintf(hex, "%02x ", (unsigned char) (pkt >> i) & 0xff);
		strlcat(text, hex, text_size);
	}
	text[strlen(text) - 1] = '\0'; // zap trailing space
#endif
#else
	char temp[8];
	for (int i = 0; i < 8; i++) {
		sprintf(temp, " %02x", eotd[i]);
		strlcat(text, temp, text_size);
	}
#endif
}
