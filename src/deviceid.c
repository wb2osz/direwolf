//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2023, 2025  John Langner, WB2OSZ
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
 * File:	deviceid.c
 *
 * Purpose:	Determine the device identifier from the destination field,
 *		or from prefix/suffix for MIC-E format.
 *
 * Description: Orginally this used the tocalls.txt file and was part of decode_aprs.c.
 *		For release 1.8, we use tocalls.yaml and this is split into a separate file.
 *
 *------------------------------------------------------------------*/

//#define TEST 1		// Standalone test.   $ gcc -DTEST deviceid.c && ./a.out


#if TEST
#define HAVE_STRLCPY 1		// prevent defining in direwolf.h
#define HAVE_STRLCAT 1
#endif

#include "direwolf.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "deviceid.h"
#include "textcolor.h"


static void unquote (int line, char *pin, char *pout);
static int tocall_cmp (const void *px, const void *py);
static int mice_cmp (const void *px, const void *py);

static void deviceid_term(void);

/*------------------------------------------------------------------
 *
 * Function:	main
 *
 * Purpose:	A little self-test used during development.
 *
 * Description:	Read the yaml file.  Decipher a few typical values.
 *
 *------------------------------------------------------------------*/

#if TEST
// So we don't need to link with any other files.
#define dw_printf printf
void text_color_set(dw_color_t)  { return; }
void strlcpy(char *dst, char *src, size_t dlen) {
	strcpy (dst, src);
}
void strlcat(char *dst, char *src, size_t dlen) {
	strcat (dst, src);
}


int main (int argc, char *argv[])
{
	char device[80];
	char comment_out[80];

	deviceid_init ();

	dw_printf ("\n");
	dw_printf ("Testing ...\n");

// MIC-E Legacy (really Kenwood).

	deviceid_decode_mice (">Comment", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "Comment") == 0);
	assert (strcmp(device, "Kenwood TH-D7A") == 0);

	deviceid_decode_mice (">Comment^", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "Comment") == 0);
	assert (strcmp(device, "Kenwood TH-D74") == 0);

	deviceid_decode_mice ("]Comment", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "Comment") == 0);
	assert (strcmp(device, "Kenwood TM-D700") == 0);

	deviceid_decode_mice ("]Comment=", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "Comment") == 0);
	assert (strcmp(device, "Kenwood TM-D710") == 0);

	deviceid_decode_mice ("]\"4V}=", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "\"4V}") == 0);
	assert (strcmp(device, "Kenwood TM-D710") == 0);


// Modern MIC-E.

	deviceid_decode_mice ("`Comment_\"", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "Comment") == 0);
	assert (strcmp(device, "Yaesu FTM-350") == 0);

	deviceid_decode_mice ("`Comment_ ", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "Comment") == 0);
	assert (strcmp(device, "Yaesu VX-8") == 0);

	deviceid_decode_mice ("'Comment|3", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "Comment") == 0);
	assert (strcmp(device, "Byonics TinyTrak3") == 0);

	deviceid_decode_mice ("Comment", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "Comment") == 0);
	assert (strcmp(device, "UNKNOWN vendor/model") == 0);

	deviceid_decode_mice ("", comment_out, sizeof(comment_out), device, sizeof(device));
	dw_printf ("%s %s\n", comment_out, device);
	assert (strcmp(comment_out, "") == 0);
	assert (strcmp(device, "UNKNOWN vendor/model") == 0);

// Tocall

	deviceid_decode_dest ("APDW18", device, sizeof(device));
	dw_printf ("%s\n", device);
	assert (strcmp(device, "WB2OSZ DireWolf") == 0);

	deviceid_decode_dest ("APD123", device, sizeof(device));
	dw_printf ("%s\n", device);
	assert (strcmp(device, "Open Source aprsd") == 0);

	// null for Vendor.
	deviceid_decode_dest ("APAX", device, sizeof(device));
	dw_printf ("%s\n", device);
	assert (strcmp(device, "AFilterX") == 0);

	deviceid_decode_dest ("APA123", device, sizeof(device));
	dw_printf ("%s\n", device);
	assert (strcmp(device, "UNKNOWN vendor/model") == 0);

	dw_printf ("\n");
	dw_printf ("Success!\n");

	exit (EXIT_SUCCESS);
}

