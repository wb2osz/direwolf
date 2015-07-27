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
 * Module:      encode_aprs.c
 *
 * Purpose:   	Construct APRS packets from components.
 *		
 * Description: 
 *
 * References:	APRS Protocol Reference.
 *
 *		Frequency spec.
 *		http://www.aprs.org/info/freqspec.txt
 *
 *---------------------------------------------------------------*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <assert.h>

#include "direwolf.h"
#include "encode_aprs.h"
#include "latlong.h"
#include "textcolor.h"



/*------------------------------------------------------------------
 *
 * Name:        set_norm_position
 *
 * Purpose:     Fill in the human-readable latitude, longitude, 
 * 		symbol part which is common to multiple data formats.
 *
 * Inputs: 	symtab	- Symbol table id or overlay.
 *		symbol	- Symbol id.
 *    		dlat	- Latitude.
 *		dlong	- Longitude.
 *
 * Outputs:	presult	- Stored here.  
 *
 * Returns:     Number of characters in result.
 *
 *----------------------------------------------------------------*/

/* Position & symbol fields common to several message formats. */

typedef struct position_s {
	  char lat[8];
	  char sym_table_id;		/* / \ 0-9 A-Z */
	  char lon[9];
	  char symbol_code;
	} position_t;


static int set_norm_position (char symtab, char symbol, double dlat, double dlong, position_t *presult)
{

	latitude_to_str (dlat, 0, presult->lat);

	if (symtab != '/' && symtab != '\\' && ! isdigit(symtab) && ! isupper(symtab)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Symbol table identifier is not one of / \\ 0-9 A-Z\n");
	}
	presult->sym_table_id = symtab;

	longitude_to_str (dlong, 0, presult->lon);

	if (symbol < '!' || symbol > '~') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Symbol code is not in range of ! to ~\n");
	}
	presult->symbol_code = symbol;

	return (sizeof(position_t));
}


/*------------------------------------------------------------------
 *
 * Name:        set_comp_position
 *
 * Purpose:     Fill in the compressed latitude, longitude, 
 *		symbol part which is common to multiple data formats.
 *
 * Inputs: 	symtab	- Symbol table id or overlay.
 *		symbol	- Symbol id.
 *    		dlat	- Latitude.
 *		dlong	- Longitude.
 *
 * 	 	power	- Watts.
 *		height	- Feet.
 *		gain	- dBi.
 *
 * 		course	- Degress, 1 - 360.  0 means none or unknown.
 *		speed	- knots.
 *
 *
 * Outputs:	presult	- Stored here.  
 *
 * Returns:     Number of characters in result.
 *
 * Description:	The cst field can have only one of 
 *
 *		course/speed	- takes priority (this implementation)
 *		radio range	- calculated from PHG
 *		altitude	- not implemented yet.
 *
 *----------------------------------------------------------------*/

/* Compressed position & symbol fields common to several message formats. */

typedef struct compressed_position_s {
	  char sym_table_id;		/* / \ a-j A-Z */
					/* "The presence of the leading Symbol Table Identifier */
					/* instead of a digit indicates that this is a compressed */
					/* Position Report and not a normal lat/long report." */

	  char y[4];			/* Compressed Latitude. */
	  char x[4];			/* Compressed Longitude. */
	  char symbol_code;
	  char c;			/* Course/speed or radio range or altitude. */
	  char s;
	  char t	;		/* Compression type. */
	} compressed_position_t;


static int set_comp_position (char symtab, char symbol, double dlat, double dlong, 
		int power, int height, int gain, 
		int course, int speed,
		compressed_position_t *presult)
{

	if (symtab != '/' && symtab != '\\' && ! isdigit(symtab) && ! isupper(symtab)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Symbol table identifier is not one of / \\ 0-9 A-Z\n");
	}

/*
 * In compressed format, the characters a-j are used for a numeric overlay.
 * This allows the receiver to distinguish between compressed and normal formats.
 */
	if (isdigit(symtab)) {
	  symtab = symtab - '0' + 'a';
	}
	presult->sym_table_id = symtab;

	latitude_to_comp_str (dlat, presult->y);
	longitude_to_comp_str (dlong, presult->x);

	if (symbol < '!' || symbol > '~') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Symbol code is not in range of ! to ~\n");
	}
	presult->symbol_code = symbol;

