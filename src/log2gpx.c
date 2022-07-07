
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

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/*
 * Information we gather for each thing.
 */

typedef struct thing_s {
	double lat;
	double lon;
	float alt;		/* Meters above average sea level. */
	float course;
	float speed;		/* Meters per second. */
	char time[20+1+3];
	char name[9+1+2];
	char desc[32];		/* freq/offset/tone something like 146.955 MHz -600k PL 74.4 */
	char comment[80];	/* Combined mic-e status and comment text */
} thing_t;

static thing_t *things;		/* Dynamically sized array. */
static int max_things;		/* Current size. */
static int num_things;		/* Number of elements currently in use. */

#define UNKNOWN_VALUE (-999)	/* Special value to indicate unknown altitude, speed, course. */

#define KNOTS_TO_METERS_PER_SEC(x) ((x)*0.51444444444)


static void read_csv(FILE *fp);
static void unquote (char *in, char *out);
static int compar(const void *a, const void *b);
static void process_things (int first, int last);


int main (int argc, char *argv[]) 
{
	int first, last;
	

/*
 * Allocate array for data.
 * Expand it as needed if initial size is inadequate.
 */

	num_things = 0;
	max_things = 1000;
	things = malloc (max_things * sizeof(thing_t));

/*
 * Read files listed or stdin if none.
 */

	if (argc == 1) {
	  read_csv (stdin);
	}
	else {
	  int n;

	  for (n=1; n<argc; n++) {
	    if (strcmp(argv[n], "-") == 0) {
	      read_csv (stdin);
	    }
	    else {
	      FILE *fp;

	      fp = fopen (argv[n], "r");
	      if (fp != NULL) {
	        read_csv (fp);
	        fclose (fp);
	      }
	      else {
	        fprintf (stderr, "Can't open %s for read.\n", argv[n]);
	        exit (1);
	      }
	    }
	  }
	}
	
	if (num_things == 0) {
	  fprintf (stderr, "Nothing to process.\n");
	  exit (1);
	}

/*
 * Sort the data so everything for the same name is adjacent and
 * in order of time.
 */

	qsort (things, num_things, sizeof(thing_t), compar);

	//for (i=0; i<num_things; i++) {
	//  printf ("%d: %s %.6f %.6f %.1f %s\n", 
	//    i,
	//    things[i].time,
	//    things[i].lat,
	//    things[i].lon,
	//    things[i].alt,
	//    things[i].name);
	//}

/*
 * GPX file header.
 */
	printf ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
	printf ("<gpx version=\"1.1\" creator=\"Dire Wolf\">\n");

/*
 * Group together all records for the same entity.
 */
	last = first = 0;
	while (first < num_things) {

	  while (last < num_things-1 && strcmp(things[first].name, things[last+1].name) == 0) {
	    last++;
	  }
	  process_things (first, last);
	  first = last + 1;
	}

/*
 *  GPX file tail.
 */
	printf ("</gpx>\n");

	exit (0);
}


/*
 * Read from given file, already open, into things array. 
 */

