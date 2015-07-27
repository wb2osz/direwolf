/***************************************************************************/
/* RSC IDENTIFIER: POLAR STEREOGRAPHIC 
 *
 *
 * ABSTRACT
 *
 *    This component provides conversions between geodetic (latitude and
 *    longitude) coordinates and Polar Stereographic (easting and northing) 
 *    coordinates.
 *
 * ERROR HANDLING
 *
 *    This component checks parameters for valid values.  If an invalid 
 *    value is found the error code is combined with the current error code 
 *    using the bitwise or.  This combining allows multiple error codes to 
 *    be returned. The possible error codes are:
 *
 *          POLAR_NO_ERROR           : No errors occurred in function
 *          POLAR_LAT_ERROR          : Latitude outside of valid range
 *                                      (-90 to 90 degrees)
 *          POLAR_LON_ERROR          : Longitude outside of valid range
 *                                      (-180 to 360 degrees) 
 *          POLAR_ORIGIN_LAT_ERROR   : Latitude of true scale outside of valid
 *                                      range (-90 to 90 degrees)
 *          POLAR_ORIGIN_LON_ERROR   : Longitude down from pole outside of valid
 *                                      range (-180 to 360 degrees)
 *          POLAR_EASTING_ERROR      : Easting outside of valid range,
 *                                      depending on ellipsoid and
 *                                      projection parameters
 *          POLAR_NORTHING_ERROR     : Northing outside of valid range,
 *                                      depending on ellipsoid and
 *                                      projection parameters
 *          POLAR_RADIUS_ERROR       : Coordinates too far from pole,
 *                                      depending on ellipsoid and
 *                                      projection parameters
 *          POLAR_A_ERROR            : Semi-major axis less than or equal to zero
 *          POLAR_INV_F_ERROR        : Inverse flattening outside of valid range
 *								  	                  (250 to 350)
 *
 *
 * REUSE NOTES
 *
 *    POLAR STEREOGRAPHIC is intended for reuse by any application that  
 *    performs a Polar Stereographic projection.
 *
 *
 * REFERENCES
 *
 *    Further information on POLAR STEREOGRAPHIC can be found in the
 *    Reuse Manual.
 *
 *
 *    POLAR STEREOGRAPHIC originated from :
 *                                U.S. Army Topographic Engineering Center
 *                                Geospatial Information Division
 *                                7701 Telegraph Road
 *                                Alexandria, VA  22310-3864
 *
 *
 * LICENSES
 *
 *    None apply to this component.
 *
 *
 * RESTRICTIONS
 *
 *    POLAR STEREOGRAPHIC has no restrictions.
 *
 *
 * ENVIRONMENT
 *
 *    POLAR STEREOGRAPHIC was tested and certified in the following
 *    environments:
 *
 *    1. Solaris 2.5 with GCC, version 2.8.1
 *    2. Window 95 with MS Visual C++, version 6
 *
 *
 * MODIFICATIONS
 *
 *    Date              Description
 *    ----              -----------
 *    06-11-95          Original Code
 *    03-01-97          Original Code
 *
 *
 */


/************************************************************************/
/*
 *                               INCLUDES
 */

#include <math.h>
#include "polarst.h"

/*
 *    math.h     - Standard C math library
 *    polarst.h  - Is for prototype error checking
 */


/************************************************************************/
/*                               DEFINES
 *
 */


#define PI           3.14159265358979323e0       /* PI     */
#define PI_OVER_2    (PI / 2.0)           
#define TWO_PI       (2.0 * PI)
#define POLAR_POW(EsSin)     pow((1.0 - EsSin) / (1.0 + EsSin), es_OVER_2)

/************************************************************************/
/*                           GLOBAL DECLARATIONS
 *
 */

const double PI_Over_4 = (PI / 4.0);

/* Ellipsoid Parameters, default to WGS 84  */
static double Polar_a = 6378137.0;                    /* Semi-major axis of ellipsoid in meters  */
static double Polar_f = 1 / 298.257223563;            /* Flattening of ellipsoid  */
static double es = 0.08181919084262188000;            /* Eccentricity of ellipsoid    */
static double es_OVER_2 = .040909595421311;           /* es / 2.0 */
static double Southern_Hemisphere = 0;                /* Flag variable */
static double tc = 1.0;
static double e4 = 1.0033565552493;
static double Polar_a_mc = 6378137.0;                 /* Polar_a * mc */
static double two_Polar_a = 12756274.0;               /* 2.0 * Polar_a */

/* Polar Stereographic projection Parameters */
static double Polar_Origin_Lat = ((PI * 90) / 180);   /* Latitude of origin in radians */
static double Polar_Origin_Long = 0.0;                /* Longitude of origin in radians */
static double Polar_False_Easting = 0.0;              /* False easting in meters */
static double Polar_False_Northing = 0.0;             /* False northing in meters */