/*
 * The cst field is complicated.
 *
 * When c is ' ', the cst field is not used.
 *
 * When the t byte has a certain pattern, c & s represent altitude.
 *
 * Otherwise, c & s can be either course/speed or radio range.
 *
 * When c is in range of '!' to 'z', 
 *
 * 	('!' - 33) * 4 = 0 degrees.
 *	...
 *	('z' - 33) * 4 = 356 degrees.
 *
 * In this case, s represents speed ...
 *
 * When c is '{', s is range ...
 */

	if (course || speed) {
	  int c;
	  int s;

	  c = (course + 1) / 4;
	  if (c < 0) c += 90;
	  if (c >= 90) c -= 90;
	  presult->c = c + '!';

	  s = (int)round(log(speed+1.0) / log(1.08));
	  presult->s = s + '!';
	   
	  presult->t = 0x26 + '!';	/* current, other tracker. */
	}
	else if (power || height || gain) {
	  int s;
	  float range;

	  presult->c = '{';		/* radio range. */

	  if (power == 0) power = 10;
	  if (height == 0) height = 20;
	  if (gain == 0) gain = 3;

	  // from protocol reference page 29.
	  range = sqrt(2.0*height * sqrt((power/10.0) * (gain/2.0)));

	  s = (int)round(log(range/2.) / log(1.08));
	  if (s < 0) s = 0;
	  if (s > 93) s = 93;

	  presult->s = s + '!';
   
	  presult->t = 0x26 + '!';	/* current, other tracker. */  
	}
	else {
	  presult->c = ' ';		/* cst field not used. */
	  presult->s = ' ';
	  presult->t = '!';		/* avoid space. */  
	}
	return (sizeof(compressed_position_t));
}



/*------------------------------------------------------------------
 *
 * Name:        phg_data_extension
 *
 * Purpose:     Fill in parts of the power/height/gain data extension.
 *
 * Inputs: 	power	- Watts.
 *		height	- Feet.
 *		gain	- dB.  Not clear if it is dBi or dBd.
 *		dir	- Directivity: N, NE, etc., omni.
 *
 * Outputs:	presult	- Stored here.  
 *
 * Returns:     Number of characters in result.
 *
 *----------------------------------------------------------------*/


typedef struct phg_s {
	  char P;
	  char H;
	  char G;
	  char p;
	  char h;
	  char g;
	  char d;
	} phg_t;


static int phg_data_extension (int power, int height, int gain, char *dir, char *presult)
{
	phg_t *r = (phg_t*)presult;
	int x;

	r->P = 'P';
	r->H = 'H';
	r->G = 'G';
		
	x = (int)round(sqrt((float)power)) + '0';
	if (x < '0') x = '0';
	else if (x > '9') x = '9';
	r->p = x;

	x = (int)round(log2(height/10.0)) + '0';
	if (x < '0') x = '0';
	/* Result can go beyond '9'. */
	r->h = x;

	x = gain + '0';
	if (x < '0') x = '0';
	else if (x > '9') x = '0';
	r->g = x;

	r->d = '0';
	if (dir != NULL) {
	  if (strcasecmp(dir,"NE") == 0) r->d = '1';
	  if (strcasecmp(dir,"E") == 0) r->d = '2';
	  if (strcasecmp(dir,"SE") == 0) r->d = '3';
	  if (strcasecmp(dir,"S") == 0) r->d = '4';
	  if (strcasecmp(dir,"SW") == 0) r->d = '5';
	  if (strcasecmp(dir,"W") == 0) r->d = '6';
	  if (strcasecmp(dir,"NW") == 0) r->d = '7';
	  if (strcasecmp(dir,"N") == 0) r->d = '8';
	}
	return (sizeof(phg_t));
}


/*------------------------------------------------------------------
 *
 * Name:        cse_spd_data_extension
 *
 * Purpose:     Fill in parts of the course & speed data extension.
 *
 * Inputs: 	course	- Degress, 1 - 360.
 *		speed	- knots.
 *
 * Outputs:	presult	- Stored here.  
 *
 * Returns:     Number of characters in result.
 *
 *----------------------------------------------------------------*/


typedef struct cs_s {
	  char cse[3];
	  char slash;
	  char spd[3];
	} cs_t;


