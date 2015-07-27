
#include <math.h>
#include "constants.h"
#include "LatLong- UTM conversion.h"

//forward declarations
double CorrRatio(double LatRad, const double C);
double NewtonRaphson(const double initEstimate);


void LLtoSwissGrid(const double Lat, const double Long, 
			 double &SwissNorthing, double &SwissEasting)
{
//converts lat/long to Swiss Grid coords.  Equations from "Supplementary PROJ.4 Notes-
//Swiss Oblique Mercator Projection", August 5, 1995, Release 4.3.3, by Gerald I. Evenden
//Lat and Long are in decimal degrees
//This transformation is, of course, only valid in Switzerland
	//Written by Chuck Gantz- chuck.gantz@globalstar.com
	double a = ellipsoid[3].EquatorialRadius; //Bessel ellipsoid
	double eccSquared = ellipsoid[3].eccentricitySquared;
	double ecc = sqrt(eccSquared);

	double LongOrigin = 7.43958333; //E7d26'22.500"
	double LatOrigin = 46.95240556; //N46d57'8.660"

	double LatRad = Lat*deg2rad;
	double LongRad = Long*deg2rad;
	double LatOriginRad = LatOrigin*deg2rad;
	double LongOriginRad = LongOrigin*deg2rad;

	double c = sqrt(1+((eccSquared * pow(cos(LatOriginRad), 4)) / (1-eccSquared))); 
	
	double equivLatOrgRadPrime = asin(sin(LatOriginRad) / c);

	//eqn. 1
	double K = log(tan(FOURTHPI + equivLatOrgRadPrime/2))
				-c*(log(tan(FOURTHPI + LatOriginRad/2)) 
					- ecc/2 * log((1+ecc*sin(LatOriginRad)) / (1-ecc*sin(LatOriginRad))));

	
	double LongRadPrime = c*(LongRad - LongOriginRad); //eqn 2
	double w = c*(log(tan(FOURTHPI + LatRad/2)) 
				- ecc/2 * log((1+ecc*sin(LatRad)) / (1-ecc*sin(LatRad)))) + K; //eqn 1
	double LatRadPrime = 2 * (atan(exp(w)) - FOURTHPI); //eqn 1
	
	//eqn 3
	double sinLatDoublePrime = cos(equivLatOrgRadPrime) * sin(LatRadPrime) 
								- sin(equivLatOrgRadPrime) * cos(LatRadPrime) * cos(LongRadPrime);
	double LatRadDoublePrime = asin(sinLatDoublePrime);
	
	//eqn 4
	double sinLongDoublePrime = cos(LatRadPrime)*sin(LongRadPrime) / cos(LatRadDoublePrime);
	double LongRadDoublePrime = asin(sinLongDoublePrime);

	double R = a*sqrt(1-eccSquared) / (1-eccSquared*sin(LatOriginRad) * sin(LatOriginRad));

	SwissNorthing = R*log(tan(FOURTHPI + LatRadDoublePrime/2)) + 200000.0; //eqn 5
	SwissEasting = R*LongRadDoublePrime + 600000.0; //eqn 6

}


void SwissGridtoLL(const double SwissNorthing, const double SwissEasting, 
					double& Lat, double& Long)
{
	double a = ellipsoid[3].EquatorialRadius; //Bessel ellipsoid
	double eccSquared = ellipsoid[3].eccentricitySquared;
	double ecc = sqrt(eccSquared);

	double LongOrigin = 7.43958333; //E7d26'22.500"
	double LatOrigin = 46.95240556; //N46d57'8.660"

	double LatOriginRad = LatOrigin*deg2rad;
	double LongOriginRad = LongOrigin*deg2rad;

	double R = a*sqrt(1-eccSquared) / (1-eccSquared*sin(LatOriginRad) * sin(LatOriginRad));

	double LatRadDoublePrime = 2*(atan(exp((SwissNorthing - 200000.0)/R)) - FOURTHPI); //eqn. 7
	double LongRadDoublePrime = (SwissEasting - 600000.0)/R; //eqn. 8 with equation corrected


	double c = sqrt(1+((eccSquared * pow(cos(LatOriginRad), 4)) / (1-eccSquared))); 
	double equivLatOrgRadPrime = asin(sin(LatOriginRad) / c);

	double sinLatRadPrime = cos(equivLatOrgRadPrime)*sin(LatRadDoublePrime)
							+ sin(equivLatOrgRadPrime)*cos(LatRadDoublePrime)*cos(LongRadDoublePrime);
	double LatRadPrime = asin(sinLatRadPrime);

	double sinLongRadPrime = cos(LatRadDoublePrime)*sin(LongRadDoublePrime)/cos(LatRadPrime);
	double LongRadPrime = asin(sinLongRadPrime);

	Long = (LongRadPrime/c + LongOriginRad) * rad2deg;

	Lat = NewtonRaphson(LatRadPrime) * rad2deg;

}

double NewtonRaphson(const double initEstimate)
{
	double Estimate = initEstimate;
	double tol = 0.00001;
	double corr;

	double eccSquared = ellipsoid[3].eccentricitySquared;
	double ecc = sqrt(eccSquared);

	double LatOrigin = 46.95240556; //N46d57'8.660"
	double LatOriginRad = LatOrigin*deg2rad;

	double c = sqrt(1+((eccSquared * pow(cos(LatOriginRad), 4)) / (1-eccSquared))); 
	
	double equivLatOrgRadPrime = asin(sin(LatOriginRad) / c);

	//eqn. 1
	double K = log(tan(FOURTHPI + equivLatOrgRadPrime/2))
				-c*(log(tan(FOURTHPI + LatOriginRad/2)) 
					- ecc/2 * log((1+ecc*sin(LatOriginRad)) / (1-ecc*sin(LatOriginRad))));
	double C = (K - log(tan(FOURTHPI + initEstimate/2)))/c;

	do
	{
		corr = CorrRatio(Estimate, C);
		Estimate = Estimate - corr;
	}
	while (fabs(corr) > tol);

	return Estimate;
}



double CorrRatio(double LatRad, const double C)
{
	double eccSquared = ellipsoid[3].eccentricitySquared;
	double ecc = sqrt(eccSquared);
	double corr = (C + log(tan(FOURTHPI + LatRad/2)) 
				- ecc/2 * log((1+ecc*sin(LatRad)) / (1-ecc*sin(LatRad)))) * (((1-eccSquared*sin(LatRad)*sin(LatRad)) * cos(LatRad)) / (1-eccSquared));

	return corr;
}
