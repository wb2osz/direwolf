
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

#endif

#if __WIN32__
#define PTW32_STATIC_LIB
#include "pthreads/pthread.h"
#else
#include <pthread.h>
#endif