static int cse_spd_data_extension (int course, int speed, char *presult)
{
	cs_t *r = (cs_t*)presult;
	char stemp[8];
	int x;

	x = course;
	if (x < 0) x = 0;
	if (x > 360) x = 360;
	sprintf (stemp, "%03d", x);
	memcpy (r->cse, stemp, 3);

	r->slash = '/';

	x = speed;
	if (x < 0) x = 0;
	if (x > 999) x = 999;
	sprintf (stemp, "%03d", x);
	memcpy (r->spd, stemp, 3);

	return (sizeof(cs_t));
}



/*------------------------------------------------------------------
 *
 * Name:        frequency_spec
 *
 * Purpose:     Put frequency specification in beginning of comment field.
 *
 * Inputs: 	freq	- MHz.
 *		tone	- Hz.
 *		offset	- MHz.
 *
 * Outputs:	presult	- Stored here.  
 *
 * Returns:     Number of characters in result.
 *
 * Description:	There are several valid variations.
 *
 *		The frequency could be missing here if it is in the 
 *		object name.  In this case we could have tone & offset.
 *
 *		Offset must always be preceded by tone.
 *
 *----------------------------------------------------------------*/


typedef struct freq_s {
	  char f[7];		/* format 999.999 */
	  char mhz[3];
	  char space;
	} freq_t;

typedef struct to_s {
	  char T;
	  char ttt[3];		/* format 999 (drop fraction) or 'off'. */
	  char space1;
	  char oooo[4];		/* leading sign, 3 digits, tens of KHz. */
	  char space2;
	} to_t;


static int frequency_spec (float freq, float tone, float offset, char *presult)
{
	int result_len = 0;
	
	if (freq != 0) {
	  freq_t *f = (freq_t*)presult;
	  char stemp[12];

	  /* Should use letters for > 999.999. */
	  sprintf (stemp, "%07.3f", freq);
	  memcpy (f->f, stemp, 7);
	  memcpy (f->mhz, "MHz", 3);
	  f->space = ' ';
	  result_len = sizeof (freq_t);
	}
	
	if (tone != 0 || offset != 0) {
	  to_t *to = (to_t*)(presult + result_len);
	  char stemp[12];

	  to->T = 'T';
	  if (tone == 0) {
	    memcpy(to->ttt, "off", 3);
	  }
	  else {
	    sprintf (stemp, "%03d", (int)tone);
	    memcpy (to->ttt, stemp, 3);
	  }
	  to->space1 = ' ';
	  sprintf (stemp, "%+04d", (int)round(offset * 100));
	  memcpy (to->oooo, stemp, 4);
	  to->space2 = ' ';

	  result_len += sizeof (to_t);
	}

	return (result_len);
}


/*------------------------------------------------------------------
 *
 * Name:        encode_position
 *
 * Purpose:     Construct info part for position report format.
 *
 * Inputs:      compressed - Send in compressed form?
 *		lat	- Latitude.
 *		lon	- Longitude.
 *		symtab	- Symbol table id or overlay.
 *		symbol	- Symbol id.
 *
 * 	 	power	- Watts.
 *		height	- Feet.
 *		gain	- dB.  Not clear if it is dBi or dBd.
 *		dir	- Directivity: N, NE, etc., omni.
 *
 * 		course	- Degress, 1 - 360.  0 means none or unknown.
 *		speed	- knots.
 *
 * 	 	freq	- MHz.
 *		tone	- Hz.
 *		offset	- MHz.
 *
 *		comment	- Additional comment text.
 *
 *
 * Outputs:	presult	- Stored here.  Should be at least ??? bytes.
 *
 * Returns:     Number of characters in result.
 *
 * Description:	There can be a single optional "data extension"
 *		following the position so there is a choice
 *		between:
 *			Power/height/gain/directivity or
 *			Course/speed.
 *
 *		Afer that, 
 *
 *----------------------------------------------------------------*/

typedef struct aprs_ll_pos_s {
	  char dti;			/* ! or = */
	  position_t pos;
	  				/* Comment up to 43 characters. */
					/* Start of comment could be data extension(s). */
} aprs_ll_pos_t;


typedef struct aprs_compressed_pos_s {
	  char dti;			/* ! or = */
	  compressed_position_t cpos;
	  				/* Comment up to 40 characters. */
					/* No data extension allowed for compressed location. */
} aprs_compressed_pos_t;


