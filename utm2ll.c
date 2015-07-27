/* UTM to Latitude / Longitude conversion */


#include <stdio.h>
#include <stdlib.h>

#include "LatLong-UTMconversion.h"


static void usage();


void main (int argc, char *argv[]) 
{
	double easting;
	double northing;
	double lat, lon;
	char zone[100];
	int znum;
	char *zlet;

	if (argc != 4) usage();

	strcpy (zone, argv[1]);
	znum = strtoul(zone, &zlet, 10);

	if (znum < 1 || znum > 60) {
	  fprintf (stderr, "Zone number is out of range.\n\n");
	  usage();
	}

	//printf ("zlet = %c 0x%02x\n", *zlet, *zlet);
	if (*zlet != '\0' && strchr ("CDEFGHJKLMNPQRSTUVWX", *zlet) == NULL) {
	  fprintf (stderr, "Latitudinal band must be one of CDEFGHJKLMNPQRSTUVWX.\n\n");
	  usage();
	}

	easting = atof(argv[2]);
	if (easting < 0 || easting > 999999) {
	  fprintf (stderr, "Easting value is out of range.\n\n");
	  usage();
	}

	northing = atof(argv[3]);
	if (northing < 0 || northing > 9999999) {
	  fprintf (stderr, "Northing value is out of range.\n\n");
	  usage();
	}

	UTMtoLL (WSG84, northing, easting, zone, &lat, &lon);

	printf ("latitude = %f, longitude = %f\n", lat, lon);
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
	fprintf (stderr, "Example:\n");
	fprintf (stderr, "\tutm2ll 19T 306130 4726010\n");

	exit (1);
}