static void read_csv(FILE *fp)
{
	char raw[500];
	char csv[500];
	int n;

	while (fgets(raw, sizeof(raw), fp) != NULL) {

	  char *next;

	  char *pchan;
	  char *putime;
	  char *pisotime;
	  char *psource;
	  char *pheard;
	  char *plevel;
	  char *perror;
	  char *pdti;
	  char *pname;
	  char *psymbol;
	  char *platitude;
	  char *plongitude;
	  char *pspeed;
	  char *pcourse;
	  char *paltitude;
	  char *pfreq;
	  char *poffset;
	  char *ptone;
	  char *psystem;
	  char *pstatus;
	  char *ptelemetry;
	  char *pcomment;


	  n = strlen(raw) - 1;
	  while (n >= 0 && (raw[n] == '\r' || raw[n] == '\n')) {
	    raw[n] = '\0';
	    n--;
	  }

	  unquote (raw, csv);
	 
	  //printf ("%s\n", csv);

/*
 * Separate out the fields.
 */	
    	  next = csv;
	  pchan = strsep(&next,"\t");
	  putime = strsep(&next,"\t");
	  pisotime = strsep(&next,"\t");
	  psource = strsep(&next,"\t");
	  pheard = strsep(&next,"\t");
	  plevel = strsep(&next,"\t");
	  perror = strsep(&next,"\t");
	  pdti = strsep(&next,"\t");
	  pname = strsep(&next,"\t");
	  psymbol = strsep(&next,"\t");
	  platitude = strsep(&next,"\t");
	  plongitude = strsep(&next,"\t");
	  pspeed = strsep(&next,"\t");		/* Knots, must convert. */
	  pcourse = strsep(&next,"\t");
	  paltitude = strsep(&next,"\t");	/* Meters, already correct units. */
	  pfreq = strsep(&next,"\t");
	  poffset = strsep(&next,"\t");
	  ptone = strsep(&next,"\t");
	  psystem = strsep(&next,"\t");
	  pstatus = strsep(&next,"\t");
	  ptelemetry = strsep(&next,"\t");	/* Currently unused.  Add to description? */
	  pcomment = strsep(&next,"\t");

	  /* Suppress the 'set but not used' warnings. */
	  /* Alternatively, we might use __attribute__((unused)) */

	  (void)(ptelemetry);
	  (void)(psystem);
	  (void)(psymbol);
	  (void)(pdti);
	  (void)(perror);
	  (void)(plevel);
	  (void)(pheard);
	  (void)(psource);
	  (void)(putime);


/*
 * Skip header line with names of fields.
 */
	  if (strcmp(pchan, "chan") == 0) {
	    continue;
	  }

/* 
 * Save only if we have valid data.
 * (Some packets don't contain a position.)
 */
	  if (pisotime != NULL && strlen(pisotime) > 0 &&
	      pname != NULL && strlen(pname) > 0 &&
	      platitude != NULL && strlen(platitude) > 0 &&
	      plongitude != NULL && strlen(plongitude) > 0) {
	
	    float speed = UNKNOWN_VALUE;
	    float course = UNKNOWN_VALUE;
	    float alt = UNKNOWN_VALUE;
	    char stemp[16], desc[32], comment[256];

	    if (pspeed != NULL && strlen(pspeed) > 0) {
	      speed = KNOTS_TO_METERS_PER_SEC(atof(pspeed));
	    }
	    if (pcourse != NULL && strlen(pcourse) > 0) {
	      course = atof(pcourse);
	    }
	    if (paltitude != NULL && strlen(paltitude) > 0) {
	      alt = atof(platitude);
	    }

/* combine freq/offset/tone into one description string. */

	    if (pfreq != NULL && strlen(pfreq) > 0) {
	      double freq = atof(pfreq);
	      snprintf (desc, sizeof(desc), "%.3f MHz", freq);
	    }
	    else {
	      strlcpy (desc, "", sizeof(desc));
	    }

	    if (poffset != NULL && strlen(poffset) > 0) {
	      int offset = atoi(poffset);
	      if (offset != 0 && offset % 1000 == 0) {
	        snprintf (stemp, sizeof(stemp), "%+dM", offset / 1000);
	      }
	      else {
	        snprintf (stemp, sizeof(stemp), "%+dk", offset);
	      }
	      if (strlen(desc) > 0) strlcat (desc, " ", sizeof(desc));
	      strlcat (desc, stemp, sizeof(desc));
	    }

	    if (ptone != NULL && strlen(ptone) > 0) {
	      if (*ptone == 'D') {
	        snprintf (stemp, sizeof(stemp), "DCS %s", ptone+1);
	      }
	      else {
	        snprintf (stemp, sizeof(stemp), "PL %s", ptone);
	      }
	      if (strlen(desc) > 0) strlcat (desc, " ", sizeof(desc));
	      strlcat (desc, stemp, sizeof(desc));
	    }

	    strlcpy (comment, "", sizeof(comment));
	    if (pstatus != NULL && strlen(pstatus) > 0) {
	      strlcpy (comment, pstatus, sizeof(comment));
	    }
	    if (pcomment != NULL && strlen(pcomment) > 0) {
	      if (strlen(comment) > 0) strlcat (comment, ", ", sizeof(comment));
	      strlcat (comment, pcomment, sizeof(comment));
	    }
	    
	    if (num_things == max_things) {
	      /* It's full.  Grow the array by 50%. */
  	      max_things += max_things / 2;
	      things = realloc (things, max_things*sizeof(thing_t));	
	    }

	    things[num_things].lat = atof(platitude);
	    things[num_things].lon = atof(plongitude);
	    things[num_things].speed = speed;
	    things[num_things].course = course;
	    things[num_things].alt = alt;
	    strlcpy (things[num_things].time, pisotime, sizeof(things[num_things].time));
	    strlcpy (things[num_things].name, pname, sizeof(things[num_things].name));
	    strlcpy (things[num_things].desc, desc, sizeof(things[num_things].desc));
	    strlcpy (things[num_things].comment, comment, sizeof(things[num_things].comment));

	    num_things++;
	  }
	}
}


/*
 * Compare function for use with qsort.
 * Order by name then date/time.
 */

static int compar(const void *a, const void *b)
{
	thing_t *ta = (thing_t *)a;
	thing_t *tb = (thing_t *)b;
	int n;

	n = strcmp(ta->name, tb->name);
	if (n != 0) 
	  return (n);
	return (strcmp(ta->time, tb->time)); 
}


/*
 * Take quoting out of CSV data.
 * Replace field separator commas with tabs while retaining 
 * commas that were part of the original data before quoting.
 */