int encode_position (int compressed, double lat, double lon, 
		char symtab, char symbol, 
		int power, int height, int gain, char *dir,
		int course, int speed,
		float freq, float tone, float offset,
		char *comment,
		char *presult)
{
	int result_len = 0;

	if (compressed) {
	  aprs_compressed_pos_t *p = (aprs_compressed_pos_t *)presult;

	  p->dti = '!';
	  set_comp_position (symtab, symbol, lat, lon, 
		power, height, gain, 
		course, speed,
		&(p->cpos));
	  result_len = 1 + sizeof (p->cpos);
	}
	else {
	  aprs_ll_pos_t *p = (aprs_ll_pos_t *)presult;

	  p->dti = '!';
	  set_norm_position (symtab, symbol, lat, lon, &(p->pos));
	  result_len = 1 + sizeof (p->pos);

/* Optional data extension. (singular) */
/* Can't have both course/speed and PHG.  Former gets priority. */

	  if (course || speed) {
	    result_len += cse_spd_data_extension (course, speed, presult + result_len);
	  }
	  else if (power || height || gain) {
 	    result_len += phg_data_extension (power, height, gain, dir, presult + result_len);
	  }
	}

/* Optional frequency spec. */

	if (freq != 0 || tone != 0 || offset != 0) {
	  result_len += frequency_spec (freq, tone, offset, presult + result_len);
	}

	presult[result_len] = '\0';

/* Finally, comment text. */
	
	if (comment != NULL) {
	  strcat (presult, comment);
	  result_len += strlen(comment);
	}

	return (result_len);

} /* end encode_position */


/*------------------------------------------------------------------
 *
 * Name:        encode_object
 *
 * Purpose:     Construct info part for object report format.
 *
 * Inputs:      name	- Name, up to 9 characters.
 *		compressed - Send in compressed form?
 *		thyme	- Time stamp or 0 for none.
 *		lat	- Latitude.
 *		lon	- Longitude.
 *		symtab	- Symbol table id or overlay.
 *		symbol	- Symbol id.
 *
 * 	 	power	- Watts.
 *		height	- Feet.
 *		gain	- dB.  Not clear if it is dBi or dBd.
 *		dir	- Direction: N, NE, etc., omni.
 *
 * 		course	- Degress, 1 - 360.  0 means none or unknown.
 *		speed	- knots.
 *
 * 	 	freq	- MHz.
 *		tone	- Hz.
 *		offset	- MHz.
 *
 *		comment	- Additional comment text.
 *
 * Outputs:	presult	- Stored here.  Should be at least ??? bytes.
 *
 * Returns:     Number of characters in result.
 *
 * Description:	
 *
 *----------------------------------------------------------------*/

typedef struct aprs_object_s {
	  struct {
	    char dti;			/* ; */					
	    char name[9];								
	    char live_killed;		/* * for live or _ for killed */	
	    char time_stamp[7];	  
	  } o;
	  union {
	    position_t pos;		/* Up to 43 char comment.  First 7 bytes could be data extension. */
	    compressed_position_t cpos;	/* Up to 40 char comment.  No PHG data extension in this case. */
	  } u;    
	} aprs_object_t;

