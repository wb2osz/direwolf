#ifndef USNG_H
  #define USNG_H

/***************************************************************************/
/* RSC IDENTIFIER:  USNG
 *
 * ABSTRACT
 *
 *    This component converts between geodetic coordinates (latitude and 
 *    longitude) and United States National Grid (USNG) coordinates. 
 *
 * ERROR HANDLING
 *
 *    This component checks parameters for valid values.  If an invalid value
 *    is found, the error code is combined with the current error code using 
 *    the bitwise or.  This combining allows multiple error codes to be
 *    returned. The possible error codes are:
 *
 *          USNG_NO_ERROR          : No errors occurred in function
 *          USNG_LAT_ERROR         : Latitude outside of valid range 
 *                                    (-90 to 90 degrees)
 *          USNG_LON_ERROR         : Longitude outside of valid range
 *                                    (-180 to 360 degrees)
 *          USNG_STR_ERROR         : An USNG string error: string too long,
 *                                    too short, or badly formed
 *          USNG_PRECISION_ERROR   : The precision must be between 0 and 5 
 *                                    inclusive.
 *          USNG_A_ERROR           : Semi-major axis less than or equal to zero
 *          USNG_INV_F_ERROR       : Inverse flattening outside of valid range
 *									                  (250 to 350)
 *          USNG_EASTING_ERROR     : Easting outside of valid range
 *                                    (100,000 to 900,000 meters for UTM)
 *                                    (0 to 4,000,000 meters for UPS)
 *          USNG_NORTHING_ERROR    : Northing outside of valid range
 *                                    (0 to 10,000,000 meters for UTM)
 *                                    (0 to 4,000,000 meters for UPS)
 *          USNG_ZONE_ERROR        : Zone outside of valid range (1 to 60)
 *          USNG_HEMISPHERE_ERROR  : Invalid hemisphere ('N' or 'S')
 *
 * REUSE NOTES
 *
 *    USNG is intended for reuse by any application that does conversions
 *    between geodetic coordinates and USNG coordinates.
 *
 * REFERENCES
 *
 *    Further information on USNG can be found in the Reuse Manual.
 *
 *    USNG originated from : Federal Geographic Data Committee
 *                           590 National Center
 *                           12201 Sunrise Valley Drive
 *                           Reston, VA  22092
 *
 * LICENSES
 *
 *    None apply to this component.
 *
 * RESTRICTIONS
 *
 *
 * ENVIRONMENT
 *
 *    USNG was tested and certified in the following environments:
 *
 *    1. Solaris 2.5 with GCC version 2.8.1
 *    2. Windows 95 with MS Visual C++ version 6
 *
 * MODIFICATIONS
 *
 *    Date              Description
 *    ----              -----------
 *    06-05-06          Original Code (cloned from MGRS)
 *
 */


/***************************************************************************/
/*
 *                              DEFINES
 */

  #define USNG_NO_ERROR                0x0000
  #define USNG_LAT_ERROR               0x0001
  #define USNG_LON_ERROR               0x0002
  #define USNG_STRING_ERROR            0x0004
  #define USNG_PRECISION_ERROR         0x0008
  #define USNG_A_ERROR                 0x0010
  #define USNG_INV_F_ERROR             0x0020
  #define USNG_EASTING_ERROR           0x0040
  #define USNG_NORTHING_ERROR          0x0080
  #define USNG_ZONE_ERROR              0x0100
  #define USNG_HEMISPHERE_ERROR        0x0200
  #define USNG_LAT_WARNING             0x0400


/***************************************************************************/
/*
 *                              FUNCTION PROTOTYPES
 */

/* ensure proper linkage to c++ programs */
  #ifdef __cplusplus
