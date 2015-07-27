//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014  John Langner, WB2OSZ
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
 * File:	log.c
 *
 * Purpose:	Save received packets to a log file.
 *
 * Description: Rather than saving the raw, sometimes rather cryptic and
 *		unreadable, format, write separated properties into 
 *		CSV format for easy reading and later processing.
 *
 *
 *------------------------------------------------------------------*/

#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>	
#include <string.h>	
#include <ctype.h>	
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>


#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "decode_aprs.h"
#include "log.h"


/*
 * CSV format needs quotes if value contains comma or quote.
 */

static void quote_for_csv (char *out, const char *in) {
	const char *p;
	char *q = out;
	int need_quote = 0;

	for (p = in; *p != '\0'; p++) {
	  if (*p == ',' || *p == '"') {
	    need_quote = 1;
	    break;
	  }
	}

	if (need_quote) {
	  *q++ = '"';
	  for (p = in; *p != '\0'; p++) {
	    if (*p == '"') {	
	      *q++ = *p;
	    }
	    *q++ = *p;
	  }
	  *q++ = '"';
	  *q = '\0';
	}
	else {
	  strcpy (out, in);
	}
}


/*------------------------------------------------------------------
 *
 * Function:	log_init
 *
 * Purpose:	Initialization at start of application.
 *
 * Inputs:	path		- Path of log file directory.
 *				  Use "." for current directory.
 *				  Empty string disables feature.
 *
 * Global Out:	g_log_dir 	- Save directory here for later use.
 *		g_log_fp	- File pointer for writing.
 *		g_open_fname	- Name of currently open file.
 *
 *------------------------------------------------------------------*/

static char g_log_dir[80];
static FILE *g_log_fp;
static char g_open_fname[20];


void log_init (char *path) 
{ 
	struct stat st;

	strcpy (g_log_dir, "");
	g_log_fp = NULL;
	strcpy (g_open_fname, "");

	if (strlen(path) == 0) {
	  return;
	}

	if (stat(path,&st) == 0) {
	  // Exists, but is it a directory?
	  if (S_ISDIR(st.st_mode)) {
	    // Specified directory exists.
	    strcpy (g_log_dir, path);
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Log file location \"%s\" is not a directory.\n", path);
	    dw_printf ("Using current working directory \".\" instead.\n");
	    strcpy (g_log_dir, ".");
	  }
	}
	else {
	  // Doesn't exist.  Try to create it.
	  // parent directory must exist.
	  // We don't create multiple levels like "mkdir -p"
#if __WIN32__
	  if (_mkdir (path) == 0) {
#else
	  if (mkdir (path, 0777) == 0) {
#endif
	    // Success.
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Log file location \"%s\" has been created.\n", path);
	    strcpy (g_log_dir, path);
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Failed to create log file location \"%s\".\n", path);
	    dw_printf ("%s\n", strerror(errno));
	    dw_printf ("Using current working directory \".\" instead.\n");
	    strcpy (g_log_dir, ".");
	  }
	}
}



/*------------------------------------------------------------------
 *
 * Function:	log_write
 *
 * Purpose:	Save information to log file.
 *
 * Inputs:	chan	- Radio channel where heard.
 *
 *		A	- Explode information from APRS packet.
 *
 *		pp	- Received packet object.
 *
 * 		alevel	- audio level.
 *
 *		retries	- Amount of effort to get a good CRC.
 *
 *------------------------------------------------------------------*/