#endif  // TEST



// Structures to hold mapping from encoded form to vendor and model.
// The .yaml file has two separate sections for MIC-E but they can
// both be handled as a single more general case.

struct mice {
	char prefix[4];		// The legacy form has 1 prefix character.
				// The newer form has none.  (more accurately ` or ')
	char suffix[4];		// The legacy form has 0 or 1.
				// The newer form has 2.
	char *vendor;
	char *model;
};

struct tocalls {
	char tocall[8];		// Up to 6 characters.  Some may have wildcards at the end.
				// Most often they are trailing "??" or "?" or "???" in one case.
				// Sometimes there is trailing "nnn".  Does that imply digits only?
				// Sometimes we see a trailing "*".  Is "*" different than "?"?
				// There are a couple bizzare cases like APnnnD which can
				// create an ambigious situation. APMPAD, APRFGD, APY0[125]D.
				// Screw them if they can't follow the rules.  I'm not putting in a special case.
	char *vendor;
	char *model;
};


static struct mice *pmice = NULL;	// Pointer to array.
static int mice_count = 0;		// Number of allocated elements.
static int mice_index = -1;		// Current index for filling in.

static struct tocalls *ptocalls = NULL;	// Pointer to array.
static int tocalls_count = 0;		// Number of allocated elements.
static int tocalls_index = -1;		// Current index for filling in.




/*------------------------------------------------------------------
 *
 * Function:	deviceid_init
 *
 * Purpose:	Called once at startup to read the tocalls.yaml file which was obtained from
 *		https://github.com/aprsorg/aprs-deviceid .
 *
 * Inputs:	tocalls.yaml with OS specific directory search list.
 *
 * Outputs:	static variables listed above.
 *
 * Description:	For maximum flexibility, we will read the
 *		data file at run time rather than compiling it in.
 *
 *------------------------------------------------------------------*/

// Make sure the array is null terminated.
// If search order is changed, do the same in symbols.c for consistency.
// fopen is perfectly happy with / in file path when running on Windows.

static const char *search_locations[] = {
	(const char *) "tocalls.yaml",			// Current working directory
	(const char *) "data/tocalls.yaml",		// Windows with CMake
	(const char *) "../data/tocalls.yaml",		// Source tree
#ifndef __WIN32__
	(const char *) "/usr/local/share/direwolf/tocalls.yaml",
	(const char *) "/usr/share/direwolf/tocalls.yaml",
#endif
#if __APPLE__
	// https://groups.yahoo.com/neo/groups/direwolf_packet/conversations/messages/2458
	// Adding the /opt/local tree since macports typically installs there.  Users might want their
	// INSTALLDIR (see Makefile.macosx) to mirror that.  If so, then we need to search the /opt/local
	// path as well.
	(const char *) "/opt/local/share/direwolf/tocalls.yaml",
#endif
	(const char *) NULL		// Important - Indicates end of list.
};