/* Maximum variance for easting and northing values for WGS 84. */
static double Polar_Delta_Easting = 12713601.0;
static double Polar_Delta_Northing = 12713601.0;

/* These state variables are for optimization purposes. The only function
 * that should modify them is Set_Polar_Stereographic_Parameters.         
 */


/************************************************************************/
/*                              FUNCTIONS
 *
 */


long Set_Polar_Stereographic_Parameters (double a,
                                         double f,
                                         double Latitude_of_True_Scale,
                                         double Longitude_Down_from_Pole,
                                         double False_Easting,
                                         double False_Northing)

{  /* BEGIN Set_Polar_Stereographic_Parameters   */
/*  
 *  The function Set_Polar_Stereographic_Parameters receives the ellipsoid
 *  parameters and Polar Stereograpic projection parameters as inputs, and
 *  sets the corresponding state variables.  If any errors occur, error
 *  code(s) are returned by the function, otherwise POLAR_NO_ERROR is returned.
 *
 *  a                : Semi-major axis of ellipsoid, in meters         (input)
 *  f                : Flattening of ellipsoid					               (input)
 *  Latitude_of_True_Scale  : Latitude of true scale, in radians       (input)
 *  Longitude_Down_from_Pole : Longitude down from pole, in radians    (input)
 *  False_Easting    : Easting (X) at center of projection, in meters  (input)
 *  False_Northing   : Northing (Y) at center of projection, in meters (input)
 */

  double es2;
  double slat, clat;
  double essin;
  double one_PLUS_es, one_MINUS_es;
  double pow_es;
  double temp, temp_northing;
  double inv_f = 1 / f;
  double mc;                    
//  const double  epsilon = 1.0e-2;
  long Error_Code = POLAR_NO_ERROR;

  if (a <= 0.0)
  { /* Semi-major axis must be greater than zero */
    Error_Code |= POLAR_A_ERROR;
  }
  if ((inv_f < 250) || (inv_f > 350))
  { /* Inverse flattening must be between 250 and 350 */
    Error_Code |= POLAR_INV_F_ERROR;
  }
  if ((Latitude_of_True_Scale < -PI_OVER_2) || (Latitude_of_True_Scale > PI_OVER_2))
  { /* Origin Latitude out of range */
    Error_Code |= POLAR_ORIGIN_LAT_ERROR;
  }
  if ((Longitude_Down_from_Pole < -PI) || (Longitude_Down_from_Pole > TWO_PI))
  { /* Origin Longitude out of range */
    Error_Code |= POLAR_ORIGIN_LON_ERROR;
  }

  if (!Error_Code)
  { /* no errors */

    Polar_a = a;
    two_Polar_a = 2.0 * Polar_a;
    Polar_f = f;

    if (Longitude_Down_from_Pole > PI)
      Longitude_Down_from_Pole -= TWO_PI;
    if (Latitude_of_True_Scale < 0)
    {
      Southern_Hemisphere = 1;
      Polar_Origin_Lat = -Latitude_of_True_Scale;
      Polar_Origin_Long = -Longitude_Down_from_Pole;
    }
    else
    {
      Southern_Hemisphere = 0;
      Polar_Origin_Lat = Latitude_of_True_Scale;
      Polar_Origin_Long = Longitude_Down_from_Pole;
    }
    Polar_False_Easting = False_Easting;
    Polar_False_Northing = False_Northing;

    es2 = 2 * Polar_f - Polar_f * Polar_f;
    es = sqrt(es2);
    es_OVER_2 = es / 2.0;

    if (fabs(fabs(Polar_Origin_Lat) - PI_OVER_2) > 1.0e-10)
    {
      slat = sin(Polar_Origin_Lat);
      essin = es * slat;
      pow_es = POLAR_POW(essin);
      clat = cos(Polar_Origin_Lat);
      mc = clat / sqrt(1.0 - essin * essin);
      Polar_a_mc = Polar_a * mc;
      tc = tan(PI_Over_4 - Polar_Origin_Lat / 2.0) / pow_es;
    }
    else
    {
      one_PLUS_es = 1.0 + es;
      one_MINUS_es = 1.0 - es;
      e4 = sqrt(pow(one_PLUS_es, one_PLUS_es) * pow(one_MINUS_es, one_MINUS_es));
    }

    /* Calculate Radius */
    Convert_Geodetic_To_Polar_Stereographic(0, Longitude_Down_from_Pole, 
                                            &temp, &temp_northing);

    Polar_Delta_Northing = temp_northing;
    if(Polar_False_Northing)
      Polar_Delta_Northing -= Polar_False_Northing;
    if (Polar_Delta_Northing < 0)
      Polar_Delta_Northing = -Polar_Delta_Northing;
    Polar_Delta_Northing *= 1.01;

    Polar_Delta_Easting = Polar_Delta_Northing;

  /*  Polar_Delta_Easting = temp_northing;
    if(Polar_False_Easting)
      Polar_Delta_Easting -= Polar_False_Easting;
    if (Polar_Delta_Easting < 0)
      Polar_Delta_Easting = -Polar_Delta_Easting;
    Polar_Delta_Easting *= 1.01;*/
  }

  return (Error_Code);
} /* END OF Set_Polar_Stereographic_Parameters */



