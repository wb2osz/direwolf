

/*------------------------------------------------------------------
 *
 * Module:      strlcpy.c
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



/*	$NetBSD: strlcpy.c,v 1.5 2014/10/31 18:59:32 spz Exp $	*/
/*	from	NetBSD: strlcpy.c,v 1.14 2003/10/27 00:12:42 lukem Exp	*/
/*	from OpenBSD: strlcpy.c,v 1.7 2003/04/12 21:56:39 millert Exp	*/

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

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>

#include "textcolor.h"

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */

#if DEBUG_STRL
size_t strlcpy_debug(char *__restrict__ dst, const char *__restrict__ src, size_t siz, const char *file, const char *func, int line)
#else
size_t strlcpy_debug(char *__restrict__ dst, const char *__restrict__ src, size_t siz)
#endif
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t retval;

#if DEBUG_STRL
	if (dst == NULL) {
		text_color_set (DW_COLOR_ERROR);
		dw_printf ("ERROR: strlcpy dst is NULL.  (%s %s %d)\n", file, func, line);
		return (0);
	}
	if (src == NULL) {
		text_color_set (DW_COLOR_ERROR);
		dw_printf ("ERROR: strlcpy src is NULL.  (%s %s %d)\n", file, func, line);
		return (0);
	}
	if (siz == 1 || siz == 4) {
		text_color_set (DW_COLOR_ERROR);
		dw_printf ("Suspicious strlcpy siz.  Is it using sizeof a pointer variable?  (%s %s %d)\n", file, func, line);
	}
#endif

	/* Copy as many bytes as will fit */
	if (n != 0 && --n != 0) {
		do {
			if ((*d++ = *s++) == 0)
				break;
		} while (--n != 0);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	retval = s - src - 1;	/* count does not include NUL */

#if DEBUG_STRL
	if (retval >= siz) {
		text_color_set (DW_COLOR_ERROR);
		dw_printf ("WARNING: strlcpy result length %d exceeds maximum length %d.  (%s %s %d)\n",
				(int)retval, (int)(siz-1), file, func, line);
	}
#endif
	return (retval);
}