void deviceid_init(void)
{
	FILE *fp = NULL;
	for (int n = 0; search_locations[n] != NULL && fp == NULL; n++) {
#if TEST
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Trying %s\n", search_locations[n]);
#endif
	  fp = fopen(search_locations[n], "r");
#if TEST
	  if (fp != NULL) {
	    dw_printf ("Opened %s\n", search_locations[n]);
	  }
#endif
	};

	if (fp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Could not open any of these file locations:\n");
	  for (int n = 0; search_locations[n] != NULL; n++) {
	    dw_printf ("    %s\n", search_locations[n]);
	  }
	  dw_printf("It won't be possible to extract device identifiers from packets.\n");
	  return;
	};

// Read file first time to get number of items.
// Allocate required space.
// Rewind.
// Read file second time to gather data.

	enum { no_section, mice_section, tocalls_section} section = no_section;
	char stuff[80];

	for (int pass = 1; pass <=2; pass++) {
	 int line = 0;		// Line number within file.

	 while (fgets(stuff, sizeof(stuff), fp)) {
	  line++;

	  // Remove trailing CR/LF or spaces.
	  char *p = stuff + strlen(stuff) - 1;
	  while (p >= (char*)stuff && (*p == '\r' || *p == '\n' || *p == ' ')) {
	    *p-- = '\0';
	  }

	  // Ignore comment lines.
	  if (stuff[0] == '#') {
	    continue;
	  }

#if TEST
	  //dw_printf ("%d: %s\n", line, stuff);
#endif
	  // This is not very robust; everything better be in exactly the right format.
	  // TODO: Be more forgiving.

	  if (strncmp(stuff, "mice:", strlen("mice:")) == 0) {
	    section = mice_section;
#if TEST
	    dw_printf ("Pass %d, line %d, MIC-E section\n", pass, line);
#endif
	  }
	  else if (strncmp(stuff, "micelegacy:", strlen("micelegacy:")) == 0) {
	    section = mice_section;  // treat both same.
#if TEST
	    dw_printf ("Pass %d, line %d, Legacy MIC-E section\n", pass, line);
#endif
	  }
	  else if (strncmp(stuff, "tocalls:", strlen("tocalls:")) == 0) {
	    section = tocalls_section;
#if TEST
	    dw_printf ("Pass %d, line %d, TOCALLS section\n", pass, line);
#endif
	  }

	  // The first property of an item is preceded by " - ".

	  if (pass == 1 && strncmp(stuff, " - ", 3) == 0) {
	    switch (section) {
	      case no_section:						break;
	      case mice_section:	mice_count++;			break;
	      case tocalls_section:	tocalls_count++;		break;
	    }
	  }

	  if (pass == 2) {
	    switch (section) {
	      case no_section:
	        break;

	      case mice_section:
	        if (strncmp(stuff, " - ", 3) == 0) {
	          mice_index++;
	          assert (mice_index >= 0 && mice_index < mice_count);
	        }
	        if (strncmp(stuff+3, "prefix: ", strlen("prefix: ")) == 0) {
	          unquote (line, stuff+3+8, pmice[mice_index].prefix);  
	        }
	        else if (strncmp(stuff+3, "suffix: ", strlen("suffix: ")) == 0) {
	          unquote (line, stuff+3+8, pmice[mice_index].suffix);  
	        }
	        else if (strncmp(stuff+3, "vendor: ", strlen("vendor: ")) == 0) {
	          pmice[mice_index].vendor = strdup(stuff+3+8);  
	        }
	        else if (strncmp(stuff+3, "model: ", strlen("model: ")) == 0) {
	          pmice[mice_index].model = strdup(stuff+3+7);  
	        }
	        break;

	      case tocalls_section:
	        if (strncmp(stuff, " - ", 3) == 0) {
	          tocalls_index++;
	          assert (tocalls_index >= 0 && tocalls_index < tocalls_count);
	        }
	        if (strncmp(stuff+3, "tocall: ", strlen("tocall: ")) == 0) {
	          // Remove trailing wildcard characters ? * n
	          // "APZ*" has quotes around it, inconsistent with everything else.
	          char *r = stuff + strlen(stuff) - 1;
	          while (r >= (char*)stuff && (*r == '?' || *r == '*' || *r == 'n' || *r == '"')) {
	            *r-- = '\0';
	          }

	          if (stuff[3+8] == '"') {
	            strlcpy (ptocalls[tocalls_index].tocall, stuff+3+8 +1, sizeof(ptocalls[tocalls_index].tocall));
	          }
	          else {
	            strlcpy (ptocalls[tocalls_index].tocall, stuff+3+8, sizeof(ptocalls[tocalls_index].tocall));
	          }

	          // Remove trailing CR/LF or spaces.
	          char *p = stuff + strlen(stuff) - 1;
	          while (p >= (char*)stuff && (*p == '\r' || *p == '\n' || *p == ' ')) {
	            *p-- = '\0';
	          }
	        }
	        else if (strncmp(stuff+3, "vendor: ", strlen("vendor: ")) == 0) {
	          ptocalls[tocalls_index].vendor = strdup(stuff+3+8);
	        }
	        else if (strncmp(stuff+3, "model: ", strlen("model: ")) == 0) {
	          ptocalls[tocalls_index].model = strdup(stuff+3+7);
	        }
	        break;
	    }
	  }
	 } // while(fgets

	 if (pass == 1) {
#if TEST
	  dw_printf ("deviceid sizes %d %d\n", mice_count, tocalls_count);
#endif
	  pmice = calloc(sizeof(struct mice), mice_count);
	  ptocalls = calloc(sizeof(struct tocalls), tocalls_count);

	  rewind (fp);
	  section = no_section;
	 }
	} // for pass = 1 or 2

	fclose (fp);

	assert (mice_index == mice_count - 1);
	assert (tocalls_index == tocalls_count - 1);


// MIC-E Legacy needs to be sorted so those with suffix come first.

	qsort (pmice, mice_count, sizeof(struct mice), mice_cmp);


// Sort tocalls by decreasing length so the search will go from most specific to least specific.
// Example:  APY350 or APY008 would match those specific models before getting to the more generic APY.

	qsort (ptocalls, tocalls_count, sizeof(struct tocalls), tocall_cmp);

#if TEST
	dw_printf ("MIC-E:\n");
	for (int i = 0; i < mice_count; i++) {
	  dw_printf ("%s %s %s %s\n", pmice[i].prefix, pmice[i].suffix, pmice[i].vendor, pmice[i].model);
	}
	dw_printf ("TOCALLS:\n");
	for (int i = 0; i < tocalls_count; i++) {
	  dw_printf ("%s %s %s\n", ptocalls[i].tocall, ptocalls[i].vendor, ptocalls[i].model);
	}
#endif

	atexit (deviceid_term);
	return;

} // end deviceid_init