void Get_Polar_Stereographic_Parameters (double *a,
                                         double *f,
                                         double *Latitude_of_True_Scale,
                                         double *Longitude_Down_from_Pole,
                                         double *False_Easting,
                                         double *False_Northing)

{ /* BEGIN Get_Polar_Stereographic_Parameters  */
/*
 * The function Get_Polar_Stereographic_Parameters returns the current
 * ellipsoid parameters and Polar projection parameters.
 *
 *  a                : Semi-major axis of ellipsoid, in meters         (output)
 *  f                : Flattening of ellipsoid					               (output)
 *  Latitude_of_True_Scale  : Latitude of true scale, in radians       (output)
 *  Longitude_Down_from_Pole : Longitude down from pole, in radians    (output)
 *  False_Easting    : Easting (X) at center of projection, in meters  (output)
 *  False_Northing   : Northing (Y) at center of projection, in meters (output)
 */

  *a = Polar_a;
  *f = Polar_f;
  *Latitude_of_True_Scale = Polar_Origin_Lat;
  *Longitude_Down_from_Pole = Polar_Origin_Long;
  *False_Easting = Polar_False_Easting;
  *False_Northing = Polar_False_Northing;
  return;
} /* END OF Get_Polar_Stereographic_Parameters */


long Convert_Geodetic_To_Polar_Stereographic (double Latitude,
                                              double Longitude,
                                              double *Easting,
                                              double *Northing)

{  /* BEGIN Convert_Geodetic_To_Polar_Stereographic */

/*
 * The function Convert_Geodetic_To_Polar_Stereographic converts geodetic
 * coordinates (latitude and longitude) to Polar Stereographic coordinates
 * (easting and northing), according to the current ellipsoid
 * and Polar Stereographic projection parameters. If any errors occur, error
 * code(s) are returned by the function, otherwise POLAR_NO_ERROR is returned.
 *
 *    Latitude   :  Latitude, in radians                      (input)
 *    Longitude  :  Longitude, in radians                     (input)
 *    Easting    :  Easting (X), in meters                    (output)
 *    Northing   :  Northing (Y), in meters                   (output)
 */

  double dlam;
  double slat;
  double essin;
  double t;
  double rho;
  double pow_es;
  long Error_Code = POLAR_NO_ERROR;

  if ((Latitude < -PI_OVER_2) || (Latitude > PI_OVER_2))
  {   /* Latitude out of range */
    Error_Code |= POLAR_LAT_ERROR;
  }
  if ((Latitude < 0) && (Southern_Hemisphere == 0))
  {   /* Latitude and Origin Latitude in different hemispheres */
    Error_Code |= POLAR_LAT_ERROR;
  }
  if ((Latitude > 0) && (Southern_Hemisphere == 1))
  {   /* Latitude and Origin Latitude in different hemispheres */
    Error_Code |= POLAR_LAT_ERROR;
  }
  if ((Longitude < -PI) || (Longitude > TWO_PI))
  {  /* Longitude out of range */
    Error_Code |= POLAR_LON_ERROR;
  }


  if (!Error_Code)
  {  /* no errors */

    if (fabs(fabs(Latitude) - PI_OVER_2) < 1.0e-10)
    {
      *Easting = Polar_False_Easting;
      *Northing = Polar_False_Northing;
    }
    else
    {
      if (Southern_Hemisphere != 0)
      {
        Longitude *= -1.0;
        Latitude *= -1.0;
      }
      dlam = Longitude - Polar_Origin_Long;
      if (dlam > PI)
      {
        dlam -= TWO_PI;
      }
      if (dlam < -PI)
      {
        dlam += TWO_PI;
      }
      slat = sin(Latitude);
      essin = es * slat;
      pow_es = POLAR_POW(essin);
      t = tan(PI_Over_4 - Latitude / 2.0) / pow_es;

      if (fabs(fabs(Polar_Origin_Lat) - PI_OVER_2) > 1.0e-10)
        rho = Polar_a_mc * t / tc;
      else
        rho = two_Polar_a * t / e4;


      if (Southern_Hemisphere != 0)
      {
        *Easting = -(rho * sin(dlam) - Polar_False_Easting);
     //   *Easting *= -1.0;
        *Northing = rho * cos(dlam) + Polar_False_Northing;
      }
      else
      {
        *Easting = rho * sin(dlam) + Polar_False_Easting;
        *Northing = -rho * cos(dlam) + Polar_False_Northing;
      }

    }
  }
  return (Error_Code);
} /* END OF Convert_Geodetic_To_Polar_Stereographic */