int encode_object (char *name, int compressed, time_t thyme, double lat, double lon, 
		char symtab, char symbol, 
		int power, int height, int gain, char *dir,
		int course, int speed,
		float freq, float tone, float offset, char *comment,
		char *presult)
{
	aprs_object_t *p = (aprs_object_t *) presult;
	int result_len = 0;
	int n;


	p->o.dti = ';';

	memset (p->o.name, ' ', sizeof(p->o.name));
	n = strlen(name);
	if (n > sizeof(p->o.name)) n = sizeof(p->o.name);
	memcpy (p->o.name, name, n);

	p->o.live_killed = '*';

	if (thyme != 0) {
	  struct tm tm;

#define XMIT_UTC 1
#if XMIT_UTC
	  gmtime_r (&thyme, &tm);
#else
	  /* Using local time, for this application, would make more sense to me. */
	  /* On Windows, localtime_r produces UTC. */
	  /* How do we set the time zone?  Google for mingw time zone. */

	  localtime_r (thyme, &tm);
#endif
	  sprintf (p->o.time_stamp, "%02d%02d%02d", tm.tm_mday, tm.tm_hour, tm.tm_min);
#if XMIT_UTC
	  p->o.time_stamp[6] = 'z';
#else
	  p->o.time_stamp[6] = '/';
#endif
	}
	else {
	  memcpy (p->o.time_stamp, "111111z", sizeof(p->o.time_stamp));
	}

	if (compressed) {
	  set_comp_position (symtab, symbol, lat, lon, 
		power, height, gain, 
		course, speed,
		&(p->u.cpos));
	  result_len = sizeof(p->o) + sizeof (p->u.cpos);
	}
	else {
	  set_norm_position (symtab, symbol, lat, lon, &(p->u.pos));
	  result_len = sizeof(p->o) + sizeof (p->u.pos);

/* Optional data extension. (singular) */
/* Can't have both course/speed and PHG.  Former gets priority. */

	  if (course || speed) {
	    result_len += cse_spd_data_extension (course, speed, presult + result_len);
	  }
	  else if (power || height || gain) {
 	    result_len += phg_data_extension (power, height, gain, dir, presult + result_len);
	  }
	}

/* Optional frequency spec. */

	if (freq != 0 || tone != 0 || offset != 0) {
	  result_len += frequency_spec (freq, tone, offset, presult + result_len);
	}

	presult[result_len] = '\0';

/* Finally, comment text. */
	
	if (comment != NULL) {
	  strcat (presult, comment);
	  result_len += strlen(comment);
	}

	return (result_len);

} /* end encode_object */


/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Quick test for some functions in this file.
 *
 * Description:	Just a smattering, not an organized test.
 *
 * 		$ rm a.exe ; gcc -DEN_MAIN encode_aprs.c latlong.c ; ./a.exe
 *
 *----------------------------------------------------------------*/


#if EN_MAIN

void text_color_set ( enum dw_color_e c )
{
        return;
} 

int main (int argc, char *argv[])
{
	char result[100];



/***********  Position  ***********/

	encode_position (0, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		0, 0, 0, NULL, 0, 0, 0, 0, 0, NULL, result);
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&") != 0) dw_printf ("ERROR!\n");

/* with PHG. */

	encode_position (0, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		50, 100, 6, "N", 0, 0, 0, 0, 0, NULL, result);
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&PHG7368") != 0) dw_printf ("ERROR!\n");

/* with freq. */

	encode_position (0, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		0, 0, 0, NULL, 0, 0, 146.955, 74.4, -0.6, NULL, result);
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&146.955MHz T074 -060 ") != 0) dw_printf ("ERROR!\n");

/* with course/speed, freq, and comment! */

	encode_position (0, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		0, 0, 0, NULL, 180, 55, 146.955, 74.4, -0.6, "River flooding", result);
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&180/055146.955MHz T074 -060 River flooding") != 0) dw_printf ("ERROR!\n");

/* Course speed, no tone, + offset */

	encode_position (0, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		0, 0, 0, NULL, 180, 55, 146.955, 0, 0.6, "River flooding", result);
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&180/055146.955MHz Toff +060 River flooding") != 0) dw_printf ("ERROR!\n");





/*********** Compressed position. ***********/

	encode_position (1, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		0, 0, 0, NULL, 0, 0, 0, 0, 0, NULL, result);
	dw_printf ("%s\n", result);
	if (strcmp(result, "!D8yKC<Hn[&   ") != 0) dw_printf ("ERROR!\n");


/* with PHG. In this case it is converted to precomputed radio range. */

	encode_position (0, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		50, 100, 6, "N", 0, 0, 0, 0, 0, NULL, result);
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&PHG7368   TBD ???") != 0) dw_printf ("ERROR!\n");

/* with course/speed, freq, and comment! */

	encode_position (0, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		0, 0, 0, NULL, 180, 55, 146.955, 74.4, -0.6, "River flooding", result);
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&180/055146.955MHz T074 -060 River flooding") != 0) dw_printf ("ERROR!\n");



/*********** Object. ***********/


	encode_object ("WB1GOF-C", 0, 0, 42+34.61/60, -(71+26.47/60), 'D', '&', 
		0, 0, 0, NULL, result);
	dw_printf ("%s\n", result);
	if (strcmp(result, ";WB1GOF-C *111111z4234.61ND07126.47W&   TBD???") != 0) dw_printf ("ERROR!\n");


	return(0);

}  /* end main */

#endif		/* unit test */


/* end encode_aprs.c */

