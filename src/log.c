//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014, 2015  John Langner, WB2OSZ
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
 *		There are two alternatives here.
 *
 *		-L logfile		Specify full file path.
 *
 *		-l logdir		Daily names will be created here.
 *
 *		Use one or the other but not both.
 *
 *------------------------------------------------------------------*/

#include "direwolf.h"

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

#if __WIN32__
#include <direct.h> 	// for _mkdir()
#endif

#include "ax25_pad.h"
#include "textcolor.h"
#include "decode_aprs.h"
#include "log.h"

static void log_term (void);


/*
 * CSV format needs quotes if value contains comma or quote.
 */

static void quote_for_csv (char *out, size_t outsize, const char *in) {
	const char *p;
	char *q = out;
	int need_quote = 0;

	for (p = in; *p != '\0'; p++) {
	  if (*p == ',' || *p == '"') {
	    need_quote = 1;
	    break;
	  }
	}

// BUG: need to avoid buffer overflow on "out".   *strcpy*

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
	  strlcpy (out, in, outsize);
	}
}


/*------------------------------------------------------------------
 *
 * Function:	log_init
 *
 * Purpose:	Initialization at start of application.
 *
 * Inputs:	daily_names	- True if daily names should be generated.
 *				  In this case path is a directory.
 *				  When false, path would be the file name.
 *
 *		path		- Log file name or just directory.
 *				  Use "." for current directory.
 *				  Empty string disables feature.
 *
 * Global Out:	g_daily_names	- True if daily names should be generated.
 *
 *		g_log_path 	- Save directory or full name here for later use.
 *
 *		g_log_fp	- File pointer for writing.
 *				  Note that file is kept open.
 *				  We don't open/close for every new item.
 *
 *		g_open_fname	- Name of currently open file.
 *				  Applicable only when g_daily_names is true.
 *
 *------------------------------------------------------------------*/

static int g_daily_names;
static char g_log_path[80];
static FILE *g_log_fp;
static char g_open_fname[20];


void log_init (int daily_names, char *path)
{ 
	struct stat st;

	g_daily_names = daily_names;
	strlcpy (g_log_path, "", sizeof(g_log_path));
	g_log_fp = NULL;
	strlcpy (g_open_fname, "", sizeof(g_open_fname));

	if (strlen(path) == 0) {
	  return;
	}

	if (g_daily_names) {

// Original strategy.  Automatic daily file names.

	  if (stat(path,&st) == 0) {
	    // Exists, but is it a directory?
	    if (S_ISDIR(st.st_mode)) {
	      // Specified directory exists.
	      strlcpy (g_log_path, path, sizeof(g_log_path));
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Log file location \"%s\" is not a directory.\n", path);
	      dw_printf ("Using current working directory \".\" instead.\n");
	      strlcpy (g_log_path, ".", sizeof(g_log_path));
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
	      strlcpy (g_log_path, path, sizeof(g_log_path));
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Failed to create log file location \"%s\".\n", path);
	      dw_printf ("%s\n", strerror(errno));
	      dw_printf ("Using current working directory \".\" instead.\n");
	      strlcpy (g_log_path, ".", sizeof(g_log_path));
	    }
	  }
	}
	else {

// Added in version 1.5.  Single file.
// Typically logrotate would be used to keep size under control.

	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Log file is \"%s\"\n", path);
	  strlcpy (g_log_path, path, sizeof(g_log_path));
	}

// Clean close on exit.

	atexit (log_term);

} /* end log_init */



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

