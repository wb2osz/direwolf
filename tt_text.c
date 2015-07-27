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
 * Module:      tt-text.c
 *
 * Purpose:   	Translate between text and touch tone representation.
 *		
 * Description: Letters can be represented by different touch tone
 *		keypad sequences.
 *
 * References:	This is based upon APRStt (TM) documents but not 100%
 *		compliant due to ambiguities and inconsistencies in
 *		the specifications.
 *
 *		http://www.aprs.org/aprstt.html
 *
 *---------------------------------------------------------------*/

/*
 * There are two different encodings called:
 *
 *   * Two-key
 *
 *		Digits are represented by a single key press.
 *		Letters (or space) are represented by the corresponding
 *		key followed by A, B, C, or D depending on the position
 *		of the letter.
 *
 *   * Multi-press
 *
 *		Letters are represented by one or more key presses
 *		depending on their position.
 *		e.g. on 5/JKL key, J = 1 press, K = 2, etc. 
 *		The digit is the number of letters plus 1.
 *		In this case, press 5 key four times to get digit 5.
 *		When two characters in a row use the same key,
 *		use the "A" key as a separator.
 *
 * Examples:
 *
 *	Character	Multipress	Two Key		Comments
 *	---------	----------	-------		--------
 *	0		00		0		Space is handled like a letter.
 *	1		1		1		No letters on 1 button.
 *	2		2222		2		3 letters -> 4 key presses
 *	9		99999		9
 *	W		9		9A
 *	X		99		9B
 *	Y		999		9C
 *	Z		9999		9D
 *	space		0		0A		0A was used in an APRStt comment example.
 *
 *
 * Note that letters can occur in callsigns and comments.
 * Everywhere else they are simply digits.
 */

/*
 * Everything is based on this table.
 * Changing it will change everything.
 */

static const char translate[10][4] = {
		/*	 A	 B	 C	 D  */
		/*	---	---	---	--- */
	/* 0 */	{	' ',	 0,	 0,	 0  },
	/* 1 */	{	 0,	 0,	 0,	 0  },
	/* 2 */	{	'A',	'B',	'C',	 0  },
	/* 3 */	{	'D',	'E',	'F',	 0  },
	/* 4 */	{	'G',	'H',	'I',	 0  },
	/* 5 */	{	'J',	'K',	'L',	 0  },
	/* 6 */	{	'M',	'N',	'O',	 0  },
	/* 7 */	{	'P',	'Q',	'R',	'S' },
	/* 8 */	{	'T',	'U',	'V',	 0  },
	/* 9 */	{	'W',	'X',	'Y',	'Z' } };

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>

#include "textcolor.h"


#if defined(ENC_MAIN) || defined(DEC_MAIN)

void text_color_set (dw_color_t c) { return; }

int dw_printf (const char *fmt, ...) 
{
	va_list args;
	int len;
	
	va_start (args, fmt);
	len = vprintf (fmt, args);
	va_end (args);
	return (len);
}

#endif


/*------------------------------------------------------------------
 *
 * Name:        tt_text_to_multipress
 *
 * Purpose:     Convert text to the multi-press representation.
 *
 * Inputs:      text	- Input string.
 *			  Should contain only digits, letters, or space.
 *			  All other punctuation is treated as space.
 *
 *		quiet	- True to suppress error messages.
 *	
 * Outputs:	buttons	- Sequence of buttons to press.
 *
 * Returns:     Number of errors detected.
 *
 *----------------------------------------------------------------*/