/*------------------------------------------------------------------
 *
 * Function:	deviceid_term
 *
 * Purpose:	Called when exiting to cleanup.
 *
 * In/Out:	pmice
 *		mice_count
 *		ptocalls
 *		tocalls_count
 *
 * Description:	Free all the allocated memory.
 *
 * Mystery:	Why does Address Sanitizer complain about a data leak
 *		for 62 strdups?
 *
 *------------------------------------------------------------------*/

static void deviceid_term(void)
{
	for (int n = 0; n < tocalls_count; n++) {
	  if (ptocalls[n].model != NULL) free (ptocalls[n].model);
	  if (ptocalls[n].vendor != NULL) free (ptocalls[n].vendor);
	}
	tocalls_count = 0;
	free (ptocalls);
	ptocalls = NULL;

	for (int n = 0; n < mice_count; n++) {
	  if (pmice[n].model != NULL) free (pmice[n].model);
	  if (pmice[n].vendor != NULL) free (pmice[n].vendor);
	}
	mice_count = 0;
	free (pmice);
	pmice = NULL;
}


/*------------------------------------------------------------------
 *
 * Function:	unquote
 *
 * Purpose:	Remove surrounding quotes and undo any escapes.
 *
 * Inputs:	line - File line number for error message.
 *
 *		in - String with quotes. Might contain \ escapes.
 *
 * Outputs:	out - Quotes and escapes removed.
 *			Limited to 2 characters to avoid buffer overflow.
 *
 * Examples:	in	out
 *		"_#"	_#
 *		"_\""	_"
 *		"="	=
 *
 *------------------------------------------------------------------*/

