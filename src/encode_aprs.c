
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014  John Langner, WB2OSZ
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

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <assert.h>

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
 *		ambiguity - Blank out least significant digits.
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


static int set_norm_position (char symtab, char symbol, double dlat, double dlong, int ambiguity, position_t *presult)
{
	// An over zealous compiler might complain about l*itude_to_str writing
	// N characters plus nul to an N character field so we stick it into a
	// larger temp then copy the desired number of bytes.  (Issue 296)

	char stemp[16];

	latitude_to_str (dlat, ambiguity, stemp);
	memcpy (presult->lat, stemp, sizeof(presult->lat));

	if (symtab != '/' && symtab != '\\' && ! isdigit(symtab) && ! isupper(symtab)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Symbol table identifier is not one of / \\ 0-9 A-Z\n");
	}
	presult->sym_table_id = symtab;

	longitude_to_str (dlong, ambiguity, stemp);
	memcpy (presult->lon, stemp, sizeof(presult->lon));

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
 * 		course	- Degrees, 0 - 360 (360 equiv. to 0).
 *			  Use G_UNKNOWN for none or unknown.
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
 *		Some conversion must be performed for course from
 *		the API definition to what is sent over the air.
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

	if (speed > 0) {
	  int c;
	  int s;
	  
	  if (course != G_UNKNOWN) {
	    c = (course + 2) / 4;
	    if (c < 0) c += 90;
	    if (c >= 90) c -= 90;
	  }
	  else {
	    c = 0;
	  }
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
 *		gain	- dB.  Protocol spec doesn't mention whether it is dBi or dBd.
 *				This says dBi:
 *				http://www.tapr.org/pipermail/aprssig/2008-September/027034.html

 *		dir	- Directivity: N, NE, etc., omni.
 *
 * Outputs:	presult	- Stored here.  
 *
 * Returns:     Number of characters in result.
 *
 *----------------------------------------------------------------*/

// TODO (bug):  Doesn't check for G_UNKNOWN.
// could have a case where some, but not all, values were specified.
// Callers originally checked for any not zero. 
// now they check for any > 0.


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
 * Inputs: 	course	- Degrees, 0 - 360 (360 equiv. to 0).
 *			  Use G_UNKNOWN for none or unknown.
 *
 *		speed	- knots.
 *
 * Outputs:	presult	- Stored here.  
 *
 * Returns:     Number of characters in result.
 *
 * Description: Over the air we use:
 *			0 	for unknown or not relevant.
 *			1 - 360	for valid course.  (360 for north)
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

	if (course != G_UNKNOWN) {
	  x = course;
	  while (x < 1) x += 360;
	  while (x > 360) x -= 360;
	  // Should now be in range of 1 - 360. */
	  // Original value of 0 for north is transmitted as 360. */
	}
	else {
	  x = 0;
	}
	snprintf (stemp, sizeof(stemp), "%03d", x);
	memcpy (r->cse, stemp, 3);

	r->slash = '/';

	x = speed;
	if (x < 0) x = 0;		// would include G_UNKNOWN
	if (x > 999) x = 999;
	snprintf (stemp, sizeof(stemp), "%03d", x);
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
 *		Resulting formats are all fixed width and have a trailing space:
 *
 *			"999.999MHz "
 *			"T999 "
 *			"+999 "			(10 kHz units)
 *
 * Reference:	http://www.aprs.org/info/freqspec.txt
 *
 *----------------------------------------------------------------*/


static int frequency_spec (float freq, float tone, float offset, char *presult)
{
	int result_size = 24;		// TODO: add as parameter.

	*presult = '\0';
	
	if (freq > 0) {
	  char stemp[16];

	  /* TODO: Should use letters for > 999.999. */
	  /* For now, just be sure we have proper field width. */

	  if (freq > 999.999) freq = 999.999;

	  snprintf (stemp, sizeof(stemp), "%07.3fMHz ", freq);

	  strlcpy (presult, stemp, result_size);
	}

	if (tone != G_UNKNOWN) {
	  char stemp[12];

	  if (tone == 0) {
	    strlcpy (stemp, "Toff ", sizeof (stemp));
	  }
	  else {
	    snprintf (stemp, sizeof(stemp), "T%03d ", (int)tone);
	  }

	  strlcat (presult, stemp, result_size);
	}

	if (offset != G_UNKNOWN) {
	  char stemp[12];

	  snprintf (stemp, sizeof(stemp), "%+04d ", (int)round(offset * 100));
	  strlcat (presult, stemp, result_size);
	}

	return (strlen(presult));
}


/*------------------------------------------------------------------
 *
 * Name:        encode_position
 *
 * Purpose:     Construct info part for position report format.
 *
 * Inputs:      messaging - This determines whether the data type indicator 
 *			   is set to '!' (false) or '=' (true).
 *		compressed - Send in compressed form?
 *		lat	- Latitude.
 *		lon	- Longitude.
 *		ambiguity - Number of digits to omit from location.
 *		alt_ft	- Altitude in feet.
 *		symtab	- Symbol table id or overlay.
 *		symbol	- Symbol id.
 *
 * 	 	power	- Watts.
 *		height	- Feet.
 *		gain	- dB.  Not clear if it is dBi or dBd.
 *		dir	- Directivity: N, NE, etc., omni.
 *
 *		course	- Degrees, 0 - 360 (360 equiv. to 0).
 *			  Use G_UNKNOWN for none or unknown.
 *		speed	- knots.		// TODO:  should distinguish unknown(not revevant) vs. known zero.
 *
 * 	 	freq	- MHz.
 *		tone	- Hz.
 *		offset	- MHz.
 *
 *		comment	- Additional comment text.
 *
 *		result_size - Amount of space for result, provided by
 *				caller, to avoid buffer overflow.
 *
 * Outputs:	presult	- Stored here.  Should be at least ??? bytes.
 *				Could get into hundreds of characters
 *				because it includes the comment.
 *
 * Returns:     Number of characters in result.
 *
 * Description:	There can be a single optional "data extension"
 *		following the position so there is a choice
 *		between:
 *			Power/height/gain/directivity or
 *			Course/speed.
 *
 *		After that,
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


int encode_position (int messaging, int compressed, double lat, double lon, int ambiguity, int alt_ft, 
		char symtab, char symbol, 
		int power, int height, int gain, char *dir,
		int course, int speed,
		float freq, float tone, float offset,
		char *comment,
		char *presult, size_t result_size)
{
	int result_len = 0;

	if (compressed) {

// Thought:
// https://groups.io/g/direwolf/topic/92718535#6886
// When speed is zero, we could put the altitude in the compressed
// position rather than having /A=999999.
// However, the resolution would be decreased and that could be important
// when hiking in hilly terrain.  It would also be confusing to
// flip back and forth between two different representations.

	  aprs_compressed_pos_t *p = (aprs_compressed_pos_t *)presult;

	  p->dti = messaging ? '=' : '!';
	  set_comp_position (symtab, symbol, lat, lon, 
		power, height, gain, 
		course, speed,
		&(p->cpos));
	  result_len = 1 + sizeof (p->cpos);
	}
	else {
	  aprs_ll_pos_t *p = (aprs_ll_pos_t *)presult;

	  p->dti = messaging ? '=' : '!';
	  set_norm_position (symtab, symbol, lat, lon, ambiguity, &(p->pos));
	  result_len = 1 + sizeof (p->pos);

/* Optional data extension. (singular) */
/* Can't have both course/speed and PHG.  Former gets priority. */

	  if (course != G_UNKNOWN || speed > 0) {
	    result_len += cse_spd_data_extension (course, speed, presult + result_len);
	  }
	  else if (power > 0 || height > 0 || gain > 0) {
 	    result_len += phg_data_extension (power, height, gain, dir, presult + result_len);
	  }
	}

/* Optional frequency spec. */

	if (freq != 0 || tone != 0 || offset != 0) {
	  result_len += frequency_spec (freq, tone, offset, presult + result_len);
	}

	presult[result_len] = '\0';

/* Altitude.  Can be anywhere in comment. */

	if (alt_ft != G_UNKNOWN) {
	  char salt[12];
	  /* Not clear if altitude can be negative. */
	  /* Be sure it will be converted to 6 digits. */
	  if (alt_ft < 0) alt_ft = 0;
	  if (alt_ft > 999999) alt_ft = 999999;
	  snprintf (salt, sizeof(salt), "/A=%06d", alt_ft);
	  strlcat (presult, salt, result_size);
	  result_len += strlen(salt);
	}

/* Finally, comment text. */
	
	if (comment != NULL) {
	  strlcat (presult, comment, result_size);
	  result_len += strlen(comment);
	}

	if (result_len >= (int)result_size) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("encode_position result of %d characters won't fit into space provided.\n", result_len);
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
 *		ambiguity - Number of digits to omit from location.
 *		symtab	- Symbol table id or overlay.
 *		symbol	- Symbol id.
 *
 * 	 	power	- Watts.
 *		height	- Feet.
 *		gain	- dB.  Not clear if it is dBi or dBd.
 *		dir	- Direction: N, NE, etc., omni.
 *
 *		course	- Degrees, 0 - 360 (360 equiv. to 0).
 *			  Use G_UNKNOWN for none or unknown.
 *		speed	- knots.
 *
 * 	 	freq	- MHz.
 *		tone	- Hz.
 *		offset	- MHz.
 *
 *		comment	- Additional comment text.
 *
 *		result_size - Amount of space for result, provided by
 *				caller, to avoid buffer overflow.
 *
 * Outputs:	presult	- Stored here.  Should be at least ??? bytes.
 *				36 for fixed part,
 *				7 for optional extended data,
 *				~20 for freq, etc.,
 *				comment could be very long...
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

int encode_object (char *name, int compressed, time_t thyme, double lat, double lon, int ambiguity,
		char symtab, char symbol, 
		int power, int height, int gain, char *dir,
		int course, int speed,
		float freq, float tone, float offset, char *comment,
		char *presult, size_t result_size)
{
	aprs_object_t *p = (aprs_object_t *) presult;
	int result_len = 0;
	int n;


	p->o.dti = ';';

	memset (p->o.name, ' ', sizeof(p->o.name));
	n = strlen(name);
	if (n > (int)(sizeof(p->o.name))) n = sizeof(p->o.name);
	memcpy (p->o.name, name, n);

	p->o.live_killed = '*';

	if (thyme != 0) {
	  struct tm tm;

#define XMIT_UTC 1
#if XMIT_UTC
	  (void)gmtime_r (&thyme, &tm);
#else
	  /* Using local time, for this application, would make more sense to me. */
	  /* On Windows, localtime_r produces UTC. */
	  /* How do we set the time zone?  Google for mingw time zone. */

	  localtime_r (thyme, &tm);
#endif
	  snprintf (p->o.time_stamp, sizeof(p->o.time_stamp), "%02d%02d%02d", tm.tm_mday, tm.tm_hour, tm.tm_min);
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
	  set_norm_position (symtab, symbol, lat, lon, ambiguity, &(p->u.pos));
	  result_len = sizeof(p->o) + sizeof (p->u.pos);

/* Optional data extension. (singular) */
/* Can't have both course/speed and PHG.  Former gets priority. */

	  if (course != G_UNKNOWN || speed > 0) {
	    result_len += cse_spd_data_extension (course, speed, presult + result_len);
	  }
	  else if (power > 0 || height > 0 || gain > 0) {
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
	  strlcat (presult, comment, result_size);
	  result_len += strlen(comment);
	}

	if (result_len >= (int)result_size) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("encode_object result of %d characters won't fit into space provided.\n", result_len);
	}

	return (result_len);

} /* end encode_object */



/*------------------------------------------------------------------
 *
 * Name:        encode_message
 *
 * Purpose:     Construct info part for APRS "message" format.
 *
 * Inputs:      addressee	- Addressed to, up to 9 characters.
 *		text		- Text part of the message.
 *		id		- Identifier, 0 to 5 characters.
 *		result_size 	- Amount of space for result, provided by
 *				  caller, to avoid buffer overflow.
 *
 * Outputs:	presult	- Stored here.
 *
 * Returns:     Number of characters in result.
 *
 * Description:	
 *
 *----------------------------------------------------------------*/


typedef struct aprs_message_s {
	    char dti;			/* : Data Type Indicator */					
	    char addressee[9];		/*   Fixed width 9 characters. */
	    char sep;			/* : separator */	
	    char text;
	} aprs_message_t;

int encode_message (char *addressee, char *text, char *id, char *presult, size_t result_size)
{
	aprs_message_t *p = (aprs_message_t *) presult;
	int n;

	p->dti = ':';

	memset (p->addressee, ' ', sizeof(p->addressee));
	n = strlen(addressee);
	if (n > (int)(sizeof(p->addressee))) n = sizeof(p->addressee);
	memcpy (p->addressee, addressee, n);

	p->sep = ':';
	p->text = '\0';

	strlcat (presult, text, result_size);
	if (strlen(id) > 0) {
	  strlcat (presult, "{", result_size);
	  strlcat (presult, id, result_size);
	}

	return (strlen(presult));

} /* end encode_message */




/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Quick test for some functions in this file.
 *
 * Description:	Just a smattering, not an organized test.
 *
 * 		$ rm a.exe ; gcc -DEN_MAIN encode_aprs.c latlong.c textcolor.c misc.a ; ./a.exe
 *
 *----------------------------------------------------------------*/


#if EN_MAIN


int main (int argc, char *argv[])
{
	char result[100];
	int errors = 0;


/***********  Position  ***********/

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		0, 0, 0, NULL, G_UNKNOWN, 0, 0, 0, 0, NULL, result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

/* with PHG. */
// TODO:  Need to test specifying some but not all.

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		50, 100, 6, "N", G_UNKNOWN, 0, 0, 0, 0, NULL, result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&PHG7368") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

/* with freq & tone.  minus offset, no offset, explicit simplex. */

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		0, 0, 0, NULL, G_UNKNOWN, 0, 146.955, 74.4, -0.6, NULL, result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&146.955MHz T074 -060 ") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		0, 0, 0, NULL, G_UNKNOWN, 0, 146.955, 74.4, G_UNKNOWN, NULL, result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&146.955MHz T074 ") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		0, 0, 0, NULL, G_UNKNOWN, 0, 146.955, 74.4, 0, NULL, result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&146.955MHz T074 +000 ") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

/* with course/speed, freq, and comment! */

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		0, 0, 0, NULL, 180, 55, 146.955, 74.4, -0.6, "River flooding", result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&180/055146.955MHz T074 -060 River flooding") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

/* Course speed, no tone, + offset */

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		0, 0, 0, NULL, 180, 55, 146.955, G_UNKNOWN, 0.6, "River flooding", result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&180/055146.955MHz +060 River flooding") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

/* Course speed, no tone, + offset + altitude */

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, 12345, 'D', '&',
		0, 0, 0, NULL, 180, 55, 146.955, G_UNKNOWN, 0.6, "River flooding", result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&180/055146.955MHz +060 /A=012345River flooding") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

	encode_position (0, 0, 42+34.61/60, -(71+26.47/60), 0, 12345, 'D', '&',
		0, 0, 0, NULL, 180, 55, 146.955, 0, 0.6, "River flooding", result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!4234.61ND07126.47W&180/055146.955MHz Toff +060 /A=012345River flooding") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

// TODO: try boundary conditions of course = 0, 359, 360

/*********** Compressed position. ***********/

	encode_position (0, 1, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		0, 0, 0, NULL, G_UNKNOWN, 0, 0, 0, 0, NULL, result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!D8yKC<Hn[&  !") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }


/* with PHG. In this case it is converted to precomputed radio range.  TODO: check on this.  Is 27.4 correct? */

	encode_position (0, 1, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		50, 100, 6, "N", G_UNKNOWN, 0, 0, 0, 0, NULL, result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!D8yKC<Hn[&{CG") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

/* with course/speed, freq, and comment!  Roundoff. 55 knots should be 63 MPH.  we get 62. */

	encode_position (0, 1, 42+34.61/60, -(71+26.47/60), 0, G_UNKNOWN, 'D', '&',
		0, 0, 0, NULL, 180, 55, 146.955, 74.4, -0.6, "River flooding", result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, "!D8yKC<Hn[&NUG146.955MHz T074 -060 River flooding") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

// TODO:  test alt; cs+alt


/*********** Object. ***********/

	encode_object ("WB1GOF-C", 0, 0, 42+34.61/60, -(71+26.47/60), 0, 'D', '&',
		0, 0, 0, NULL, G_UNKNOWN, 0, 0, 0, 0, NULL, result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, ";WB1GOF-C *111111z4234.61ND07126.47W&") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }

// TODO: need more tests.

	if (errors > 0) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Encode APRS test FAILED with %d errors.\n", errors);
	  exit (EXIT_FAILURE);
	}


/*********** Message. ***********/


	encode_message ("N2GH", "some stuff", "", result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, ":N2GH     :some stuff") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }


	encode_message ("N2GH", "other stuff", "12345", result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, ":N2GH     :other stuff{12345") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }


	encode_message ("WB2OSZ-123", "other stuff", "12345", result, sizeof(result));
	dw_printf ("%s\n", result);
	if (strcmp(result, ":WB2OSZ-12:other stuff{12345") != 0) { dw_printf ("ERROR!  line %d\n", __LINE__); errors++; }


	if (errors != 0) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Encode APRS test FAILED with %d errors.\n", errors);
	  exit (EXIT_FAILURE);
	}

	text_color_set (DW_COLOR_REC);
	dw_printf ("Encode APRS test PASSED with no errors.\n");
	exit (EXIT_SUCCESS);
	

}  /* end main */

#endif		/* unit test */


/* end encode_aprs.c */

