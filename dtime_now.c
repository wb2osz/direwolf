
#include "direwolf.h"

#include <stdio.h>

#include "textcolor.h"
#include "dtime_now.h"


/* Current time in seconds but more resolution than time(). */

/* We don't care what date a 0 value represents because we */
/* only use this to calculate elapsed real time. */



#include <time.h>

#ifdef __APPLE__
#include <sys/time.h>
#endif

#include <string.h>		// needed for Mac.


/*------------------------------------------------------------------
 *
 * Name:	dtime_now
 *
 * Purpose:   	Return current time as double precision.
 *		
 * Input:	none
 *
 * Returns:	Unix time, as double precision, so we can get resolution
 *		finer than one second.		
 *
 * Description:	Normal unix time is in seconds since 1/1/1970 00:00:00 UTC.
 *		Sometimes we want resolution finer than a second.
 *		Rather than having a separate variable for the fractional
 *		part of a second, and having extra calculations everywhere,
 *		simply use double precision floating point to make usage
 *		easier.
 *		
 *---------------------------------------------------------------*/


double dtime_now (void)
{
	double result;

#if __WIN32__
	/* 64 bit integer is number of 100 nanosecond intervals from Jan 1, 1601. */

	FILETIME ft;
	
	GetSystemTimeAsFileTime (&ft);

	result = ((( (double)ft.dwHighDateTime * (256. * 256. * 256. * 256.) + 
			(double)ft.dwLowDateTime ) / 10000000.) - 11644473600.);
#else
	/* tv_sec is seconds from Jan 1, 1970. */

	struct timespec ts;

#ifdef __APPLE__
	struct timeval tp;
	gettimeofday(&tp, NULL);
	ts.tv_nsec = tp.tv_usec * 1000;
	ts.tv_sec  = tp.tv_sec;
#else
	clock_gettime (CLOCK_REALTIME, &ts);
#endif

	result = ((double)(ts.tv_sec) + (double)(ts.tv_nsec) * 0.000000001);
	
#endif

#if DEBUG	
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dtime_now() returns %.3f\n", result );
#endif

	return (result);
}


#if __WIN32__

/* 
 * Windows doesn't have localtime_r.
 * It should have the equivalent localtime_s, with opposite parameter
 * order,  but I get undefined reference when trying to use it.
 */

static struct tm *localtime_r(time_t *clock, struct tm *res)
{
	struct tm *tm;

	tm = localtime (clock);
	memcpy (res, tm, sizeof(struct tm));
	return (res);
}

#endif


/*------------------------------------------------------------------
 *
 * Name:	timestamp_now
 *
 * Purpose:   	Convert local time to one of these formats for debug output.
 *
 *			HH:MM:SS
 *			HH:MM:SS.mmm
 *		
 * Input:	result_size	- Size of result location.
 *				  Should be at least 9 or 13.
 *
 *		show_ms		- True to display milliseconds.
 *
 * Output:	result		- Result is placed here.
 *
 *---------------------------------------------------------------*/

void timestamp_now (char *result, int result_size, int show_ms)
{
	double now = dtime_now();
	time_t t = (int)now;
	struct tm tm;

	localtime_r (&t, &tm);
	strftime (result, result_size, "%H:%M:%S", &tm);

	if (show_ms) {
	  int ms = (now - (int)t) * 1000;
	  char strms[16];

	  if (ms == 1000) ms = 999;
	  sprintf (strms, ".%03d", ms);
	  strlcat (result, strms, result_size);
	}

}  /* end timestamp_now */



/*------------------------------------------------------------------
 *
 * Name:	timestamp_user_format
 *
 * Purpose:   	Convert local time user-specified format.  e.g.
 *
 *			HH:MM:SS
 *			mm/dd/YYYY HH:MM:SS
 *			dd/mm/YYYY HH:MM:SS
 *		
 * Input:	result_size	- Size of result location.
 *
 *		user_format	- See strftime documentation.
 *
 *					https://linux.die.net/man/3/strftime
 *					https://msdn.microsoft.com/en-us/library/aa272978(v=vs.60).aspx
 *					
 *				  Note that Windows does not support all of the Linux formats.
 *				  For example, Linux has %T which is equivalent to %H:%M:%S
 *
 * Output:	result		- Result is placed here.
 *
 *---------------------------------------------------------------*/

void timestamp_user_format (char *result, int result_size, char *user_format)
{
	double now = dtime_now();
	time_t t = (int)now;
	struct tm tm;

	localtime_r (&t, &tm);
	strftime (result, result_size, user_format, &tm);

}  /* end timestamp_user_format */


/*------------------------------------------------------------------
 *
 * Name:	timestamp_filename
 *
 * Purpose:   	Generate unique file name based on the current time.
 *		The format will be:		
 *
 *			YYYYMMDD-HHMMSS-mmm
 *		
 * Input:	result_size	- Size of result location.
 *				  Should be at least 20.
 *
 * Output:	result		- Result is placed here.
 *
 * Description:	This is for the kissutil "-r" option which places
 *		each received frame in a new file.  It is possible to
 *		have two packets arrive in less than a second so we
 *		need more than one second resolution.
 *
 *		What if someone wants UTC, rather than local time?
 *		You can simply set an environment variable like this:
 *
 *			TZ=UTC direwolf
 *
 *		so it's probably not worth the effort to add another
 *		option.
 *
 *---------------------------------------------------------------*/

void timestamp_filename (char *result, int result_size)
{
	double now = dtime_now();
	time_t t = (int)now;
	struct tm tm;

	localtime_r (&t, &tm);
	strftime (result, result_size, "%Y%m%d-%H%M%S", &tm);

	int ms = (now - (int)t) * 1000;
	char strms[16];

	if (ms == 1000) ms = 999;
	sprintf (strms, "-%03d", ms);
	strlcat (result, strms, result_size);

}  /* end timestamp_filename */

