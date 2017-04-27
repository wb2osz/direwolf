/* Latitude / Longitude to UTM conversion */

#include "direwolf.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>


#include "utm.h"
#include "mgrs.h"
#include "usng.h"
#include "error_string.h"


#define D2R(d) ((d) * M_PI / 180.)
#define R2D(r) ((r) * 180. / M_PI)


static void usage();


int main (int argc, char *argv[]) 
{
	double easting;
	double northing;
	double lat, lon;
	char mgrs[32];
	char usng[32];
	char hemisphere;
	long lzone;
	long err;
	char message[300];


	if (argc != 3) usage();

        lat = atof(argv[1]);

        lon = atof(argv[2]);


// UTM

	err = Convert_Geodetic_To_UTM (D2R(lat), D2R(lon), &lzone, &hemisphere, &easting, &northing);
        if (err == 0) {                      
	  printf ("UTM zone = %ld, hemisphere = %c, easting = %.0f, northing = %.0f\n", lzone, hemisphere, easting, northing);
	}
	else {
	  utm_error_string (err, message);
	  fprintf (stderr, "Conversion to UTM failed:\n%s\n\n", message);
	
	  // Others could still succeed, keep going.
	}


// Practice run with MGRS to see if it will succeed

        err = Convert_Geodetic_To_MGRS (D2R(lat), D2R(lon), 5L, mgrs);
	if (err == 0) {

// OK, hope changing precision doesn't make a difference.

	  long precision;

	  printf ("MGRS =");
          for (precision = 1; precision <= 5; precision++) {
            Convert_Geodetic_To_MGRS (D2R(lat), D2R(lon), precision, mgrs);
            printf ("  %s", mgrs);
          }
	  printf ("\n");
	}
	else {
	  mgrs_error_string (err, message);
	  fprintf (stderr, "Conversion to MGRS failed:\n%s\n", message);
	}

// Same for USNG.

        err = Convert_Geodetic_To_USNG (D2R(lat), D2R(lon), 5L, usng);
	if (err == 0) {

	  long precision;

	  printf ("USNG =");
          for (precision = 1; precision <= 5; precision++) {
            Convert_Geodetic_To_USNG (D2R(lat), D2R(lon), precision, usng);
            printf ("  %s", usng);
          }
	  printf ("\n");
	}
	else {
	  usng_error_string (err, message);
	  fprintf (stderr, "Conversion to USNG failed:\n%s\n", message);
	}

	exit (0);
}


static void usage (void)
{
	fprintf (stderr, "Latitude / Longitude to UTM conversion\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "Usage:\n");
	fprintf (stderr, "\tll2utm  latitude  longitude\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "where,\n");
	fprintf (stderr, "\tLatitude and longitude are in decimal degrees.\n");
	fprintf (stderr, "\t   Use negative for south or west.\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "Example:\n");
	fprintf (stderr, "\tll2utm 42.662139 -71.365553\n");

	exit (1);
}