int tt_text_to_multipress (char *text, int quiet, char *buttons) 
{
	char *t = text;
	char *b = buttons;
	char c;
	int row, col;
	int errors = 0;
	int found;
	int n;

	*b = '\0';
	
	while ((c = *t++) != '\0') {

	  if (isdigit(c)) {
	
/* Count number of other characters assigned to this button. */
/* Press that number plus one more. */

	    n = 1;
	    row = c - '0';
	    for (col=0; col<4; col++) {
	      if (translate[row][col] != 0) {
	        n++;
	      }
	    }
	    if (buttons[0] != '\0' && *(b-1) == row + '0') {
	      *b++ = 'A';
	    }
	    while (n--) {
	      *b++ = row + '0';
	      *b = '\0';
	    }
	  }
	  else {
	    if (isupper(c)) {
	      ;
	    }	  
	    else if (islower(c)) {
	      c = toupper(c);
	    }
	    else if (c != ' ') {
	      errors++;
	      if (! quiet) {
	        text_color_set (DW_COLOR_ERROR);
		dw_printf ("Text to multi-press: Only letters, digits, and space allowed.\n");
	      }
	      c = ' ';
	    }

/* Search for everything else in the translation table. */
/* Press number of times depending on column where found. */

	    found = 0;

	    for (row=0; row<10 && ! found; row++) {
	      for (col=0; col<4 && ! found; col++) {
	        if (c == translate[row][col]) {

/* Stick in 'A' if previous character used same button. */

	          if (buttons[0] != '\0' && *(b-1) == row + '0') {
	            *b++ = 'A';
	          }
	          n = col + 1;
	          while (n--) {
	            *b++ = row + '0';
	            *b = '\0';
	            found = 1;
	          }
	        }
	      }
	    }
	    if (! found) {
	      errors++;
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Text to multi-press: INTERNAL ERROR.  Should not be here.\n");
	    }
	  }
	}
	return (errors);          

} /* end tt_text_to_multipress */


/*------------------------------------------------------------------
 *
 * Name:        tt_text_to_two_key
 *
 * Purpose:     Convert text to the two-key representation.
 *
 * Inputs:      text	- Input string.
 *			  Should contain only digits, letters, or space.
 *			  All other punctuation is treated as space.
 *
 *		quiet	- True to suppress error messages.
 *	
 * Outputs:	buttons	- Sequence of buttons to press.
 *
 * Returns:     Number of errors detected.
 *
 *----------------------------------------------------------------*/

int tt_text_to_two_key (char *text, int quiet, char *buttons) 
{
	char *t = text;
	char *b = buttons;
	char c;
	int row, col;
	int errors = 0;
	int found;


	*b = '\0';
	
	while ((c = *t++) != '\0') {

	  if (isdigit(c)) {
	
/* Digit is single key press. */
	  
	    *b++ = c;
	    *b = '\0';
	  }
	  else {
	    if (isupper(c)) {
	      ;
	    }	  
	    else if (islower(c)) {
	      c = toupper(c);
	    }
	    else if (c != ' ') {
	      errors++;
	      if (! quiet) {
	        text_color_set (DW_COLOR_ERROR);
		dw_printf ("Text to two key: Only letters, digits, and space allowed.\n");
	      }
	      c = ' ';
	    }

/* Search for everything else in the translation table. */

	    found = 0;

	    for (row=0; row<10 && ! found; row++) {
	      for (col=0; col<4 && ! found; col++) {
	        if (c == translate[row][col]) {
		  *b++ = '0' + row;
	          *b++ = 'A' + col;
	          *b = '\0';
	          found = 1;
	        }
	      }
	    }
	    if (! found) {
	      errors++;
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Text to two-key: INTERNAL ERROR.  Should not be here.\n");
	    }
	  }
	}
	return (errors);          

} /* end tt_text_to_two_key */


/*------------------------------------------------------------------
 *
 * Name:        tt_multipress_to_text
 *
 * Purpose:     Convert the multi-press representation to text.
 *
 * Inputs:      buttons	- Input string.
 *			  Should contain only 0123456789A.
 *
 *		quiet	- True to suppress error messages.
 *	
 * Outputs:	text	- Converted to letters, digits, space.
 *
 * Returns:     Number of errors detected.
 *
 *----------------------------------------------------------------*/