long Convert_Polar_Stereographic_To_Geodetic (double Easting,
                                              double Northing,
                                              double *Latitude,
                                              double *Longitude)

{ /*  BEGIN Convert_Polar_Stereographic_To_Geodetic  */
/*
 *  The function Convert_Polar_Stereographic_To_Geodetic converts Polar
 *  Stereographic coordinates (easting and northing) to geodetic
 *  coordinates (latitude and longitude) according to the current ellipsoid
 *  and Polar Stereographic projection Parameters. If any errors occur, the
 *  code(s) are returned by the function, otherwise POLAR_NO_ERROR
 *  is returned.
 *
 *  Easting          : Easting (X), in meters                   (input)
 *  Northing         : Northing (Y), in meters                  (input)
 *  Latitude         : Latitude, in radians                     (output)
 *  Longitude        : Longitude, in radians                    (output)
 *
 */

  double dy = 0, dx = 0;
  double rho = 0;
  double t;
  double PHI, sin_PHI;
  double tempPHI = 0.0;
  double essin;
  double pow_es;
  double delta_radius;
  long Error_Code = POLAR_NO_ERROR;
  double min_easting = Polar_False_Easting - Polar_Delta_Easting;
  double max_easting = Polar_False_Easting + Polar_Delta_Easting;
  double min_northing = Polar_False_Northing - Polar_Delta_Northing;
  double max_northing = Polar_False_Northing + Polar_Delta_Northing;

  if (Easting > max_easting || Easting < min_easting)
  { /* Easting out of range */
    Error_Code |= POLAR_EASTING_ERROR;
  }
  if (Northing > max_northing || Northing < min_northing)
  { /* Northing out of range */
    Error_Code |= POLAR_NORTHING_ERROR;
  }

  if (!Error_Code)
  {
    dy = Northing - Polar_False_Northing;
    dx = Easting - Polar_False_Easting;

    /* Radius of point with origin of false easting, false northing */
    rho = sqrt(dx * dx + dy * dy);   
    
    delta_radius = sqrt(Polar_Delta_Easting * Polar_Delta_Easting + Polar_Delta_Northing * Polar_Delta_Northing);

    if(rho > delta_radius)
    { /* Point is outside of projection area */
      Error_Code |= POLAR_RADIUS_ERROR;
    }

    if (!Error_Code)
    { /* no errors */
      if ((dy == 0.0) && (dx == 0.0))
      {
        *Latitude = PI_OVER_2;
        *Longitude = Polar_Origin_Long;

      }
      else
      {
        if (Southern_Hemisphere != 0)
        {
          dy *= -1.0;
          dx *= -1.0;
        }

        if (fabs(fabs(Polar_Origin_Lat) - PI_OVER_2) > 1.0e-10)
          t = rho * tc / (Polar_a_mc);
        else
          t = rho * e4 / (two_Polar_a);
        PHI = PI_OVER_2 - 2.0 * atan(t);
        while (fabs(PHI - tempPHI) > 1.0e-10)
        {
          tempPHI = PHI;
          sin_PHI = sin(PHI);
          essin =  es * sin_PHI;
          pow_es = POLAR_POW(essin);
          PHI = PI_OVER_2 - 2.0 * atan(t * pow_es);
        }
        *Latitude = PHI;
        *Longitude = Polar_Origin_Long + atan2(dx, -dy);

        if (*Longitude > PI)
          *Longitude -= TWO_PI;
        else if (*Longitude < -PI)
          *Longitude += TWO_PI;


        if (*Latitude > PI_OVER_2)  /* force distorted values to 90, -90 degrees */
          *Latitude = PI_OVER_2;
        else if (*Latitude < -PI_OVER_2)
          *Latitude = -PI_OVER_2;

        if (*Longitude > PI)  /* force distorted values to 180, -180 degrees */
          *Longitude = PI;
        else if (*Longitude < -PI)
          *Longitude = -PI;

      }
      if (Southern_Hemisphere != 0)
      {
        *Latitude *= -1.0;
        *Longitude *= -1.0;
      }
    }
  }
  return (Error_Code);
} /* END OF Convert_Polar_Stereographic_To_Geodetic */



