#ifndef UPS_H
  #define UPS_H
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


/**********************************************************************/
/*
 *                        DEFINES
 */

  #define UPS_NO_ERROR                0x0000
  #define UPS_LAT_ERROR               0x0001
  #define UPS_LON_ERROR               0x0002
  #define UPS_HEMISPHERE_ERROR        0x0004
  #define UPS_EASTING_ERROR           0x0008
  #define UPS_NORTHING_ERROR          0x0010
  #define UPS_A_ERROR                 0x0020
  #define UPS_INV_F_ERROR             0x0040


/**********************************************************************/
/*
 *                        FUNCTION PROTOTYPES
 *                          for UPS.C
 */

/* ensure proper linkage to c++ programs */
  #ifdef __cplusplus
extern "C" {
  #endif

  long Set_UPS_Parameters( double a,
                           double f);
/*
 * The function SET_UPS_PARAMETERS receives the ellipsoid parameters and sets
 * the corresponding state variables. If any errors occur, the error code(s)
 * are returned by the function, otherwise UPS_NO_ERROR is returned.
 *
 *   a     : Semi-major axis of ellipsoid in meters (input)
 *   f     : Flattening of ellipsoid                (input)
 */


  void Get_UPS_Parameters( double *a,
                           double *f);
/*
 * The function Get_UPS_Parameters returns the current ellipsoid parameters.
 *
 *  a      : Semi-major axis of ellipsoid, in meters (output)
 *  f      : Flattening of ellipsoid                 (output)
 */


  long Convert_Geodetic_To_UPS ( double Latitude,
                                 double Longitude,
                                 char   *Hemisphere,
                                 double *Easting,
                                 double *Northing);
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


  long Convert_UPS_To_Geodetic(char   Hemisphere,
                               double Easting,
                               double Northing,
                               double *Latitude,
                               double *Longitude);

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

  #ifdef __cplusplus
}
  #endif

#endif  /* UPS_H  */
