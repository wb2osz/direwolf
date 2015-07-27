
#ifndef DIREWOLF_H
#define DIREWOLF_H 1


/*
 * Maximum number of radio channels.
 */

#define MAX_CHANS 2

/*
 * Maximum number of modems per channel.
 * I called them "subchannels" (in the code) because 
 * it is short and unambiguous.
 * Nothing magic about the number.  Could be larger
 * but CPU demands might be overwhelming.
 */

#define MAX_SUBCHANS 9


#if __WIN32__
#include <windows.h>
#define SLEEP_SEC(n) Sleep((n)*1000)
#define SLEEP_MS(n) Sleep(n)
#else
#define SLEEP_SEC(n) sleep(n)
#define SLEEP_MS(n) usleep((n)*1000)
#endif


#if __WIN32__
#define PTW32_STATIC_LIB
#include "pthreads/pthread.h"
#else
#include <pthread.h>
#endif


/* Not sure where to put these. */

/* Prefix with DW_ because /usr/include/gps.h uses a couple of these names. */


#define DW_METERS_TO_FEET(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 3.2808399)
#define DW_FEET_TO_METERS(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.3048)
#define DW_KM_TO_MILES(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.621371192)

#define DW_KNOTS_TO_MPH(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 1.15077945)
#define DW_KNOTS_TO_METERS_PER_SEC(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.51444444444)
#define DW_MPH_TO_KNOTS(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.868976)
#define DW_MPH_TO_METERS_PER_SEC(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.44704)

#define DW_MBAR_TO_INHG(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.0295333727)

#endif   /* ifndef DIREWOLF_H */