#ifndef POLARST_H
  #define POLARST_H
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


/**********************************************************************/
/*
 *                        DEFINES
 */

  #define POLAR_NO_ERROR                0x0000
  #define POLAR_LAT_ERROR               0x0001
  #define POLAR_LON_ERROR               0x0002
  #define POLAR_ORIGIN_LAT_ERROR        0x0004
  #define POLAR_ORIGIN_LON_ERROR        0x0008
  #define POLAR_EASTING_ERROR			  0x0010
  #define POLAR_NORTHING_ERROR		  0x0020
  #define POLAR_A_ERROR                 0x0040
  #define POLAR_INV_F_ERROR             0x0080
  #define POLAR_RADIUS_ERROR            0x0100

/**********************************************************************/
/*
 *                        FUNCTION PROTOTYPES
 */

/* ensure proper linkage to c++ programs */
  #ifdef __cplusplus
extern "C" {
  #endif

  long Set_Polar_Stereographic_Parameters (double a,
                                           double f,
                                           double Latitude_of_True_Scale,
                                           double Longitude_Down_from_Pole,
                                           double False_Easting,
                                           double False_Northing);
/*  
 *  The function Set_Polar_Stereographic_Parameters receives the ellipsoid
 *  parameters and Polar Stereograpic projection parameters as inputs, and
 *  sets the corresponding state variables.  If any errors occur, error
 *  code(s) are returned by the function, otherwise POLAR_NO_ERROR is returned.
 *
 *  a                : Semi-major axis of ellipsoid, in meters         (input)
 *  f                : Flattening of ellipsoid                         (input)
 *  Latitude_of_True_Scale  : Latitude of true scale, in radians       (input)
 *  Longitude_Down_from_Pole : Longitude down from pole, in radians    (input)
 *  False_Easting    : Easting (X) at center of projection, in meters  (input)
 *  False_Northing   : Northing (Y) at center of projection, in meters (input)
 */


  void Get_Polar_Stereographic_Parameters (double *a,
                                           double *f,
                                           double *Latitude_of_True_Scale,
                                           double *Longitude_Down_from_Pole,
                                           double *False_Easting,
                                           double *False_Northing);
/*
 * The function Get_Polar_Stereographic_Parameters returns the current
 * ellipsoid parameters and Polar projection parameters.
 *
 *  a                : Semi-major axis of ellipsoid, in meters         (output)
 *  f                : Flattening of ellipsoid                         (output)
 *  Latitude_of_True_Scale  : Latitude of true scale, in radians       (output)
 *  Longitude_Down_from_Pole : Longitude down from pole, in radians    (output)
 *  False_Easting    : Easting (X) at center of projection, in meters  (output)
 *  False_Northing   : Northing (Y) at center of projection, in meters (output)
 */


  long Convert_Geodetic_To_Polar_Stereographic (double Latitude,
                                                double Longitude,
                                                double *Easting,
                                                double *Northing);
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



  long Convert_Polar_Stereographic_To_Geodetic (double Easting,
                                                double Northing,
                                                double *Latitude,
                                                double *Longitude);

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

  #ifdef __cplusplus
}
  #endif

#endif  /* POLARST_H  */

