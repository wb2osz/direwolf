
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014  John Langner, WB2OSZ
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


/*-------------------------------------------------------------------
 *
 * Name:        textcolor.c
 *
 * Purpose:     Originally this would only set color of text
 *	 	and we used printf everywhere.
 *		Now we also have a printf replacement that can
 *		be used to redirect all output to the desired place.
 *		This opens the door to using ncurses, a GUI, or
 *		running as a daemon.
 *
 * Description:	For Linux and Cygwin use the ANSI escape sequences.
 *		In earlier versions of Windows, the cmd window and ANSI.SYS
 *		could interpret this but it doesn't seem to be available
 *		anymore so we use a different interface.
 *
 * References:
 *		http://en.wikipedia.org/wiki/ANSI_escape_code
 *		http://academic.evergreen.edu/projects/biophysics/technotes/program/ansi_esc.htm
 *
 *

>>>> READ THIS PART!!! <<<<

 *
 * 
 * Problem:	The ANSI escape sequences, used on Linux, allow 8 basic colors.
 *		Unfortunately, white is not one of them.  We only have dark
 *		white, also known as light gray.  To get brighter colors, 
 *		we need to apply an attribute.  On some systems, the bold 
 *		attribute produces a brighter color rather than a bold font.
 *		On other systems, we need to use the blink attribute to get 
 *		bright colors, including white.  However on others, blink
 *		does actually produce blinking characters.
 *
 *		Several people have also complained that bright green is
 *		very hard to read against a light background.  The current
 *		implementation does not allow easy user customization of colors.
 *		
 *		Currently, the only option is to put "-t 0" on the command
 *		line to disable all text color.  This is more readable but
 *		makes it harder to distinguish different types of
 *		information, e.g. received packets vs. error messages.
 *
 *		A few people have suggested ncurses.  This needs to 
 *		be investigated for a future version.   The foundation has
 *		already been put in place.  All of the printf's should have been
 *		replaced by dw_printf, defined in this file.   All of the
 *		text output is now being funneled thru this one function
 *		so it should be easy to send it to the user by some
 *		other means.
 *
 *--------------------------------------------------------------------*/


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>


#if __WIN32__

#include <windows.h>

#define BACKGROUND_WHITE (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY)



#elif __CYGWIN__	/* Cygwin */

/* For Cygwin we need "blink" (5) rather than the */
/* expected bright/bold (1) to get bright white background. */
/* Makes no sense but I stumbled across that somewhere. */

static const char background_white[] = "\e[5;47m";

/* Whenever a dark color is used, the */
/* background is reset and needs to be set again. */

static const char black[]	= "\e[0;30m" "\e[5;47m";
static const char red[] 	= "\e[1;31m";
static const char green[] 	= "\e[1;32m";
static const char yellow[] 	= "\e[1;33m";
static const char blue[] 	= "\e[1;34m";
static const char magenta[] 	= "\e[1;35m";
static const char cyan[] 	= "\e[1;36m";
static const char dark_green[]	= "\e[0;32m" "\e[5;47m";

/* Clear from cursor to end of screen. */

static const char clear_eos[]	= "\e[0J";


#elif __arm__ 	/* Linux on Raspberry Pi or similar */

/* We need "blink" (5) rather than the */
/* expected bright/bold (1) to get bright white background. */
/* Makes no sense but I stumbled across that somewhere. */

/* If you do get blinking, remove all references to "\e[5;47m" */

static const char background_white[] = "\e[5;47m";

/* Whenever a dark color is used, the */
/* background is reset and needs to be set again. */

static const char black[]	= "\e[0;30m" "\e[5;47m";
static const char red[] 	= "\e[1;31m" "\e[5;47m";
static const char green[] 	= "\e[1;32m" "\e[5;47m";
static const char yellow[] 	= "\e[1;33m" "\e[5;47m";
static const char blue[] 	= "\e[1;34m" "\e[5;47m";
static const char magenta[] 	= "\e[1;35m" "\e[5;47m";
static const char cyan[] 	= "\e[1;36m" "\e[5;47m";
static const char dark_green[]	= "\e[0;32m" "\e[5;47m";

/* Clear from cursor to end of screen. */

static const char clear_eos[]	= "\e[0J";


#else 	/* Other Linux */

#if 1		/* new in version 1.2, as suggested by IW2DHW */
		/* Test done using gnome-terminal and xterm */

static const char background_white[] = "\e[48;2;255;255;255m";

/* Whenever a dark color is used, the */
/* background is reset and needs to be set again. */


static const char black[]	= "\e[0;30m" "\e[48;2;255;255;255m";
static const char red[] 	= "\e[0;31m" "\e[48;2;255;255;255m";
static const char green[] 	= "\e[0;32m" "\e[48;2;255;255;255m";
static const char yellow[] 	= "\e[0;33m" "\e[48;2;255;255;255m";
static const char blue[] 	= "\e[0;34m" "\e[48;2;255;255;255m";
static const char magenta[] 	= "\e[0;35m" "\e[48;2;255;255;255m";
static const char cyan[] 	= "\e[0;36m" "\e[48;2;255;255;255m";
static const char dark_green[]	= "\e[0;32m" "\e[48;2;255;255;255m";


#else 		/* from version 1.1 */


static const char background_white[] = "\e[47;1m";

/* Whenever a dark color is used, the */
/* background is reset and needs to be set again. */

