/********************************************************************/
/* RSC IDENTIFIER: UPS
 *
 *
 * ABSTRACT
 *
 *    This component provides conversions between geodetic (latitude
 *    and longitude) coordinates and Universal Polar Stereographic (UPS)
 *    projection (hemisphere, easting, and northing) coordinates.
 *
 *
 * ERROR HANDLING
 *
 *    This component checks parameters for valid values.  If an 
 *    invalid value is found the error code is combined with the 
 *    current error code using the bitwise or.  This combining allows  
 *    multiple error codes to be returned. The possible error codes 
 *    are:
 *
 *         UPS_NO_ERROR           : No errors occurred in function
 *         UPS_LAT_ERROR          : Latitude outside of valid range
 *                                   (North Pole: 83.5 to 90,
 *                                    South Pole: -79.5 to -90)
 *         UPS_LON_ERROR          : Longitude outside of valid range
 *                                   (-180 to 360 degrees)
 *         UPS_HEMISPHERE_ERROR   : Invalid hemisphere ('N' or 'S')
 *         UPS_EASTING_ERROR      : Easting outside of valid range,
 *                                   (0 to 4,000,000m)
 *         UPS_NORTHING_ERROR     : Northing outside of valid range,
 *                                   (0 to 4,000,000m)
 *         UPS_A_ERROR            : Semi-major axis less than or equal to zero
 *         UPS_INV_F_ERROR        : Inverse flattening outside of valid range
 *								  	               (250 to 350)
 *
 *
 * REUSE NOTES
 *
 *    UPS is intended for reuse by any application that performs a Universal
 *    Polar Stereographic (UPS) projection.
 *
 *
 * REFERENCES
 *
 *    Further information on UPS can be found in the Reuse Manual.
 *
 *    UPS originated from :  U.S. Army Topographic Engineering Center
 *                           Geospatial Information Division
 *                           7701 Telegraph Road
 *                           Alexandria, VA  22310-3864
 *
 *
 * LICENSES
 *
 *    None apply to this component.
 *
 *
 * RESTRICTIONS
 *
 *    UPS has no restrictions.
 *
 *
 * ENVIRONMENT
 *
 *    UPS was tested and certified in the following environments:
 *
 *    1. Solaris 2.5 with GCC version 2.8.1
 *    2. Windows 95 with MS Visual C++ version 6
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
#include "ups.h"
/*
 *    math.h     - Is needed to call the math functions.
 *    polar.h    - Is used to convert polar stereographic coordinates
 *    ups.h      - Defines the function prototypes for the ups module.
 */


/************************************************************************/
/*                               GLOBAL DECLARATIONS
 *
 */

#define PI       3.14159265358979323e0  /* PI     */
#define PI_OVER    (PI/2.0e0)           /* PI over 2 */
#define MAX_LAT    ((PI * 90)/180.0)    /* 90 degrees in radians */
#define MAX_ORIGIN_LAT ((81.114528 * PI) / 180.0)
#define MIN_NORTH_LAT (83.5*PI/180.0)
#define MIN_SOUTH_LAT (-79.5*PI/180.0)
#define MIN_EAST_NORTH 0
#define MAX_EAST_NORTH 4000000

/* Ellipsoid Parameters, default to WGS 84  */
static double UPS_a = 6378137.0;          /* Semi-major axis of ellipsoid in meters   */
static double UPS_f = 1 / 298.257223563;  /* Flattening of ellipsoid  */
const double UPS_False_Easting = 2000000;
const double UPS_False_Northing = 2000000;
static double UPS_Origin_Latitude = MAX_ORIGIN_LAT;  /*set default = North Hemisphere */
static double UPS_Origin_Longitude = 0.0;


/************************************************************************/
/*                              FUNCTIONS
 *
 */


long Set_UPS_Parameters( double a,
                         double f)
{
/*
 * The function SET_UPS_PARAMETERS receives the ellipsoid parameters and sets
 * the corresponding state variables. If any errors occur, the error code(s)
 * are returned by the function, otherwise UPS_NO_ERROR is returned.
 *
 *   a     : Semi-major axis of ellipsoid in meters (input)
 *   f     : Flattening of ellipsoid					      (input)
 */

  double inv_f = 1 / f;
  long Error_Code = UPS_NO_ERROR;

  if (a <= 0.0)
  { /* Semi-major axis must be greater than zero */
    Error_Code |= UPS_A_ERROR;
  }
  if ((inv_f < 250) || (inv_f > 350))
  { /* Inverse flattening must be between 250 and 350 */
    Error_Code |= UPS_INV_F_ERROR;
  }

  if (!Error_Code)
  { /* no errors */
    UPS_a = a;
    UPS_f = f;
  }
  return (Error_Code);
}  /* END of Set_UPS_Parameters  */


void Get_UPS_Parameters( double *a,
                         double *f)
{
/*
 * The function Get_UPS_Parameters returns the current ellipsoid parameters.
 *
 *  a      : Semi-major axis of ellipsoid, in meters (output)
 *  f      : Flattening of ellipsoid					       (output)
 */

  *a = UPS_a;
  *f = UPS_f;
  return;
} /* END OF Get_UPS_Parameters */


