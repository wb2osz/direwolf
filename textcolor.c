
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2019  John Langner, WB2OSZ
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
 *		Previously, the only option was to put "-t 0" on the command
 *		line to disable all text color.  This is more readable but
 *		makes it harder to distinguish different types of
 *		information, e.g. received packets vs. error messages.
 *
 *		A few people have suggested ncurses.
 *		I looked at ncurses, and it doesn't seem to be the solution.
 *		It always sends the same color control codes rather than
 *		detecting the terminal type and adjusting its behavior.
 *
 *		For a long time, there was a compile time distinction between
 *		ARM (e.g. Raspberry Pi) and other platforms.  With the arrival
 *		of Raspbian Buster, we get flashing and the general Linux settings
 *		work better.
 *
 *		Since there doesn't seem to be a single universal solution,
 *		the text color option will now be allowed to have multiple values.
 *
 *--------------------------------------------------------------------*/


#include "direwolf.h"		// Should be first.  includes windows.h

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>


#if __WIN32__

#define BACKGROUND_WHITE (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY)

#else 	/* Linux, BSD, Mac OSX */


// Alternative 1:

// Was new in version 1.2, as suggested by IW2DHW.
// Tested with gnome-terminal and xterm.
// Raspbian Buster LXTerminal also likes this.
// (Should go back and check earlier versions - at one time I intentionally made ARM different.)

// Here we are using the RGB color format to set the background.
// Alas, PuTTY doesn't recognize the RGB format so the background is not set.


// Alternative 2:

// We need "blink" (5) rather than the expected bright/bold (1)
// attribute to get bright white background on some terminals.
// Makes no sense but I stumbled across that somewhere.

// This is your best choice for PuTTY.  Background (around text but not rest of line) is set to white.
// On GNOME Terminal and LXTerminal, this produces blinking text with a gray background.


// Alternative 3:

// This is using the bright/bold attribute, as you would expect from the documentation.
// Whenever a dark color is used, the background is reset and needs to be set again.
// In recent tests, background is always gray, not white like it should be.


#define MAX_T 3

static const char *t_background_white[MAX_T+1] = { "", 	"\e[48;2;255;255;255m",			"\e[5;47m",		"\e[1;47m" };

static const char *t_black[MAX_T+1]	= { "", "\e[0;30m" "\e[48;2;255;255;255m",	"\e[0;30m" "\e[5;47m",	"\e[0;30m" "\e[1;47m" };
static const char *t_red[MAX_T+1] 	= { "", "\e[1;31m" "\e[48;2;255;255;255m",	"\e[1;31m" "\e[5;47m",	"\e[1;31m" "\e[1;47m" };
static const char *t_green[MAX_T+1] 	= { "", "\e[1;32m" "\e[48;2;255;255;255m",	"\e[1;32m" "\e[5;47m",	"\e[1;32m" "\e[1;47m" };
static const char *t_dark_green[MAX_T+1]= { "", "\e[0;32m" "\e[48;2;255;255;255m",	"\e[0;32m" "\e[5;47m",	"\e[0;32m" "\e[1;47m" };
static const char *t_yellow[MAX_T+1] 	= { "", "\e[1;33m" "\e[48;2;255;255;255m",	"\e[1;33m" "\e[5;47m",	"\e[1;33m" "\e[1;47m" };
static const char *t_blue[MAX_T+1] 	= { "", "\e[1;34m" "\e[48;2;255;255;255m",	"\e[1;34m" "\e[5;47m",	"\e[1;34m" "\e[1;47m" };
static const char *t_magenta[MAX_T+1] 	= { "", "\e[1;35m" "\e[48;2;255;255;255m",	"\e[1;35m" "\e[5;47m",	"\e[1;35m" "\e[1;47m" };
static const char *t_cyan[MAX_T+1] 	= { "", "\e[0;36m" "\e[48;2;255;255;255m",	"\e[0;36m" "\e[5;47m",	"\e[0;36m" "\e[1;47m" };


/* Clear from cursor to end of screen. */

static const char clear_eos[]	= "\e[0J";

#endif	/* end Linux */


#include "textcolor.h"


/*
 * g_enable_color:
 *	0 = disable text colors.
 *	1 = default, should be good for LXTerminal, GNOME Terminal, xterm.
 *	2 = alternative, best choice for PuTTY  (i.e. remote login from Windows PC to Linux).
 *	3 = another alternative.  Additional suggestions are welcome.
 *	others... future possibility.
 */

static int g_enable_color = 1;


void text_color_init (int enable_color)
{


#if __WIN32__


	if (g_enable_color != 0) {

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

// Run a test if outside of acceptable range.

	if (enable_color < 0 || enable_color > MAX_T) {
	  int t;
	  for (t = 0; t <= MAX_T; t++) {
	    text_color_init (t);
	    printf ("-t %d", t);
	    if (t) printf ("   [white background]   ");
	    printf ("\n");
	    printf ("%sBlack ", t_black[t]);
	    printf ("%sRed ", t_red[t]);
	    printf ("%sGreen ", t_green[t]);
	    printf ("%sDark-Green ", t_dark_green[t]);
	    printf ("%sYellow ", t_yellow[t]);
	    printf ("%sBlue ", t_blue[t]);
	    printf ("%sMagenta ", t_magenta[t]);
	    printf ("%sCyan   \n", t_cyan[t]);
	   }
	   exit (EXIT_SUCCESS);
	}

	g_enable_color = enable_color;

	if (g_enable_color != 0) {
	  int t = g_enable_color;

	  if (t < 0) t = 0;
	  if (t > MAX_T) t = MAX_T;

	  printf ("%s", t_background_white[t]);
	  printf ("%s", clear_eos);
	  printf ("%s", t_black[t]);
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
	    // Release 1.6.   Dark green, same as for debug.
	    // Bright green is too hard to see with white background,
	    // attr = FOREGROUND_GREEN | FOREGROUND_INTENSITY | BACKGROUND_WHITE;
	    attr = FOREGROUND_GREEN | BACKGROUND_WHITE;
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

	int t = g_enable_color;

	if (t < 0) t = 0;
	if (t > MAX_T) t = MAX_T;

	switch (c) {

	  default:
	  case DW_COLOR_INFO:
	    printf ("%s", t_black[t]);
	    break;

	  case DW_COLOR_ERROR:
	    printf ("%s", t_red[t]);
	    break;

	  case DW_COLOR_REC:
	    // Bright green is very difficult to read against a while background.
	    // Let's use dark green instead.   release 1.6.
	    //printf ("%s", t_green[t]);
	    printf ("%s", t_dark_green[t]);
	    break;

	  case DW_COLOR_DECODED:
	    printf ("%s", t_blue[t]);
	    break;

	  case DW_COLOR_XMIT:
	    printf ("%s", t_magenta[t]);
	    break;

	  case DW_COLOR_DEBUG:
	    printf ("%s", t_dark_green[t]);
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
