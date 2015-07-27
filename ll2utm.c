/* Latitude / Longitude to UTM conversion */

#include <stdio.h>
#include <stdlib.h>

#include "LatLong-UTMconversion.h"


static void usage();


void main (int argc, char *argv[]) 
{
	double easting;
	double northing;
	double lat, lon;
	char zone[8];

	if (argc != 3) usage();


	lat = atof(argv[1]);
	if (lat < -90 || lat > 90) {
	  fprintf (stderr, "Latitude value is out of range.\n\n");
	  usage();
	}

	lon = atof(argv[2]);
	if (lon < -180 || lon > 180) {
	  fprintf (stderr, "Longitude value is out of range.\n\n");
	  usage();
	}

	LLtoUTM (WSG84, lat, lon, &northing, &easting, zone);

	printf ("zone = %s, easting = %.0f, northing = %.0f\n", zone, easting, northing);
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