long Convert_Geodetic_To_UPS ( double Latitude,
                               double Longitude,
                               char   *Hemisphere,
                               double *Easting,
                               double *Northing)
{
/*
 *  The function Convert_Geodetic_To_UPS converts geodetic (latitude and
 *  longitude) coordinates to UPS (hemisphere, easting, and northing)
 *  coordinates, according to the current ellipsoid parameters. If any 
 *  errors occur, the error code(s) are returned by the function, 
 *  otherwide UPS_NO_ERROR is returned.
 *
 *    Latitude      : Latitude in radians                       (input)
 *    Longitude     : Longitude in radians                      (input)
 *    Hemisphere    : Hemisphere either 'N' or 'S'              (output)
 *    Easting       : Easting/X in meters                       (output)
 *    Northing      : Northing/Y in meters                      (output)
 */

  double tempEasting, tempNorthing;
  long Error_Code = UPS_NO_ERROR;

  if ((Latitude < -MAX_LAT) || (Latitude > MAX_LAT))
  {   /* latitude out of range */
    Error_Code |= UPS_LAT_ERROR;
  }
  if ((Latitude < 0) && (Latitude > MIN_SOUTH_LAT))
    Error_Code |= UPS_LAT_ERROR;
  if ((Latitude >= 0) && (Latitude < MIN_NORTH_LAT))
    Error_Code |= UPS_LAT_ERROR;
  if ((Longitude < -PI) || (Longitude > (2 * PI)))
  {  /* slam out of range */
    Error_Code |= UPS_LON_ERROR;
  }

  if (!Error_Code)
  {  /* no errors */
    if (Latitude < 0)
    {
      UPS_Origin_Latitude = -MAX_ORIGIN_LAT; 
      *Hemisphere = 'S';
    }
    else
    {
      UPS_Origin_Latitude = MAX_ORIGIN_LAT; 
      *Hemisphere = 'N';
    }


    Set_Polar_Stereographic_Parameters( UPS_a,
                                        UPS_f,
                                        UPS_Origin_Latitude,
                                        UPS_Origin_Longitude,
                                        UPS_False_Easting,
                                        UPS_False_Northing);

    Convert_Geodetic_To_Polar_Stereographic(Latitude,
                                            Longitude,
                                            &tempEasting,
                                            &tempNorthing);

    *Easting = tempEasting;
    *Northing = tempNorthing;
  }  /*  END of if(!Error_Code)   */

  return Error_Code;
}  /* END OF Convert_Geodetic_To_UPS  */


long Convert_UPS_To_Geodetic(char   Hemisphere,
                             double Easting,
                             double Northing,
                             double *Latitude,
                             double *Longitude)
{
/*
 *  The function Convert_UPS_To_Geodetic converts UPS (hemisphere, easting, 
 *  and northing) coordinates to geodetic (latitude and longitude) coordinates
 *  according to the current ellipsoid parameters.  If any errors occur, the 
 *  error code(s) are returned by the function, otherwise UPS_NO_ERROR is 
 *  returned.
 *
 *    Hemisphere    : Hemisphere either 'N' or 'S'              (input)
 *    Easting       : Easting/X in meters                       (input)
 *    Northing      : Northing/Y in meters                      (input)
 *    Latitude      : Latitude in radians                       (output)
 *    Longitude     : Longitude in radians                      (output)
 */

  long Error_Code = UPS_NO_ERROR;

  if ((Hemisphere != 'N') && (Hemisphere != 'S'))
    Error_Code |= UPS_HEMISPHERE_ERROR;
  if ((Easting < MIN_EAST_NORTH) || (Easting > MAX_EAST_NORTH))
    Error_Code |= UPS_EASTING_ERROR;
  if ((Northing < MIN_EAST_NORTH) || (Northing > MAX_EAST_NORTH))
    Error_Code |= UPS_NORTHING_ERROR;

  if (Hemisphere =='N')
  {UPS_Origin_Latitude = MAX_ORIGIN_LAT;}
  if (Hemisphere =='S')
  {UPS_Origin_Latitude = -MAX_ORIGIN_LAT;}

  if (!Error_Code)
  {   /*  no errors   */
    Set_Polar_Stereographic_Parameters( UPS_a,
                                        UPS_f,
                                        UPS_Origin_Latitude,
                                        UPS_Origin_Longitude,
                                        UPS_False_Easting,
                                        UPS_False_Northing);



    Convert_Polar_Stereographic_To_Geodetic( Easting,
                                             Northing,
                                             Latitude,
                                             Longitude); 


    if ((*Latitude < 0) && (*Latitude > MIN_SOUTH_LAT))
      Error_Code |= UPS_LAT_ERROR;
    if ((*Latitude >= 0) && (*Latitude < MIN_NORTH_LAT))
      Error_Code |= UPS_LAT_ERROR;
  }  /*  END OF if(!Error_Code) */
  return (Error_Code);
}  /*  END OF Convert_UPS_To_Geodetic  */ 