int tt_multipress_to_text (char *buttons, int quiet, char *text) 
{
	char *b = buttons;
	char *t = text;
	char c;
	int row, col;
	int errors = 0;
	int maxspan;
	int n;

	*t = '\0';
	
	while ((c = *b++) != '\0') {

	  if (isdigit(c)) {
	
/* Determine max that can occur in a row. */
/* = number of other characters assigned to this button + 1. */

	    maxspan = 1;
	    row = c - '0';
	    for (col=0; col<4; col++) {
	      if (translate[row][col] != 0) {
	        maxspan++;
	      }
	    }

/* Count number of consecutive same digits. */

	    n = 1;
	    while (c == *b) {
	      b++;
	      n++;
	    }

	    if (n < maxspan) {
	      *t++ = translate[row][n-1];
	      *t = '\0';
	    }
	    else if (n == maxspan) {
	      *t++ = c;
	      *t = '\0';
	    }
	    else {
	      errors++;
	      if (! quiet) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("Multi-press to text: Maximum of %d \"%c\" can occur in a row.\n", maxspan, c);
	      }
	      /* Treat like the maximum length. */
	      *t++ = c;
	      *t = '\0';
	    }
	  }
	  else if (c == 'A' || c == 'a') {

/* Separator should occur only if digit before and after are the same. */
	     
	    if (b == buttons + 1 || *b == '\0' || *(b-2) != *b) {
	      errors++;
	      if (! quiet) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("Multi-press to text: \"A\" can occur only between two same digits.\n");
  	      }
	    }
	  }
	  else {

/* Completely unexpected character. */

	    errors++;
	    if (! quiet) {
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Multi-press to text: \"%c\" not allowed.\n", c);
  	    }
	  }
	}
	return (errors);          

} /* end tt_multipress_to_text */


/*------------------------------------------------------------------
 *
 * Name:        tt_two_key_to_text
 *
 * Purpose:     Convert the two key representation to text.
 *
 * Inputs:      buttons	- Input string.
 *			  Should contain only 0123456789ABCD.
 *
 *		quiet	- True to suppress error messages.
 *	
 * Outputs:	text	- Converted to letters, digits, space.
 *
 * Returns:     Number of errors detected.
 *
 *----------------------------------------------------------------*/

int tt_two_key_to_text (char *buttons, int quiet, char *text) 
{
	char *b = buttons;
	char *t = text;
	char c;
	int row, col;
	int errors = 0;
	int n;

	*t = '\0';
	
	while ((c = *b++) != '\0') {

	  if (isdigit(c)) {
	
/* Letter (or space) if followed by ABCD. */
	    
	    row = c - '0';
	    col = -1;

	    if (*b >= 'A' && *b <= 'D') {
	      col = *b++ - 'A';
	    }
	    else if (*b >= 'a' && *b <= 'd') {
	      col = *b++ - 'a';
	    }

	    if (col >= 0) {
	      if (translate[row][col] != 0) {
	        *t++ = translate[row][col];
	        *t = '\0';
	      }
	      else {
		errors++;
	        if (! quiet) {
	          text_color_set (DW_COLOR_ERROR);
	          dw_printf ("Two key to text: Invalid combination \"%c%c\".\n", c, col+'A');
		}
	      }
	    }
	    else {
	      *t++ = c;
	      *t = '\0';
	    }
	  }
	  else if ((c >= 'A' && c <= 'D') || (c >= 'a' && c <= 'd')) {

/* ABCD not expected here. */
	     
	    errors++;
	    if (! quiet) {
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Two-key to text: A, B, C, or D in unexpected location.\n");
	    }
	  }
	  else {

/* Completely unexpected character. */

	    errors++;
	    if (! quiet) {
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Two-key to text: Invalid character \"%c\".\n", c);
  	    }
	  }
	}
	return (errors);          

} /* end tt_two_key_to_text */


/*------------------------------------------------------------------
 *
 * Name:        tt_guess_type
 *
 * Purpose:     Try to guess which encoding we have.
 *
 * Inputs:      buttons	- Input string.
 *			  Should contain only 0123456789ABCD.
 *
 * Returns:     TT_MULTIPRESS	- Looks like multipress.
 *		TT_TWO_KEY	- Looks like two key.
 *		TT_EITHER	- Could be either one.
 *
 *----------------------------------------------------------------*/

