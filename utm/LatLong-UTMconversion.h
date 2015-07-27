//LatLong- UTM conversion..h
//definitions for lat/long to UTM and UTM to lat/lng conversions
#include <string.h>

#ifndef LATLONGCONV
#define LATLONGCONV

void LLtoUTM(int ReferenceEllipsoid, const double Lat, const double Long, 
			 double *UTMNorthing, double *UTMEasting, char* UTMZone);
void UTMtoLL(int ReferenceEllipsoid, const double UTMNorthing, const double UTMEasting, const char* UTMZone,
			  double *Lat,  double *Long );
char UTMLetterDesignator(double Lat);
void LLtoSwissGrid(const double Lat, const double Long, 
			 double *SwissNorthing, double *SwissEasting);
void SwissGridtoLL(const double SwissNorthing, const double SwissEasting, 
					double *Lat, double *Long);

struct Ellipsoid_s {
	int id;
	char* ellipsoidName;
	double EquatorialRadius; 
	double eccentricitySquared;  
};

typedef struct Ellipsoid_s Ellipsoid;

#define WSG84 23


#endif