void log_write (int chan, decode_aprs_t *A, packet_t pp, int alevel, retry_t retries)
{
	time_t now; 		// make 'now' a parameter so we can process historical data ???
	char fname[20];
	struct tm tm;


	if (strlen(g_log_dir) == 0) return;

	// Generate the file name from current date, UTC.

	now = time(NULL);
	gmtime_r (&now, &tm);	

	// Microsoft doesn't recognize %F as equivalent to %Y-%m-%d

	strftime (fname, sizeof(fname), "%Y-%m-%d.log", &tm);

	// Close current file if name has changed

	if (g_log_fp != NULL && strcmp(fname, g_open_fname) != 0) {
	  log_term ();
	}

	// Open for append if not already open.

	if (g_log_fp == NULL) {
	  char full_path[120];
	  struct stat st;
	  int already_there;

	  strcpy (full_path, g_log_dir);
#if __WIN32__
	  strcat (full_path, "\\");
#else
	  strcat (full_path, "/");
#endif
	  strcat (full_path, fname);

	  // See if it already exists.
	  // This is used later to write a header if it did not exist already.

	  already_there = stat(full_path,&st) == 0;

	  text_color_set(DW_COLOR_INFO);
	  dw_printf("Opening log file \"%s\".\n", fname);

	  g_log_fp = fopen (full_path, "a");

	  if (g_log_fp != NULL) {
	    strcpy (g_open_fname, fname);
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("Can't open log file \"%s\" for write.\n", full_path);
	    dw_printf ("%s\n", strerror(errno));
	    strcpy (g_open_fname, "");
	    return;
	  }

	  // Write a header suitable for importing into a spreadsheet
	  // only if this will be the first line.
	
	  if ( ! already_there) {
	    fprintf (g_log_fp, "chan,utime,isotime,source,heard,level,error,dti,name,symbol,latitude,longitude,speed,course,altitude,frequency,offset,tone,system,status,comment\n");
	  }
	}

	if (g_log_fp != NULL) {

	  char itime[24];
	  char heard[AX25_MAX_ADDR_LEN+1];
	  int h;
	  char stemp[256];
	  char slat[16], slon[16], sspd[12], scse[12], salt[12];
	  char sfreq[20], soffs[10], stone[10];
	  char sdti[10];
	  char sname[24];
	  char ssymbol[8];
	  char smfr[60];
	  char sstatus[40];
	  char stelemetry[200];
	  char scomment[256];


	  // Microsoft doesn't recognize %T as equivalent to %H:%M:%S

	  strftime (itime, sizeof(itime), "%Y-%m-%dT%H:%M:%SZ", &tm);

          /* Who are we hearing?   Original station or digipeater? */
	  /* Similar code in direwolf.c.  Combine into one function? */

	  strcpy(heard, "");
	  if (pp != NULL) {
	    if (ax25_get_num_addr(pp) == 0) {
	      /* Not AX.25. No station to display below. */
	      h = -1;
	      strcpy (heard, "");
	    }
	    else {
	      h = ax25_get_heard(pp);
              ax25_get_addr_with_ssid(pp, h, heard);
	    }
	   
	    if (h >= AX25_REPEATER_2 && 
	        strncmp(heard, "WIDE", 4) == 0 &&
	        isdigit(heard[4]) &&
	        heard[5] == '\0') {

	      ax25_get_addr_with_ssid(pp, h-1, heard);
	      strcat (heard, "?");
	    }
	  }

	  // Might need to quote anything that could contain comma or quote.

	  strcpy(sdti, "");
	  if (pp != NULL) {
	    stemp[0] = ax25_get_dti(pp);
	    stemp[1] = '\0';
	    quote_for_csv (sdti, stemp);
	  }

	  quote_for_csv (sname, (strlen(A->g_name) > 0) ? A->g_name : A->g_src);

	  stemp[0] = A->g_symbol_table;
	  stemp[1] = A->g_symbol_code;
	  stemp[2] = '\0';
	  quote_for_csv (ssymbol, stemp);

	  quote_for_csv (smfr, A->g_mfr);
	  quote_for_csv (sstatus, A->g_mic_e_status);
	  quote_for_csv (stelemetry, A->g_telemetry);
	  quote_for_csv (scomment, A->g_comment);

	  strcpy (slat, "");  if (A->g_lat != G_UNKNOWN)      sprintf (slat, "%.6f", A->g_lat);
	  strcpy (slon, "");  if (A->g_lon != G_UNKNOWN)      sprintf (slon, "%.6f", A->g_lon);
	  strcpy (sspd, "");  if (A->g_speed != G_UNKNOWN)    sprintf (sspd, "%.1f", DW_MPH_TO_KNOTS(A->g_speed));
	  strcpy (scse, "");  if (A->g_course != G_UNKNOWN)   sprintf (scse, "%.1f", A->g_course);
	  strcpy (salt, "");  if (A->g_altitude != G_UNKNOWN) sprintf (salt, "%.1f", DW_FEET_TO_METERS(A->g_altitude));

	  strcpy (sfreq, "");  if (A->g_freq   != G_UNKNOWN) sprintf (sfreq, "%.3f", A->g_freq);
	  strcpy (soffs, "");  if (A->g_offset != G_UNKNOWN) sprintf (soffs, "%+d", A->g_offset);
	  strcpy (stone, "");  if (A->g_tone   != G_UNKNOWN) sprintf (stone, "%.1f", A->g_tone);
	                       if (A->g_dcs    != G_UNKNOWN) sprintf (stone, "D%03o", A->g_dcs);

	  fprintf (g_log_fp, "%d,%d,%s,%s,%s,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", 
			chan, (int)now, itime, 
			A->g_src, heard, alevel, (int)retries, sdti,
			sname, ssymbol,
			slat, slon, sspd, scse, salt, 
			sfreq, soffs, stone, 
			smfr, sstatus, stelemetry, scomment);
	  fflush (g_log_fp);
	}

} /* end log_write */



/*------------------------------------------------------------------
 *
 * Function:	log_term
 *
 * Purpose:	Close any open log file.
 *		Called when exiting or when date changes.
 *
 *------------------------------------------------------------------*/


void log_term (void)
{
	if (g_log_fp != NULL) {

	  text_color_set(DW_COLOR_INFO);
	  dw_printf("Closing log file \"%s\".\n", g_open_fname);

	  fclose (g_log_fp);

	  g_log_fp = NULL;
	  strcpy (g_open_fname, "");
	}

} /* end log_term */


/* end log.c */
