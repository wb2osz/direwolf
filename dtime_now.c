

#include "textcolor.h"
#include "dtime_now.h"


/* Current time in seconds but more resolution than time(). */

/* We don't care what date a 0 value represents because we */
/* only use this to calculate elapsed real time. */



#include <time.h>

#if __WIN32__
#include <windows.h>
#endif



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

	clock_gettime (CLOCK_REALTIME, &ts);

	result = ((double)(ts.tv_sec) + (double)(ts.tv_nsec) * 0.000000001);
	
#endif

#if DEBUG	
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dtime_now() returns %.3f\n", result );
#endif

	return (result);
}
