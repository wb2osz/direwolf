/* UTM to Latitude / Longitude conversion */

#include "direwolf.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

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
	char szone[100];
	long lzone;
	char *zlet;
	char hemi;
	long err;
	char message[300];


	if (argc == 4) {

// 3 command line arguments for UTM

	  strlcpy (szone, argv[1], sizeof(szone));
	  lzone = strtoul(szone, &zlet, 10);

	  if (*zlet == '\0') {
	    hemi = 'N';
	  }
	  else {
	  
	    if (islower(*zlet)) {
	      *zlet = toupper(*zlet);
	    }
	    if (strchr ("CDEFGHJKLMNPQRSTUVWX", *zlet) == NULL) {
	      fprintf (stderr, "Latitudinal band must be one of CDEFGHJKLMNPQRSTUVWX.\n\n");
	      usage();
	    }
	    if (*zlet >= 'N') {
	      hemi = 'N';
	    }
	    else {
	      hemi = 'S';
	    }
	  }
  
	  easting = atof(argv[2]);

	  northing = atof(argv[3]);

	  err = Convert_UTM_To_Geodetic(lzone, hemi, easting, northing, &lat, &lon);
	  if (err == 0) {
	    lat = R2D(lat);
	    lon = R2D(lon);

	    printf ("from UTM, latitude = %.6f, longitude = %.6f\n", lat, lon);
	  }
	  else {

	    utm_error_string (err, message);
	    fprintf (stderr, "Conversion from UTM failed:\n%s\n\n", message);

	  }
	}
	else if (argc == 2) {

// One command line argument, USNG or MGRS.

// TODO: continue here.


	  err = Convert_USNG_To_Geodetic (argv[1], &lat, &lon);
 	  if (err == 0) {
	    lat = R2D(lat);
	    lon = R2D(lon);
	    printf ("from USNG, latitude = %.6f, longitude = %.6f\n", lat, lon);
	  }
	  else {
	    usng_error_string (err, message);
	    fprintf (stderr, "Conversion from USNG failed:\n%s\n\n", message);
	  }

	  err = Convert_MGRS_To_Geodetic (argv[1], &lat, &lon);
 	  if (err == 0) {
	    lat = R2D(lat);
	    lon = R2D(lon);
	    printf ("from MGRS, latitude = %.6f, longitude = %.6f\n", lat, lon);
	  }
	  else {
	    mgrs_error_string (err, message);
	    fprintf (stderr, "Conversion from MGRS failed:\n%s\n\n", message);
	  }

	}
	else {
	  usage();
	}

	exit (0);
}


static void usage (void)
{
	fprintf (stderr, "UTM to Latitude / Longitude conversion\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "Usage:\n");
	fprintf (stderr, "\tutm2ll  zone  easting  northing\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "where,\n");
	fprintf (stderr, "\tzone is UTM zone 1 thru 60 with optional latitudinal band.\n");
	fprintf (stderr, "\teasting is x coordinate in meters\n");
	fprintf (stderr, "\tnorthing is y coordinate in meters\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "or:\n");
	fprintf (stderr, "\tutm2ll  x\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "where,\n");
	fprintf (stderr, "\tx is USNG or MGRS location.\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "Examples:\n");
	fprintf (stderr, "\tutm2ll 19T 306130 4726010\n");
	fprintf (stderr, "\tutm2ll 19TCH06132600\n");

	exit (1);
}