extern "C" {
  #endif


  long Set_USNG_Parameters(double a,
                           double f,
                           char   *Ellipsoid_Code);
/*
 * The function Set_USNG_Parameters receives the ellipsoid parameters and sets
 * the corresponding state variables. If any errors occur, the error code(s)
 * are returned by the function, otherwise USNG_NO_ERROR is returned.
 *
 *   a                : Semi-major axis of ellipsoid in meters (input)
 *   f                : Flattening of ellipsoid					       (input)
 *   Ellipsoid_Code   : 2-letter code for ellipsoid            (input)
 */


  void Get_USNG_Parameters(double *a,
                           double *f,
                           char   *Ellipsoid_Code);
/*
 * The function Get_USNG_Parameters returns the current ellipsoid
 * parameters.
 *
 *  a                : Semi-major axis of ellipsoid, in meters (output)
 *  f                : Flattening of ellipsoid					       (output)
 *  Ellipsoid_Code   : 2-letter code for ellipsoid             (output)
 */


  long Convert_Geodetic_To_USNG (double Latitude,
                                 double Longitude,
                                 long   Precision,
                                 char *USNG);
/*
 * The function Convert_Geodetic_To_USNG converts geodetic (latitude and
 * longitude) coordinates to a USNG coordinate string, according to the 
 * current ellipsoid parameters.  If any errors occur, the error code(s) 
 * are returned by the  function, otherwise USNG_NO_ERROR is returned.
 *
 *    Latitude   : Latitude in radians              (input)
 *    Longitude  : Longitude in radians             (input)
 *    Precision  : Precision level of USNG string   (input)
 *    USNG       : USNG coordinate string           (output)
 *  
 */


  long Convert_USNG_To_Geodetic (char *USNG,
                                 double *Latitude,
                                 double *Longitude);
/*
 * This function converts a USNG coordinate string to Geodetic (latitude
 * and longitude in radians) coordinates.  If any errors occur, the error 
 * code(s) are returned by the  function, otherwise USNG_NO_ERROR is returned.  
 *
 *    USNG       : USNG coordinate string           (input)
 *    Latitude   : Latitude in radians              (output)
 *    Longitude  : Longitude in radians             (output)
 *  
 */


  long Convert_UTM_To_USNG (long Zone,
                            char Hemisphere,
                            double Easting,
                            double Northing,
                            long Precision,
                            char *USNG);
/*
 * The function Convert_UTM_To_USNG converts UTM (zone, easting, and
 * northing) coordinates to a USNG coordinate string, according to the 
 * current ellipsoid parameters.  If any errors occur, the error code(s) 
 * are returned by the  function, otherwise USNG_NO_ERROR is returned.
 *
 *    Zone       : UTM zone                         (input)
 *    Hemisphere : North or South hemisphere        (input)
 *    Easting    : Easting (X) in meters            (input)
 *    Northing   : Northing (Y) in meters           (input)
 *    Precision  : Precision level of USNG string   (input)
 *    USNG       : USNG coordinate string           (output)
 */


  long Convert_USNG_To_UTM (char   *USNG,
                            long   *Zone,
                            char   *Hemisphere,
                            double *Easting,
                            double *Northing); 
/*
 * The function Convert_USNG_To_UTM converts a USNG coordinate string
 * to UTM projection (zone, hemisphere, easting and northing) coordinates 
 * according to the current ellipsoid parameters.  If any errors occur, 
 * the error code(s) are returned by the function, otherwise UTM_NO_ERROR 
 * is returned.
 *
 *    USNG       : USNG coordinate string           (input)
 *    Zone       : UTM zone                         (output)
 *    Hemisphere : North or South hemisphere        (output)
 *    Easting    : Easting (X) in meters            (output)
 *    Northing   : Northing (Y) in meters           (output)
 */



  long Convert_UPS_To_USNG ( char   Hemisphere,
                             double Easting,
                             double Northing,
                             long Precision,
                             char *USNG);

/*
 *  The function Convert_UPS_To_USNG converts UPS (hemisphere, easting, 
 *  and northing) coordinates to a USNG coordinate string according to 
 *  the current ellipsoid parameters.  If any errors occur, the error
 *  code(s) are returned by the function, otherwise UPS_NO_ERROR is 
 *  returned.
 *
 *    Hemisphere    : Hemisphere either 'N' or 'S'     (input)
 *    Easting       : Easting/X in meters              (input)
 *    Northing      : Northing/Y in meters             (input)
 *    Precision     : Precision level of USNG string   (input)
 *    USNG          : USNG coordinate string           (output)
 */


  long Convert_USNG_To_UPS ( char   *USNG,
                             char   *Hemisphere,
                             double *Easting,
                             double *Northing);
/*
 *  The function Convert_USNG_To_UPS converts a USNG coordinate string
 *  to UPS (hemisphere, easting, and northing) coordinates, according 
 *  to the current ellipsoid parameters. If any errors occur, the error 
 *  code(s) are returned by the function, otherwide UPS_NO_ERROR is returned.
 *
 *    USNG          : USNG coordinate string           (input)
 *    Hemisphere    : Hemisphere either 'N' or 'S'     (output)
 *    Easting       : Easting/X in meters              (output)
 *    Northing      : Northing/Y in meters             (output)
 */



  #ifdef __cplusplus
}
  #endif

#endif /* USNG_H */