static void unquote (int line, char *pin, char *pout)
{
	int count = 0;

	*pout = '\0';
	if (*pin != '"') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Missing leading \" for %s on line %d.\n", pin, line);
	  return;
	}

	pin++;
	while (*pin != '\0' && *pin != '\"' && count < 2) {
	  if (*pin == '\\') {
	    pin++;
	  }
	  *pout++ = *pin++;
	  count++;
	}
	*pout = '\0';

	if (*pin != '"') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Missing trailing \" or string too long on line %d.\n", line);
	  return;
	}
}

// Used to sort the tocalls by length.
// When length is equal, alphabetically.

static int tocall_cmp (const void *px, const void *py)
{
	const struct tocalls *x = (struct tocalls *)px;
	const struct tocalls *y = (struct tocalls *)py;

	if (strlen(x->tocall) != strlen(y->tocall)) {
	  return (strlen(y->tocall) - strlen(x->tocall));
	}
	return (strcmp(x->tocall, y->tocall));
}

// Used to sort the suffixes by length.
// Longer at the top.
// Example check for  >xxx^ before >xxx .

static int mice_cmp (const void *px, const void *py)
{
	const struct mice *x = (struct mice *)px;
	const struct mice *y = (struct mice *)py;

	return (strlen(y->suffix) - strlen(x->suffix));
}





/*------------------------------------------------------------------
 *
 * Function:	deviceid_decode_dest
 *
 * Purpose:	Find vendor/model for destination address of form APxxxx.
 *
 * Inputs:	dest	- Destination address.  No SSID.
 *
 *		device_size - Amount of space available for result to avoid buffer overflow.
 *
 * Outputs:	device	- Vendor and model.
 *
 * Description:	With the exception of MIC-E format, we expect to find the vendor/model in the
 *		AX.25 destination field.   The form should be APxxxx.
 *
 *		Search the list looking for the maximum length match.
 *
 *		tocalls was sorted by decreasing length so the search will go from
 *		most specific to least specific.
 *		Example:  APY350 or APY008 would match those specific models before
 *		getting to the more generic APY.
 *
 *------------------------------------------------------------------*/

void deviceid_decode_dest (char *dest, char *device, size_t device_size)
{
	if (ptocalls == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("deviceid_decode_dest called without any deviceid data.\n");
	  strlcpy (device, "Internal error - UNKNOWN vendor/model", device_size);
	  return;
	}

	*device = '\0';

	for (int n = 0; n < tocalls_count; n++) {
	  if (strncmp(dest, ptocalls[n].tocall, strlen(ptocalls[n].tocall)) == 0) {

	    if (ptocalls[n].vendor != NULL && ptocalls[n].model != NULL) {	// both vendor & model
	      snprintf (device, device_size, "%s %s", ptocalls[n].vendor, ptocalls[n].model);
	    }

	    else if (ptocalls[n].vendor != NULL) {	// only vendor
	      strlcpy (device, ptocalls[n].vendor, device_size);
	    }

	    else if (ptocalls[n].model != NULL) {	// only model
	      strlcpy (device, ptocalls[n].model, device_size);
	    }

	    else {	// found in table but no vendor or model
	      break;
	    }

	    return;
	  }
	}

// Not found in table.  Or found but both vendor and model are missing.

	strlcpy (device, "UNKNOWN vendor/model", device_size);
	return;

} // end deviceid_decode_dest