typedef enum { TT_EITHER, TT_MULTIPRESS, TT_TWO_KEY } tt_enc_t;

tt_enc_t tt_guess_type (char *buttons) 
{
	char text[256];
	int err_mp;
	int err_tk;
	
/* If it contains B, C, or D, it can't be multipress. */

	if (strchr (buttons, 'B') != NULL || strchr (buttons, 'b') != NULL ||
	    strchr (buttons, 'C') != NULL || strchr (buttons, 'c') != NULL ||
	    strchr (buttons, 'D') != NULL || strchr (buttons, 'd') != NULL) {
	  return (TT_TWO_KEY);
	}

/* Try parsing quietly and see if one gets errors and the other doesn't. */

	err_mp = tt_multipress_to_text (buttons, 1, text);
	err_tk = tt_two_key_to_text (buttons, 1, text);

	if (err_mp == 0 && err_tk > 0) {
	  return (TT_MULTIPRESS);
 	}
	else if (err_tk == 0 && err_mp > 0) {
	  return (TT_TWO_KEY);
	}

/* Could be either one. */

	return (TT_EITHER);

} /* end tt_guess_type */



/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Utility program for testing the encoding.
 *
 *----------------------------------------------------------------*/


#if ENC_MAIN

int checksum (char *tt)
{
	int cs = 10;	/* Assume leading 'A'. */
			/* Doesn't matter due to mod 10 at the end. */
	char *p;

	for (p = tt; *p != '\0'; p++) {
	  if (isdigit(*p)) {
	    cs += *p - '0';
	  }
	  else if (isupper(*p)) {
	    cs += *p - 'A' + 10;
	  }
	  else if (islower(*p)) {
	    cs += *p - 'a' + 10;
	  }
	}

	return (cs % 10);
}

int main (int argc, char *argv[])
{
	char text[1000], buttons[2000];
	int n;
	int cs;

	text_color_set (DW_COLOR_INFO);

	if (argc < 2) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Supply text string on command line.\n");
	  exit (1);
	}

	strcpy (text, argv[1]);

	for (n = 2; n < argc; n++) {
	  strcat (text, " ");
	  strcat (text, argv[n]);
	}

	dw_printf ("Push buttons for multi-press method:\n");
	n = tt_text_to_multipress (text, 0, buttons);
	cs = checksum (buttons);

	dw_printf ("\"%s\"    checksum for call = %d\n", buttons, cs);

	dw_printf ("Push buttons for two-key method:\n");
	n = tt_text_to_two_key (text, 0, buttons);
	cs = checksum (buttons);

	dw_printf ("\"%s\"    checksum for call = %d\n", buttons, cs);

	return(0);

}  /* end main */

#endif		/* encoding */


/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Utility program for testing the decoding.
 *
 *----------------------------------------------------------------*/


#if DEC_MAIN


int main (int argc, char *argv[])
{
	char buttons[2000], text[1000];
	int n;

	text_color_set (DW_COLOR_INFO);

	if (argc < 2) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Supply button sequence on command line.\n");
	  exit (1);
	}

	strcpy (buttons, argv[1]);

	for (n = 2; n < argc; n++) {
	  strcat (buttons, argv[n]);
	}

	switch (tt_guess_type(buttons)) {
	  case TT_MULTIPRESS:
	    dw_printf ("Looks like multi-press encoding.\n");
	    break;
	  case TT_TWO_KEY:
	    dw_printf ("Looks like two-key encoding.\n");
	    break;
	  default:
	    dw_printf ("Could be either type of encoding.\n");
	    break;
	}

	dw_printf ("Decoded text from multi-press method:\n");
	n = tt_multipress_to_text (buttons, 0, text);
	dw_printf ("\"%s\"\n", text);

	dw_printf ("Decoded text from two-key method:\n");
	n = tt_two_key_to_text (buttons, 0, text);
	dw_printf ("\"%s\"\n", text);

	return(0);

}  /* end main */

#endif		/* decoding */



/* end tt-text.c */