void log_write (int chan, decode_aprs_t *A, packet_t pp, alevel_t alevel, retry_t retries)
{
	time_t now;
	struct tm tm;


	if (strlen(g_log_path) == 0) return;

	now = time(NULL);			// Get current time.
	(void)gmtime_r (&now, &tm);	
// FIXME:  https://github.com/wb2osz/direwolf/issues/473

	if (g_daily_names) {

// Original strategy.  Automatic daily file names.

	  char fname[20];

	  // Generate the file name from current date, UTC.
	  // Why UTC rather than local time?  I don't recall the reasoning.
	  // It's been there a few years and no on complained so leave it alone for now.

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

	    strlcpy (full_path, g_log_path, sizeof(full_path));
#if __WIN32__
	    strlcat (full_path, "\\", sizeof(full_path));
#else
	    strlcat (full_path, "/", sizeof(full_path));
#endif
	    strlcat (full_path, fname, sizeof(full_path));

	    // See if file already exists and not empty.
	    // This is used later to write a header if it did not exist already.

	    already_there = (stat(full_path,&st) == 0) && (st.st_size > 0);

	    text_color_set(DW_COLOR_INFO);
	    dw_printf("Opening log file \"%s\".\n", fname);

	    g_log_fp = fopen (full_path, "a");

	    if (g_log_fp != NULL) {
	      strlcpy (g_open_fname, fname, sizeof(g_open_fname));
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Can't open log file \"%s\" for write.\n", full_path);
	      dw_printf ("%s\n", strerror(errno));
	      strlcpy (g_open_fname, "", sizeof(g_open_fname));
	      return;
	    }

	    // Write a header suitable for importing into a spreadsheet
	    // only if this will be the first line.
	
	    if ( ! already_there) {
	      fprintf (g_log_fp, "chan,utime,isotime,source,heard,level,error,dti,name,symbol,latitude,longitude,speed,course,altitude,frequency,offset,tone,system,status,telemetry,comment\n");
	    }
	  }
	}
	else {

// Added in version 1.5.  Single file.

	  // Open for append if not already open.

	  if (g_log_fp == NULL) {
	    struct stat st;
	    int already_there;

	    // See if file already exists and not empty.
	    // This is used later to write a header if it did not exist already.

	    already_there = (stat(g_log_path,&st) == 0) && (st.st_size > 0);

	    text_color_set(DW_COLOR_INFO);
	    dw_printf("Opening log file \"%s\"\n", g_log_path);

	    g_log_fp = fopen (g_log_path, "a");

	    if (g_log_fp == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Can't open log file \"%s\" for write.\n", g_log_path);
	      dw_printf ("%s\n", strerror(errno));
	      strlcpy (g_log_path, "", sizeof(g_log_path));
	      return;
	    }

	    // Write a header suitable for importing into a spreadsheet
	    // only if this will be the first line.

	    if ( ! already_there) {
	      fprintf (g_log_fp, "chan,utime,isotime,source,heard,level,error,dti,name,symbol,latitude,longitude,speed,course,altitude,frequency,offset,tone,system,status,telemetry,comment\n");
	    }
	  }
	}


// Add line to file if it is now open.

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
	  char alevel_text[40];



	  // Microsoft doesn't recognize %T as equivalent to %H:%M:%S

	  strftime (itime, sizeof(itime), "%Y-%m-%dT%H:%M:%SZ", &tm);

          /* Who are we hearing?   Original station or digipeater? */
	  /* Similar code in direwolf.c.  Combine into one function? */

	  strlcpy(heard, "", sizeof(heard));
	  if (pp != NULL) {
	    if (ax25_get_num_addr(pp) == 0) {
	      /* Not AX.25. No station to display below. */
	      h = -1;
	      strlcpy (heard, "", sizeof(heard));
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
	      strlcat (heard, "?", sizeof(heard));
	    }
	  }

	  ax25_alevel_to_text (alevel, alevel_text);


	  // Might need to quote anything that could contain comma or quote.

	  strlcpy(sdti, "", sizeof(sdti));
	  if (pp != NULL) {
	    stemp[0] = ax25_get_dti(pp);
	    stemp[1] = '\0';
	    quote_for_csv (sdti, sizeof(sdti), stemp);
	  }

	  quote_for_csv (sname, sizeof(sname), (strlen(A->g_name) > 0) ? A->g_name : A->g_src);

	  stemp[0] = A->g_symbol_table;
	  stemp[1] = A->g_symbol_code;
	  stemp[2] = '\0';
	  quote_for_csv (ssymbol, sizeof(ssymbol), stemp);

	  quote_for_csv (smfr, sizeof(smfr), A->g_mfr);
	  quote_for_csv (sstatus, sizeof(sstatus), A->g_mic_e_status);
	  quote_for_csv (stelemetry, sizeof(stelemetry), A->g_telemetry);
	  quote_for_csv (scomment, sizeof(scomment), A->g_comment);

	  strlcpy (slat, "", sizeof(slat));  if (A->g_lat != G_UNKNOWN)         snprintf (slat, sizeof(slat), "%.6f", A->g_lat);
	  strlcpy (slon, "", sizeof(slon));  if (A->g_lon != G_UNKNOWN)         snprintf (slon, sizeof(slon), "%.6f", A->g_lon);
	  strlcpy (sspd, "", sizeof(sspd));  if (A->g_speed_mph != G_UNKNOWN)   snprintf (sspd, sizeof(sspd), "%.1f", DW_MPH_TO_KNOTS(A->g_speed_mph));
	  strlcpy (scse, "", sizeof(scse));  if (A->g_course != G_UNKNOWN)      snprintf (scse, sizeof(scse), "%.1f", A->g_course);
	  strlcpy (salt, "", sizeof(salt));  if (A->g_altitude_ft != G_UNKNOWN) snprintf (salt, sizeof(salt), "%.1f", DW_FEET_TO_METERS(A->g_altitude_ft));

	  strlcpy (sfreq, "", sizeof(sfreq));  if (A->g_freq   != G_UNKNOWN) snprintf (sfreq, sizeof(sfreq), "%.3f", A->g_freq);
	  strlcpy (soffs, "", sizeof(soffs));  if (A->g_offset != G_UNKNOWN) snprintf (soffs, sizeof(soffs), "%+d", A->g_offset);
	  strlcpy (stone, "", sizeof(stone));  if (A->g_tone   != G_UNKNOWN) snprintf (stone, sizeof(stone), "%.1f", A->g_tone);
	                       if (A->g_dcs    != G_UNKNOWN) snprintf (stone, sizeof(stone), "D%03o", A->g_dcs);

	  fprintf (g_log_fp, "%d,%d,%s,%s,%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", 
			chan, (int)now, itime, 
			A->g_src, heard, alevel_text, (int)retries, sdti,
			sname, ssymbol,
			slat, slon, sspd, scse, salt, 
			sfreq, soffs, stone, 
			smfr, sstatus, stelemetry, scomment);

	  fflush (g_log_fp);
	}

} /* end log_write */