static const char black[]	= "\e[0;30m" "\e[1;47m";
static const char red[] 	= "\e[1;31m" "\e[1;47m";
static const char green[] 	= "\e[1;32m" "\e[1;47m"; 
static const char yellow[] 	= "\e[1;33m" "\e[1;47m";
static const char blue[] 	= "\e[1;34m" "\e[1;47m";
static const char magenta[] 	= "\e[1;35m" "\e[1;47m";
static const char cyan[] 	= "\e[1;36m" "\e[1;47m";
static const char dark_green[]	= "\e[0;32m" "\e[1;47m";


#endif


/* Clear from cursor to end of screen. */

static const char clear_eos[]	= "\e[0J";

#endif	/* end Linux */


#include "textcolor.h"


/*
 * g_enable_color:
 *	0 = disable text colors.
 *	1 = normal.
 *	others... future possibility.
 */

static int g_enable_color = 1;


void text_color_init (int enable_color)
{

	g_enable_color = enable_color;


#if __WIN32__


	if (g_enable_color) {

	  HANDLE h;
	  CONSOLE_SCREEN_BUFFER_INFO csbi;
	  WORD attr = BACKGROUND_WHITE;
	  DWORD length;
	  COORD coord;
	  DWORD nwritten;

	  h = GetStdHandle(STD_OUTPUT_HANDLE);
	  if (h != NULL && h != INVALID_HANDLE_VALUE) {

	    GetConsoleScreenBufferInfo (h, &csbi);

	    length = csbi.dwSize.X * csbi.dwSize.Y;
	    coord.X = 0; 
	    coord.Y = 0;
	    FillConsoleOutputAttribute (h, attr, length, coord, &nwritten);
	  }
	}

#else
	if (g_enable_color) {
	  //printf ("%s", clear_eos);
	  printf ("%s", background_white);
	  printf ("%s", clear_eos);
	  printf ("%s", black);
	}
#endif
}


#if __WIN32__

/* Seems that ANSI.SYS is no longer available. */


void text_color_set ( enum dw_color_e c )
{
	WORD attr;
	HANDLE h;

	if (g_enable_color == 0) {
	  return;
	}

	switch (c) {

	  default:
	  case DW_COLOR_INFO:
	    attr = BACKGROUND_WHITE;
	    break;

	  case DW_COLOR_ERROR:
	    attr = FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_WHITE;
	    break;

	  case DW_COLOR_REC:
	    attr = FOREGROUND_GREEN | FOREGROUND_INTENSITY | BACKGROUND_WHITE;
	    break;

	  case DW_COLOR_DECODED:
	    attr = FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_WHITE;
	    break;

	  case DW_COLOR_XMIT:
	    attr = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_WHITE;
	    break;

	  case DW_COLOR_DEBUG:
	    attr = FOREGROUND_GREEN | BACKGROUND_WHITE;
	    break;
	}

	h = GetStdHandle(STD_OUTPUT_HANDLE);

	if (h != NULL && h != INVALID_HANDLE_VALUE) {
	  SetConsoleTextAttribute (h, attr);
	}
}

#else

void text_color_set ( enum dw_color_e c )
{

	if (g_enable_color == 0) {
	  return;
	}

	switch (c) {

	  default:
	  case DW_COLOR_INFO:
	    printf ("%s", black);
	    break;

	  case DW_COLOR_ERROR:
	    printf ("%s", red);
	    break;

	  case DW_COLOR_REC:
	    printf ("%s", green);
	    break;

	  case DW_COLOR_DECODED:
	    printf ("%s", blue);
	    break;

	  case DW_COLOR_XMIT:
	    printf ("%s", magenta);
	    break;

	  case DW_COLOR_DEBUG:
	    printf ("%s", dark_green);
	    break;
	}
}

#endif


/*-------------------------------------------------------------------
 *
 * Name:        dw_printf 
 *
 * Purpose:     printf replacement that allows us to send all text
 *		output to stdout or other desired destination.
 *
 * Inputs:	fmt	- C language format.
 *		...	- Addtional arguments, just like printf.
 *
 *
 * Returns:	Number of characters in result.
 *
 * Bug:		Fixed size buffer.  
 *		I'd rather not do a malloc for each print.
 *
 *--------------------------------------------------------------------*/


// TODO: replace all printf, look for stderr, perror
// TODO:   $ grep printf *.c | grep -v dw_printf | grep -v fprintf | gawk '{ print $1 }' |  sort -u


int dw_printf (const char *fmt, ...) 
{
#define BSIZE 1000
	va_list args;
	char buffer[BSIZE];
	int len;
	
	va_start (args, fmt);
	len = vsnprintf (buffer, BSIZE, fmt, args);
	va_end (args);

// TODO: other possible destinations...

	fputs (buffer, stdout);
	return (len);
}



#if TESTC
main () 
{
	printf ("Initial condition\n");
	text_color_init (1);
	printf ("After text_color_init\n");
	text_color_set(DW_COLOR_INFO); 		printf ("Info\n");
	text_color_set(DW_COLOR_ERROR); 	printf ("Error\n");
	text_color_set(DW_COLOR_REC); 		printf ("Rec\n");
	text_color_set(DW_COLOR_DECODED); 	printf ("Decoded\n");
	text_color_set(DW_COLOR_XMIT); 		printf ("Xmit\n");
	text_color_set(DW_COLOR_DEBUG); 	printf ("Debug\n");
}
#endif
	
/* end textcolor.c */
