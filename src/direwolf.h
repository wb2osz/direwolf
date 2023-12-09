
/* direwolf.h - Common stuff used many places. */

// TODO:   include this file first before anything else in each .c file.


#ifdef NDEBUG
#undef NDEBUG		// Because it would disable assert().
#endif


#ifndef DIREWOLF_H
#define DIREWOLF_H 1

/*
 * Support Windows XP and later.
 *
 * We need this before "#include <ws2tcpip.h>".
 *
 * Don't know what other impact it might have on others.
 */

#if __WIN32__

#ifdef _WIN32_WINNT
#error	Include "direwolf.h" before any windows system files.
#endif
#ifdef WINVER
#error	Include "direwolf.h" before any windows system files.
#endif

#define _WIN32_WINNT 0x0501     /* Minimum OS version is XP. */
#define WINVER       0x0501     /* Minimum OS version is XP. */

#include <winsock2.h>
#include <windows.h>

#endif

/*
 * Maximum number of audio devices.
 * Three is probably adequate for standard version.
 * Larger reasonable numbers should also be fine.
 *
 * For example, if you wanted to use 4 audio devices at once, change this to 4.
 */

#define MAX_ADEVS 3			

	
/*
 * Maximum number of radio channels.
 * Note that there could be gaps.
 * Suppose audio device 0 was in mono mode and audio device 1 was stereo.
 * The channels available would be:
 *
 *	ADevice 0:	channel 0
 *	ADevice 1:	left = 2, right = 3
 *
 * TODO1.2:  Look for any places that have
 *		for (ch=0; ch<MAX_CHANS; ch++) ...
 * and make sure they handle undefined channels correctly.
 */

#define MAX_RADIO_CHANS ((MAX_ADEVS) * 2)

#define MAX_CHANS MAX_RADIO_CHANS	// TODO: Replace all former  with latter to avoid confusion with following.

#define MAX_TOTAL_CHANS 16		// v1.7 allows additional virtual channels which are connected
					// to something other than radio modems.
					// Total maximum channels is based on the 4 bit KISS field.
					// Someone with very unusual requirements could increase this and
					// use only the AGW network protocol.

/*
 * Maximum number of rigs.
 */

#ifdef USE_HAMLIB
#define MAX_RIGS MAX_CHANS
#endif

/*
 * Get audio device number for given channel.
 * and first channel for given device.
 */

#define ACHAN2ADEV(n) ((n)>>1)
#define ADEVFIRSTCHAN(n) ((n) * 2)

/*
 * Maximum number of modems per channel.
 * I called them "subchannels" (in the code) because 
 * it is short and unambiguous.
 * Nothing magic about the number.  Could be larger
 * but CPU demands might be overwhelming.
 */

#define MAX_SUBCHANS 9

/*
 * Each one of these can have multiple slicers, at
 * different levels, to compensate for different
 * amplitudes of the AFSK tones.
 * Initially used same number as subchannels but
 * we could probably trim this down a little
 * without impacting performance.
 */

#define MAX_SLICERS 9


#if __WIN32__
#define SLEEP_SEC(n) Sleep((n)*1000)
#define SLEEP_MS(n) Sleep(n)
#else
#define SLEEP_SEC(n) sleep(n)
#define SLEEP_MS(n) usleep((n)*1000)
#endif

#if __WIN32__

#define PTW32_STATIC_LIB
//#include "pthreads/pthread.h"

// This enables definitions of localtime_r and gmtime_r in system time.h.
//#define _POSIX_THREAD_SAFE_FUNCTIONS 1
#define _POSIX_C_SOURCE 1

#else
 #include <pthread.h>
#endif


#ifdef __APPLE__

// https://groups.yahoo.com/neo/groups/direwolf_packet/conversations/messages/2072

// The original suggestion was to add this to only ptt.c.
// I thought it would make sense to put it here, so it will apply to all files,
// consistently, rather than only one file ptt.c.

// The placement of this is critical.  Putting it earlier was a problem.
// https://github.com/wb2osz/direwolf/issues/113

// It needs to be after the include pthread.h because
// pthread.h pulls in <sys/cdefs.h>, which redefines __DARWIN_C_LEVEL back to ansi,
// which breaks things.
// Maybe it should just go in ptt.c as originally suggested.

// #define __DARWIN_C_LEVEL  __DARWIN_C_FULL

// There is a more involved patch here:
//  https://groups.yahoo.com/neo/groups/direwolf_packet/conversations/messages/2458

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

// Defining _DARWIN_C_SOURCE ensures that the definition for the cfmakeraw function (or similar)
// are pulled in through the include file <sys/termios.h>.

#ifdef __DARWIN_C_LEVEL
#undef __DARWIN_C_LEVEL
#endif

#define __DARWIN_C_LEVEL  __DARWIN_C_FULL

#endif


/* Not sure where to put these. */

/* Prefix with DW_ because /usr/include/gps.h uses a couple of these names. */

#ifndef G_UNKNOWN
#include "latlong.h"
#endif


#define DW_METERS_TO_FEET(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 3.2808399)
#define DW_FEET_TO_METERS(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.3048)
#define DW_KM_TO_MILES(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.621371192)
#define DW_MILES_TO_KM(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 1.609344)

#define DW_KNOTS_TO_MPH(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 1.15077945)
#define DW_KNOTS_TO_METERS_PER_SEC(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.51444444444)
#define DW_MPH_TO_KNOTS(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.868976)
#define DW_MPH_TO_METERS_PER_SEC(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.44704)

#define DW_MBAR_TO_INHG(x) ((x) == G_UNKNOWN ? G_UNKNOWN : (x) * 0.0295333727)