static void unquote (char *in, char *out)
{
	char *p;
	char *q = out;		/* Mind your p's and q's */
	int quoted = 0;

	for (p=in; *p!='\0'; p++) {
	  if (*p == '"') {
	    if (p == in || ( !quoted && *(p-1) == ',')) {
	      /* " found at beginning of field */
	      quoted = 1;
	    }
	    else if (*(p+1) == '\0' || (quoted && *(p+1) == ',')) {
	      /* " found at end of field */
	      quoted = 0;
	    }
	    else {
	      /* " found somewhere in middle of field. */
	      /* We expect to be in quoted state and we should have a pair. */
	      if (quoted && *(p+1) == '"') {
	        /* Keep one and drop the other. */
	        *q++ = *p;
		p++;
	      }
	      else {
	        /* This shouldn't happen. */
	        fprintf (stderr, "CSV data quoting is messed up.\n");
	        *q++ = *p;
	      }
	    }
	  } 
	  else if (*p == ',') {
	    if (quoted) {
	      /* Comma in original data.  Keep it. */
	      *q++ = *p;
	    }
	    else {
	      /* Comma is field separator.  Replace with tab. */
	      *q++ = '\t';
	    }
	  }
	  else {
	    /* copy ordinary character. */
	    *q++ = *p;
	  }
	}
	*q = '\0';
}

/*
 * Prepare text values for XML.
 * Replace significant characters with "predefined entities."
 */

static void xml_text (char *in, char *out)
{
	char *p, *q;

	q = out;
	for (p = in; *p != '\0'; p++) {
	  if (*p == '"') {
	    *q++ = '&';
	    *q++ = 'q';
	    *q++ = 'u';
	    *q++ = 'o';
	    *q++ = 't';
	    *q++ = ';';
	  }
	  else if (*p == '&') {
	    *q++ = '&';
	    *q++ = 'a';
	    *q++ = 'm';
	    *q++ = 'p';
	    *q++ = ';';
	  }
	  else if (*p == '\'') {
	    *q++ = '&';
	    *q++ = 'a';
	    *q++ = 'p';
	    *q++ = 'o';
	    *q++ = 's';
	    *q++ = ';';
	  }
	  else if (*p == '<') {
	    *q++ = '&';
	    *q++ = 'l';
	    *q++ = 't';
	    *q++ = ';';
	  }
	  else if (*p == '>') {
	    *q++ = '&';
	    *q++ = 'g';
	    *q++ = 't';
	    *q++ = ';';
	  }
	  else {
	    *q++ = *p;
	  }
	}
	*q = '\0';
}


/*
 * Process all things with the same name.
 * They should be sorted by time.
 * For stationary entities, generate just one GPX waypoint.
 * For moving entities, generate a GPX track.
 */

static void process_things (int first, int last)
{
	//printf ("process %d to %d\n", first, last);
	int i;
	int moved = 0;
	char safe_name[30];
	char safe_comment[120];

	for (i=first+1; i<=last; i++) {
	  if (things[i].lat != things[first].lat) moved = 1;
	  if (things[i].lon != things[first].lon) moved = 1;
	}

	if (moved) {

/*
 * Generate track for moving thing.
 */
	  xml_text (things[first].name, safe_name);
	  xml_text (things[first].comment, safe_comment);

	  printf ("  <trk>\n");
	  printf ("    <name>%s</name>\n", safe_name);
	  printf ("    <trkseg>\n");

	  for (i=first; i<=last; i++) {
	    printf ("      <trkpt lat=\"%.6f\" lon=\"%.6f\">\n", things[i].lat, things[i].lon);
	    if (things[i].speed != UNKNOWN_VALUE) {
	      printf ("        <speed>%.1f</speed>\n", things[i].speed);
	    }
	    if (things[i].course != UNKNOWN_VALUE) {
	      printf ("        <course>%.1f</course>\n", things[i].course);
	    }
	    if (things[i].alt != UNKNOWN_VALUE) {
	      printf ("        <ele>%.1f</ele>\n", things[i].alt);
	    }
	    if (strlen(things[i].desc) > 0) {
	      printf ("        <desc>%s</desc>\n", things[i].desc);
	    }
	    if (strlen(safe_comment) > 0) {
	      printf ("        <cmt>%s</cmt>\n", safe_comment);
	    }
	    printf ("        <time>%s</time>\n", things[i].time);
	    printf ("      </trkpt>\n");
	  }

	  printf ("    </trkseg>\n");
	  printf ("  </trk>\n");

	  /* Also generate waypoint for last location. */
	}

	// Future possibility?
	// <sym>Symbol Name</sym>	-- not standardized.

/*
 * Generate waypoint for stationary thing or last known position for moving thing.
 */
	xml_text (things[last].name, safe_name);
	xml_text (things[last].comment, safe_comment);

	printf ("  <wpt lat=\"%.6f\" lon=\"%.6f\">\n", things[last].lat, things[last].lon);
	if (things[last].alt != UNKNOWN_VALUE) {
	  printf ("    <ele>%.1f</ele>\n", things[last].alt);
	}
	if (strlen(things[i].desc) > 0) {
	  printf ("    <desc>%s</desc>\n", things[i].desc);
	}
	if (strlen(safe_comment) > 0) {
	  printf ("    <cmt>%s</cmt>\n", safe_comment);
	}
	printf ("    <name>%s</name>\n", safe_name);
	printf ("  </wpt>\n");
}