/*------------------------------------------------------------------
 *
 * Function:	deviceid_decode_mice
 *
 * Purpose:	Find vendor/model for MIC-E comment.
 *
 * Inputs:	comment - MIC-E comment that might have vendor/model encoded as
 *			a prefix and/or suffix.
 *			Any trailing CR has already been removed.
 *
 *		trimmed_size - Amount of space available for result to avoid buffer overflow.
 *
 *		device_size - Amount of space available for result to avoid buffer overflow.
 *
 * Outputs:	trimmed - Final comment with device vendor/model removed.
 *				This would include any altitude.
 *
 *		device	- Vendor and model.
 *
 * Description:	MIC-E device identification has a tortured history.
 *
 *		The Kenwood TH-D7A  put ">" at the beginning of the comment.
 *		The Kenwood TM-D700 put "]" at the beginning of the comment.
 *		Later Kenwood models also added a single suffix character
 *		using a character very unlikely to appear at the end of a comment.
 *
 *		The later convention, used by everyone else, is to have a prefix of ` or '
 *		and a suffix of two characters.  The suffix characters need to be
 *		something very unlikely to be found at the end of a comment.
 *
 *		A receiving device is expected to remove those extra characters
 *		before displaying the comment.
 *
 * References:	http://www.aprs.org/aprs12/mic-e-types.txt
 *		http://www.aprs.org/aprs12/mic-e-examples.txt
 *		https://github.com/wb2osz/aprsspec containing:
 *			APRS Protocol Specification 1.2
 *			Understanding APRS Packets
 *
 *------------------------------------------------------------------*/

// The strncmp documentation doesn't mention behavior if length is zero.
// Do our own just to be safe.

static inline int strncmp_z (char *a, char *b, size_t len)
{
	int result = 0;
	if (len > 0) {
	  result = strncmp(a, b, len);
	}
	//dw_printf ("Comparing '%s' and '%s' len %d result %d\n", a, b, len, result);
	return result;
}

void deviceid_decode_mice (char *comment, char *trimmed, size_t trimmed_size, char *device, size_t device_size)
{
	strlcpy (device, "UNKNOWN vendor/model", device_size);
	strlcpy (trimmed, comment, trimmed_size);
	if (strlen(comment) < 1) {
	  return;
	}

	if (ptocalls == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("deviceid_decode_mice called without any deviceid data.\n");
	  return;
	}


// The Legacy format has an explicit prefix in the table.
// For others, it must be ` or ' to indicate whether messaging capable.

	for (int n = 0; n < mice_count; n++) {
	  if ((strlen(pmice[n].prefix) != 0 &&					// Legacy
	      strncmp_z(comment, 						// prefix from table
		  pmice[n].prefix,
	          strlen(pmice[n].prefix)) == 0 &&
	      strncmp_z(comment + strlen(comment) - strlen(pmice[n].suffix),	// possible suffix
		pmice[n].suffix,
	        strlen(pmice[n].suffix)) == 0) ||

	     (strlen(pmice[n].prefix) == 0 &&					// Later
	      (comment[0] == '`' || comment[0] == '\'')	&&			// prefix ` or '
	      strncmp_z(comment + strlen(comment) - strlen(pmice[n].suffix),	// suffix
		pmice[n].suffix,
	        strlen(pmice[n].suffix)) == 0)  ) {

	    if (pmice[n].vendor != NULL) {
	      strlcpy (device, pmice[n].vendor, device_size);
	    }

	    if (pmice[n].vendor != NULL && pmice[n].model != NULL) {
	      strlcat (device, " ", device_size);
	    }

	    if (pmice[n].model != NULL) {
	      strlcat (device, pmice[n].model, device_size);
	    }

	    // Remove any prefix/suffix and return what remains.

	    strlcpy (trimmed, comment + 1, trimmed_size);
	    trimmed[strlen(comment) - 1 - strlen(pmice[n].suffix)] = '\0';

	    return;
	  }
	}


// Not found.

	strlcpy (device, "UNKNOWN vendor/model", device_size);
	strlcpy (trimmed, comment, trimmed_size);

} // end deviceid_decode_mice

// end deviceid.c