/*------------------------------------------------------------------
 *
 * Function:	log_rr_bits
 *
 * Purpose:	Quick hack to look at the C and RR bits just to see what is there.
 *		This seems like a good place because it is a small subset of the function above.
 *
 * Inputs:	A	- Explode information from APRS packet.
 *
 *		pp	- Received packet object.
 *
 *------------------------------------------------------------------*/

void log_rr_bits (decode_aprs_t *A, packet_t pp)
{

	if (1) {

	  char heard[AX25_MAX_ADDR_LEN+1];
	  char smfr[60];
	  char *p;
	  int src_c, dst_c;
	  int src_rr, dst_rr;

	  // Sanitize system type (manufacturer) changing any comma to period.

	  strlcpy (smfr, A->g_mfr, sizeof(smfr));
	  for (p=smfr; *p!='\0'; p++) {
	    if (*p == ',') *p = '.';
	  }

          /* Who are we hearing?   Original station or digipeater? */
	  /* Similar code in direwolf.c.  Combine into one function? */

	  strlcpy(heard, "", sizeof(heard));

	  if (pp != NULL) {
	    int h;

	    if (ax25_get_num_addr(pp) == 0) {
	      /* Not AX.25. No station to display below. */
	      h = -1;
	      strlcpy (heard, "", sizeof(heard));
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
	      strlcat (heard, "?", sizeof(heard));
	    }

	    src_c = ax25_get_h (pp, AX25_SOURCE);
	    dst_c = ax25_get_h (pp, AX25_DESTINATION);
	    src_rr = ax25_get_rr (pp, AX25_SOURCE);
	    dst_rr = ax25_get_rr (pp, AX25_DESTINATION);

	    // C RR	for source
	    // C RR	for destination
	    // system type
	    // source
	    // station heard

	    text_color_set(DW_COLOR_INFO);

	    dw_printf ("%d %d%d  %d %d%d,%s,%s,%s\n",
			src_c, (src_rr >> 1) & 1, src_rr & 1,
			dst_c, (dst_rr >> 1) & 1, dst_rr & 1,
			smfr, A->g_src, heard);
	  }
	}

} /* end log_rr_bits */




/*------------------------------------------------------------------
 *
 * Function:	log_term
 *
 * Purpose:	Close any open log file.
 *		Called when exiting or when date changes.
 *
 *------------------------------------------------------------------*/


static void log_term (void)
{
	if (g_log_fp != NULL) {

	  text_color_set(DW_COLOR_INFO);

	  if (g_daily_names) {
	    dw_printf("Closing log file \"%s\".\n", g_open_fname);
	  }
	  else {
	    dw_printf("Closing log file \"%s\".\n", g_log_path);
	  }

	  fclose (g_log_fp);

	  g_log_fp = NULL;
	  strlcpy (g_open_fname, "", sizeof(g_open_fname));
	}

} /* end log_term */


/* end log.c */
