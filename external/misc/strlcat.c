

/*------------------------------------------------------------------
 *
 * Module:      strlcat.c
 *
 * Purpose:   	Safe string functions to guard against buffer overflow.
 *			
 * Description:	The size of character strings, especially when coming from the 
 *		outside, can sometimes exceed a fixed size storage area.  
 *
 *		There was one case where a MIC-E format packet had an enormous
 *		comment that exceeded an internal buffer of 256 characters,
 *		resulting in a crash.
 *
 *		We are not always meticulous about checking sizes to avoid overflow.
 *		Use of these functions, instead of strcpy and strcat, should
 *		help avoid issues.
 *
 * Orgin:	From OpenBSD as the copyright notice indicates.
 *		The GNU folks didn't think it was appropriate for inclusion 
 *		in glibc.     https://lwn.net/Articles/507319/
 *
 * Modifications:	Added extra debug output when strings are truncated.
 *			Not sure if I will leave this in the release version
 *			or just let it happen silently.		
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>

#include "textcolor.h"


/*	$NetBSD: strlcat.c,v 1.5 2014/10/31 18:59:32 spz Exp $	*/
/*	from	NetBSD: strlcat.c,v 1.16 2003/10/27 00:12:42 lukem Exp	*/
/*	from OpenBSD: strlcat.c,v 1.10 2003/04/12 21:56:39 millert Exp	*/

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND TODD C. MILLER DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL TODD C. MILLER BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */



/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */

#if DEBUG_STRL
size_t strlcat_debug(char *__restrict__ dst, const char *__restrict__ src, size_t siz, const char *file, const char *func, int line)
#else
size_t strlcat_debug(char *__restrict__ dst, const char *__restrict__ src, size_t siz)
#endif
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t dlen;
	size_t retval;

#if DEBUG_STRL
	if (dst == NULL) {
		text_color_set (DW_COLOR_ERROR);
		dw_printf ("ERROR: strlcat dst is NULL.  (%s %s %d)\n", file, func, line);
		return (0);
	}
	if (src == NULL) {
		text_color_set (DW_COLOR_ERROR);
		dw_printf ("ERROR: strlcat src is NULL.  (%s %s %d)\n", file, func, line);
		return (0);
	}
	if (siz == 1 || siz == 4) {
		text_color_set (DW_COLOR_ERROR);
		dw_printf ("Suspicious strlcat siz.  Is it using sizeof a pointer variable?  (%s %s %d)\n", file, func, line);
	}
#endif

	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;

	if (n == 0) {
		retval = dlen + strlen(s);
		goto the_end;
	}
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';

	retval = dlen + (s - src);	/* count does not include NUL */
the_end:

#if DEBUG_STRL
	if (retval >= siz) {
		text_color_set (DW_COLOR_ERROR);
		dw_printf ("WARNING: strlcat result length %d exceeds maximum length %d.  (%s %s %d)\n",
				(int)retval, (int)(siz-1), file, func, line);
	}
#endif
	return (retval);
}