#if __WIN32__

typedef CRITICAL_SECTION dw_mutex_t;

#define dw_mutex_init(x) \
	InitializeCriticalSection (x)

/* This one waits for lock. */

#define dw_mutex_lock(x) \
	EnterCriticalSection (x) 

/* Returns non-zero if lock was obtained. */

#define dw_mutex_try_lock(x) \
	TryEnterCriticalSection (x)

#define dw_mutex_unlock(x) \
	LeaveCriticalSection (x)


#else

typedef pthread_mutex_t dw_mutex_t;

#define dw_mutex_init(x) pthread_mutex_init (x, NULL)

/* this one will wait. */

#define dw_mutex_lock(x) \
	{	\
	  int err; \
	  err = pthread_mutex_lock (x); \
	  if (err != 0) { \
	    text_color_set(DW_COLOR_ERROR); \
	    dw_printf ("INTERNAL ERROR %s %d pthread_mutex_lock returned %d", __FILE__, __LINE__, err); \
	    exit (1); \
	  } \
	}

/* This one returns true if lock successful, false if not. */
/* pthread_mutex_trylock returns 0 for success. */

#define dw_mutex_try_lock(x) \
	({	\
	  int err; \
	  err = pthread_mutex_trylock (x); \
	  if (err != 0 && err != EBUSY) { \
	    text_color_set(DW_COLOR_ERROR); \
	    dw_printf ("INTERNAL ERROR %s %d pthread_mutex_trylock returned %d", __FILE__, __LINE__, err); \
	    exit (1); \
	  } ; \
	  ! err; \
	})

#define dw_mutex_unlock(x) \
	{	\
	  int err; \
	  err = pthread_mutex_unlock (x); \
	  if (err != 0) { \
	    text_color_set(DW_COLOR_ERROR); \
	    dw_printf ("INTERNAL ERROR %s %d pthread_mutex_unlock returned %d", __FILE__, __LINE__, err); \
	    exit (1); \
	  } \
	}

#endif



// Formerly used write/read on Linux, for some forgotten reason,
// but always using send/recv makes more sense.
// Need option to prevent a SIGPIPE signal on Linux.  (added for 1.5 beta 2)

#if __WIN32__ || __APPLE__
#define SOCK_SEND(s,data,size) send(s,data,size,0)
#else
#define SOCK_SEND(s,data,size) send(s,data,size, MSG_NOSIGNAL)
#endif
#define SOCK_RECV(s,data,size) recv(s,data,size,0)


/* Platform differences for string functions. */


// Windows is missing a few which are available on Unix/Linux platforms.
// We provide our own copies when building on Windows.

#if __WIN32__
char *strsep(char **stringp, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);
#endif

// Don't recall why I added this for everyone rather than only for Windows.
// Potential problem if some C library declares it a little differently.
char *strcasestr(const char *S, const char *FIND);


// cmake tries to determine whether strlcpy and strlcat are provided by the C runtime library.
//
//	../CMakeLists.txt:check_symbol_exists(strlcpy string.h HAVE_STRLCPY)
//
// It sets HAVE_STRLCPY and HAVE_STRLCAT if the corresponding functions are declared.
// Unfortunately this does not work right for glibc 2.38 which declares the functions
// like this:
//
//	extern __typeof (strlcpy) __strlcpy;
//	libc_hidden_proto (__strlcpy)
//	extern __typeof (strlcat) __strlcat;
//	libc_hidden_proto (__strlcat)
//
// Rather than the normal way found in earlier versions:
//
//	extern char *strcpy (char *__restrict __dest, const char *__restrict __src)
//
// Perhaps a later version of cmake will recognize this form but the version I'm
// using does not.
// So, our work around is to assume these functions are available for glibc >= 2.38.
//
// In theory, cmake should be able to find the version of the C runtime library, 
// but I could not get it to work.  So we have the test here.  We will still build
// own library with the strl... functions but this does not cause a problem
// because they have special debug names which will not cause a conflict.

#ifdef __GLIBC__
#if (__GLIBC__ > 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 38))
// These functions first added in 2.38.
//#warning "DEBUG - glibc >= 2.38"
#define HAVE_STRLCPY 1
#define HAVE_STRLCAT 1
#else
//#warning "DEBUG - glibc < 2.38"
#endif
#endif

#define DEBUG_STRL 1	// Extra Debug version when using our own strlcpy, strlcat.
			// Should be ignored if not supplying our own.

#ifndef HAVE_STRLCPY	// Need to supply our own.
#if DEBUG_STRL
#define strlcpy(dst,src,siz) strlcpy_debug(dst,src,siz,__FILE__,__func__,__LINE__)
size_t strlcpy_debug(char *__restrict__ dst, const char *__restrict__ src, size_t siz, const char *file, const char *func, int line);
#else
#define strlcpy(dst,src,siz) strlcpy_debug(dst,src,siz)
size_t strlcpy_debug(char *__restrict__ dst, const char *__restrict__ src, size_t siz);
#endif  /* DEBUG_STRL */
#endif

#ifndef HAVE_STRLCAT	// Need to supply our own.
#if DEBUG_STRL
#define strlcat(dst,src,siz) strlcat_debug(dst,src,siz,__FILE__,__func__,__LINE__)
size_t strlcat_debug(char *__restrict__ dst, const char *__restrict__ src, size_t siz, const char *file, const char *func, int line);
#else
#define strlcat(dst,src,siz) strlcat_debug(dst,src,siz)
size_t strlcat_debug(char *__restrict__ dst, const char *__restrict__ src, size_t siz);
#endif  /* DEBUG_STRL */
#endif


#endif   /* ifndef DIREWOLF_H */
