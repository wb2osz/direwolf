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
 *                                    (250 to 350)
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
 *    2. Windows XP with MS Visual C++ version 6
 *
 * MODIFICATIONS
 *
 *    Date              Description
 *    ----              -----------
 *    06-05-06          Original Code (cloned from MGRS)
 */


/***************************************************************************/
/*
 *                               INCLUDES
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ups.h"
#include "utm.h"
#include "usng.h"

/*
 *      ctype.h     - Standard C character handling library
 *      math.h      - Standard C math library
 *      stdio.h     - Standard C input/output library
 *      string.h    - Standard C string handling library
 *      ups.h       - Universal Polar Stereographic (UPS) projection
 *      utm.h       - Universal Transverse Mercator (UTM) projection
 *      usng.h      - function prototype error checking
 */


/***************************************************************************/
/*
 *                              GLOBAL DECLARATIONS
 */
#define DEG_TO_RAD       0.017453292519943295 /* PI/180                      */
#define RAD_TO_DEG       57.29577951308232087 /* 180/PI                      */
#define LETTER_A               0   /* ARRAY INDEX FOR LETTER A               */
#define LETTER_B               1   /* ARRAY INDEX FOR LETTER B               */
#define LETTER_C               2   /* ARRAY INDEX FOR LETTER C               */
#define LETTER_D               3   /* ARRAY INDEX FOR LETTER D               */
#define LETTER_E               4   /* ARRAY INDEX FOR LETTER E               */
#define LETTER_F               5   /* ARRAY INDEX FOR LETTER F               */
#define LETTER_G               6   /* ARRAY INDEX FOR LETTER G               */
#define LETTER_H               7   /* ARRAY INDEX FOR LETTER H               */
#define LETTER_I               8   /* ARRAY INDEX FOR LETTER I               */
#define LETTER_J               9   /* ARRAY INDEX FOR LETTER J               */
#define LETTER_K              10   /* ARRAY INDEX FOR LETTER K               */
#define LETTER_L              11   /* ARRAY INDEX FOR LETTER L               */
#define LETTER_M              12   /* ARRAY INDEX FOR LETTER M               */
#define LETTER_N              13   /* ARRAY INDEX FOR LETTER N               */
#define LETTER_O              14   /* ARRAY INDEX FOR LETTER O               */
#define LETTER_P              15   /* ARRAY INDEX FOR LETTER P               */
#define LETTER_Q              16   /* ARRAY INDEX FOR LETTER Q               */
#define LETTER_R              17   /* ARRAY INDEX FOR LETTER R               */
#define LETTER_S              18   /* ARRAY INDEX FOR LETTER S               */
#define LETTER_T              19   /* ARRAY INDEX FOR LETTER T               */
#define LETTER_U              20   /* ARRAY INDEX FOR LETTER U               */
#define LETTER_V              21   /* ARRAY INDEX FOR LETTER V               */
#define LETTER_W              22   /* ARRAY INDEX FOR LETTER W               */
#define LETTER_X              23   /* ARRAY INDEX FOR LETTER X               */
#define LETTER_Y              24   /* ARRAY INDEX FOR LETTER Y               */
#define LETTER_Z              25   /* ARRAY INDEX FOR LETTER Z               */
#define USNG_LETTERS            3  /* NUMBER OF LETTERS IN USNG              */
#define ONEHT          100000.e0    /* ONE HUNDRED THOUSAND                  */
#define TWOMIL        2000000.e0    /* TWO MILLION                           */
#define TRUE                      1  /* CONSTANT VALUE FOR TRUE VALUE  */
#define FALSE                     0  /* CONSTANT VALUE FOR FALSE VALUE */
#define PI    3.14159265358979323e0  /* PI                             */
#define PI_OVER_2  (PI / 2.0e0)

#define MIN_EASTING  100000
#define MAX_EASTING  900000
#define MIN_NORTHING 0
#define MAX_NORTHING 10000000
#define MAX_PRECISION           5   /* Maximum precision of easting & northing */
#define MIN_UTM_LAT      ( (-80 * PI) / 180.0 ) /* -80 degrees in radians    */
#define MAX_UTM_LAT      ( (84 * PI) / 180.0 )  /* 84 degrees in radians     */

#define MIN_EAST_NORTH 0
#define MAX_EAST_NORTH 4000000


/* Ellipsoid parameters, default to WGS 84 */
double USNG_a = 6378137.0;    /* Semi-major axis of ellipsoid in meters */
double USNG_f = 1 / 298.257223563; /* Flattening of ellipsoid           */
double USNG_recpf = 298.257223563;
char   USNG_Ellipsoid_Code[3] = {'W','E',0};


typedef struct Latitude_Band_Value
{
  long letter;            /* letter representing latitude band  */
  double min_northing;    /* minimum northing for latitude band */
  double north;           /* upper latitude for latitude band   */
  double south;           /* lower latitude for latitude band   */
  double northing_offset; /* latitude band northing offset      */
} Latitude_Band;

static const Latitude_Band Latitude_Band_Table[20] =
  {{LETTER_C, 1100000.0, -72.0, -80.5, 0.0}, 
  {LETTER_D, 2000000.0, -64.0, -72.0, 2000000.0},
  {LETTER_E, 2800000.0, -56.0, -64.0, 2000000.0},
  {LETTER_F, 3700000.0, -48.0, -56.0, 2000000.0},
  {LETTER_G, 4600000.0, -40.0, -48.0, 4000000.0},
  {LETTER_H, 5500000.0, -32.0, -40.0, 4000000.0},
  {LETTER_J, 6400000.0, -24.0, -32.0, 6000000.0},
  {LETTER_K, 7300000.0, -16.0, -24.0, 6000000.0},
  {LETTER_L, 8200000.0, -8.0, -16.0, 8000000.0},
  {LETTER_M, 9100000.0, 0.0, -8.0, 8000000.0},
  {LETTER_N, 0.0, 8.0, 0.0, 0.0},
  {LETTER_P, 800000.0, 16.0, 8.0, 0.0},
  {LETTER_Q, 1700000.0, 24.0, 16.0, 0.0},
  {LETTER_R, 2600000.0, 32.0, 24.0, 2000000.0},
  {LETTER_S, 3500000.0, 40.0, 32.0, 2000000.0},
  {LETTER_T, 4400000.0, 48.0, 40.0, 4000000.0},
  {LETTER_U, 5300000.0, 56.0, 48.0, 4000000.0},
  {LETTER_V, 6200000.0, 64.0, 56.0, 6000000.0},
  {LETTER_W, 7000000.0, 72.0, 64.0, 6000000.0},
  {LETTER_X, 7900000.0, 84.5, 72.0, 6000000.0}};


typedef struct UPS_Constant_Value
{
  long letter;            /* letter representing latitude band      */
  long ltr2_low_value;    /* 2nd letter range - low number         */
  long ltr2_high_value;   /* 2nd letter range - high number          */
  long ltr3_high_value;   /* 3rd letter range - high number (UPS)   */
  double false_easting;   /* False easting based on 2nd letter      */
  double false_northing;  /* False northing based on 3rd letter     */
} UPS_Constant;

static const UPS_Constant UPS_Constant_Table[4] =
  {{LETTER_A, LETTER_J, LETTER_Z, LETTER_Z, 800000.0, 800000.0},
  {LETTER_B, LETTER_A, LETTER_R, LETTER_Z, 2000000.0, 800000.0},
  {LETTER_Y, LETTER_J, LETTER_Z, LETTER_P, 800000.0, 1300000.0},
  {LETTER_Z, LETTER_A, LETTER_J, LETTER_P, 2000000.0, 1300000.0}};

/***************************************************************************/
/*
 *                              FUNCTIONS
 */

long USNG_Get_Latitude_Band_Min_Northing(long letter, double* min_northing, double* northing_offset)
/*
 * The function USNG_Get_Latitude_Band_Min_Northing receives a latitude band letter
 * and uses the Latitude_Band_Table to determine the minimum northing and northing offset
 * for that latitude band letter.
 *
 *   letter        : Latitude band letter             (input)
 *   min_northing  : Minimum northing for that letter (output)
 */
{ /* USNG_Get_Latitude_Band_Min_Northing */
  long error_code = USNG_NO_ERROR;

  if ((letter >= LETTER_C) && (letter <= LETTER_H))
  {
    *min_northing = Latitude_Band_Table[letter-2].min_northing;
    *northing_offset = Latitude_Band_Table[letter-2].northing_offset;
  }
  else if ((letter >= LETTER_J) && (letter <= LETTER_N))
  {
    *min_northing = Latitude_Band_Table[letter-3].min_northing;
    *northing_offset = Latitude_Band_Table[letter-3].northing_offset;
  }
  else if ((letter >= LETTER_P) && (letter <= LETTER_X))
  {
    *min_northing = Latitude_Band_Table[letter-4].min_northing;
    *northing_offset = Latitude_Band_Table[letter-4].northing_offset;
  }
  else
    error_code |= USNG_STRING_ERROR;

  return error_code;
} /* USNG_Get_Latitude_Band_Min_Northing */


long USNG_Get_Latitude_Range(long letter, double* north, double* south)
/*
 * The function USNG_Get_Latitude_Range receives a latitude band letter
 * and uses the Latitude_Band_Table to determine the latitude band
 * boundaries for that latitude band letter.
 *
 *   letter   : Latitude band letter                        (input)
 *   north    : Northern latitude boundary for that letter  (output)
 *   north    : Southern latitude boundary for that letter  (output)
 */
{ /* USNG_Get_Latitude_Range */
  long error_code = USNG_NO_ERROR;

  if ((letter >= LETTER_C) && (letter <= LETTER_H))
  {
    *north = Latitude_Band_Table[letter-2].north * DEG_TO_RAD;
    *south = Latitude_Band_Table[letter-2].south * DEG_TO_RAD;
  }
  else if ((letter >= LETTER_J) && (letter <= LETTER_N))
  {
    *north = Latitude_Band_Table[letter-3].north * DEG_TO_RAD;
    *south = Latitude_Band_Table[letter-3].south * DEG_TO_RAD;
  }
  else if ((letter >= LETTER_P) && (letter <= LETTER_X))
  {
    *north = Latitude_Band_Table[letter-4].north * DEG_TO_RAD;
    *south = Latitude_Band_Table[letter-4].south * DEG_TO_RAD;
  }
  else
    error_code |= USNG_STRING_ERROR;

  return error_code;
} /* USNG_Get_Latitude_Range */


long USNG_Get_Latitude_Letter(double latitude, int* letter)
/*
 * The function USNG_Get_Latitude_Letter receives a latitude value
 * and uses the Latitude_Band_Table to determine the latitude band
 * letter for that latitude.
 *
 *   latitude   : Latitude              (input)
 *   letter     : Latitude band letter  (output)
 */
{ /* USNG_Get_Latitude_Letter */
  double temp = 0.0;
  long error_code = USNG_NO_ERROR;
  double lat_deg = latitude * RAD_TO_DEG;

  if (lat_deg >= 72 && lat_deg < 84.5)
    *letter = LETTER_X;
  else if (lat_deg > -80.5 && lat_deg < 72)
  {
    temp = ((latitude + (80.0 * DEG_TO_RAD)) / (8.0 * DEG_TO_RAD)) + 1.0e-12;
    *letter = Latitude_Band_Table[(int)temp].letter;
  }
  else
    error_code |= USNG_LAT_ERROR;

  return error_code;
} /* USNG_Get_Latitude_Letter */


long USNG_Check_Zone(char* USNG, long* zone_exists)
/*
 * The function USNG_Check_Zone receives a USNG coordinate string.
 * If a zone is given, TRUE is returned. Otherwise, FALSE
 * is returned.
 *
 *   USNG           : USNG coordinate string        (input)
 *   zone_exists    : TRUE if a zone is given,
 *                    FALSE if a zone is not given  (output)
 */
{ /* USNG_Check_Zone */
  int i = 0;
  int j = 0;
  int num_digits = 0;
  long error_code = USNG_NO_ERROR;

  /* skip any leading blanks */
  while (USNG[i] == ' ')
    i++;
  j = i;
  while (isdigit(USNG[i]))
    i++;
  num_digits = i - j;
  if (num_digits <= 2)
    if (num_digits > 0)
      *zone_exists = TRUE;
    else
      *zone_exists = FALSE;
  else
    error_code |= USNG_STRING_ERROR;

  return error_code;
} /* USNG_Check_Zone */


long Make_USNG_String (char* USNG,
                       long Zone,
                       int Letters[USNG_LETTERS],
                       double Easting,
                       double Northing,
                       long Precision)
/*
 * The function Make_USNG_String constructs a USNG string
 * from its component parts.
 *
 *   USNG           : USNG coordinate string          (output)
 *   Zone           : UTM Zone                        (input)
 *   Letters        : USNG coordinate string letters  (input)
 *   Easting        : Easting value                   (input)
 *   Northing       : Northing value                  (input)
 *   Precision      : Precision level of USNG string  (input)
 */
{ /* Make_USNG_String */
  long i;
  long j;
  double divisor;
  long east;
  long north;
  char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  long error_code = USNG_NO_ERROR;

  i = 0;
  if (Zone)
    i = sprintf (USNG+i,"%2.2ld",Zone);
  else
    strcpy(USNG, "  ");  // 2 spaces - Should i be set to 2?

  for (j=0;j<3;j++)
    USNG[i++] = alphabet[Letters[j]];
  divisor = pow (10.0, (5 - Precision));
  Easting = fmod (Easting, 100000.0);
  if (Easting >= 99999.5)
    Easting = 99999.0;
  east = (long)(Easting/divisor);
  i += sprintf (USNG+i, "%*.*ld", (int)Precision, (int)Precision, east);
  Northing = fmod (Northing, 100000.0);
  if (Northing >= 99999.5)
    Northing = 99999.0;
  north = (long)(Northing/divisor);
  i += sprintf (USNG+i, "%*.*ld", (int)Precision, (int)Precision, north);
  return (error_code);
} /* Make_USNG_String */


long Break_USNG_String (char* USNG,
                        long* Zone,
                        long Letters[USNG_LETTERS],
                        double* Easting,
                        double* Northing,
                        long* Precision)
/*
 * The function Break_USNG_String breaks down a USNG
 * coordinate string into its component parts.
 *
 *   USNG           : USNG coordinate string          (input)
 *   Zone           : UTM Zone                        (output)
 *   Letters        : USNG coordinate string letters  (output)
 *   Easting        : Easting value                   (output)
 *   Northing       : Northing value                  (output)
 *   Precision      : Precision level of USNG string  (output)
 */
{ /* Break_USNG_String */
  long num_digits;
  long num_letters;
  long i = 0;
  long j = 0;
  long error_code = USNG_NO_ERROR;

  while (USNG[i] == ' ')
    i++;  /* skip any leading blanks */
  j = i;
  while (isdigit(USNG[i]))
    i++;
  num_digits = i - j;
  if (num_digits <= 2)
    if (num_digits > 0)
    {
      char zone_string[3];
      /* get zone */
      strncpy (zone_string, USNG+j, 2);
      zone_string[2] = 0;
      sscanf (zone_string, "%ld", Zone);
      if ((*Zone < 1) || (*Zone > 60))
        error_code |= USNG_STRING_ERROR;
    }
    else
      *Zone = 0;
  else
    error_code |= USNG_STRING_ERROR;
  j = i;

  while (isalpha(USNG[i]))
    i++;
  num_letters = i - j;
  if (num_letters == 3)
  {
    /* get letters */
    Letters[0] = (toupper(USNG[j]) - (long)'A');
    if ((Letters[0] == LETTER_I) || (Letters[0] == LETTER_O))
      error_code |= USNG_STRING_ERROR;
    Letters[1] = (toupper(USNG[j+1]) - (long)'A');
    if ((Letters[1] == LETTER_I) || (Letters[1] == LETTER_O))
      error_code |= USNG_STRING_ERROR;
    Letters[2] = (toupper(USNG[j+2]) - (long)'A');
    if ((Letters[2] == LETTER_I) || (Letters[2] == LETTER_O))
      error_code |= USNG_STRING_ERROR;
  }
  else
    error_code |= USNG_STRING_ERROR;
  j = i;
  while (isdigit(USNG[i]))
    i++;
  num_digits = i - j;
  if ((num_digits <= 10) && (num_digits%2 == 0))
  {
    long n;
    char east_string[6];
    char north_string[6];
    long east;
    long north;
    double multiplier;
    /* get easting & northing */
    n = num_digits/2;
    *Precision = n;
    if (n > 0)
    {
      strncpy (east_string, USNG+j, n);
      east_string[n] = 0;
      sscanf (east_string, "%ld", &east);
      strncpy (north_string, USNG+j+n, n);
      north_string[n] = 0;
      sscanf (north_string, "%ld", &north);
      multiplier = pow (10.0, 5 - n);
      *Easting = east * multiplier;
      *Northing = north * multiplier;
    }
    else
    {
      *Easting = 0.0;
      *Northing = 0.0;
    }
  }
  else
    error_code |= USNG_STRING_ERROR;

  return (error_code);
} /* Break_USNG_String */


void USNG_Get_Grid_Values (long zone,
                           long* ltr2_low_value,
                           long* ltr2_high_value,
                           double *pattern_offset)
/*
 * The function USNG_Get_Grid_Values sets the letter range used for
 * the 2nd letter in the USNG coordinate string, based on the set
 * number of the utm zone. It also sets the pattern offset using a
 * value of A for the second letter of the grid square, based on
 * the grid pattern and set number of the utm zone.
 *
 *    zone            : Zone number             (input)
 *    ltr2_low_value  : 2nd letter low number   (output)
 *    ltr2_high_value : 2nd letter high number  (output)
 *    pattern_offset  : Pattern offset          (output)
 */
{ /* BEGIN USNG_Get_Grid_Values */
  long set_number;    /* Set number (1-6) based on UTM zone number */

  set_number = zone % 6;

  if (!set_number)
    set_number = 6;

  if ((set_number == 1) || (set_number == 4))
  {
    *ltr2_low_value = LETTER_A;
    *ltr2_high_value = LETTER_H;
  }
  else if ((set_number == 2) || (set_number == 5))
  {
    *ltr2_low_value = LETTER_J;
    *ltr2_high_value = LETTER_R;
  }
  else if ((set_number == 3) || (set_number == 6))
  {
    *ltr2_low_value = LETTER_S;
    *ltr2_high_value = LETTER_Z;
  }

  /* False northing at A for second letter of grid square */
  if ((set_number % 2) ==  0)
    *pattern_offset = 500000.0;
  else
    *pattern_offset = 0.0;

} /* END OF USNG_Get_Grid_Values */


long UTM_To_USNG (long Zone,
                  double Latitude,
                  double Easting,
                  double Northing,
                  long Precision,
                  char *USNG)
/*
 * The function UTM_To_USNG calculates a USNG coordinate string
 * based on the zone, latitude, easting and northing.
 *
 *    Zone      : Zone number             (input)
 *    Latitude  : Latitude in radians     (input)
 *    Easting   : Easting                 (input)
 *    Northing  : Northing                (input)
 *    Precision : Precision               (input)
 *    USNG      : USNG coordinate string  (output)
 */
{ /* BEGIN UTM_To_USNG */
  double pattern_offset;      /* Pattern offset for 3rd letter               */
  double grid_northing;       /* Northing used to derive 3rd letter of USNG  */
  long ltr2_low_value;        /* 2nd letter range - low number               */
  long ltr2_high_value;       /* 2nd letter range - high number              */
  int letters[USNG_LETTERS];  /* Number location of 3 letters in alphabet    */
  double divisor;
  long error_code = USNG_NO_ERROR;

  /* Round easting and northing values */
  divisor = pow (10.0, (5 - Precision));
  Easting = (long)(Easting/divisor) * divisor;
  Northing = (long)(Northing/divisor) * divisor;

  if( Latitude <= 0.0 && Northing == 1.0e7)
  {
    Latitude = 0.0;
    Northing = 0.0;
  }

  ltr2_low_value = LETTER_A;	// Make compiler shut up about possibly uninitialized value.
				// It should be set by the following but compiler doesn't know.

  USNG_Get_Grid_Values(Zone, &ltr2_low_value, &ltr2_high_value, &pattern_offset);

  error_code = USNG_Get_Latitude_Letter(Latitude, &letters[0]);

  if (!error_code)
  {
    grid_northing = Northing;

    while (grid_northing >= TWOMIL)
    {
      grid_northing = grid_northing - TWOMIL;
    }
    grid_northing = grid_northing + pattern_offset;
    if(grid_northing >= TWOMIL)
      grid_northing = grid_northing - TWOMIL;

    letters[2] = (long)(grid_northing / ONEHT);
    if (letters[2] > LETTER_H)
      letters[2] = letters[2] + 1;

    if (letters[2] > LETTER_N)
      letters[2] = letters[2] + 1;

    letters[1] = ltr2_low_value + ((long)(Easting / ONEHT) -1);
    if ((ltr2_low_value == LETTER_J) && (letters[1] > LETTER_N))
      letters[1] = letters[1] + 1;

    Make_USNG_String (USNG, Zone, letters, Easting, Northing, Precision);
  }
  return error_code;
} /* END UTM_To_USNG */


long Set_USNG_Parameters (double a,
                          double f,
                          char   *Ellipsoid_Code)
/*
 * The function SET_USNG_PARAMETERS receives the ellipsoid parameters and sets
 * the corresponding state variables. If any errors occur, the error code(s)
 * are returned by the function, otherwise USNG_NO_ERROR is returned.
 *
 *   a                : Semi-major axis of ellipsoid in meters  (input)
 *   f                : Flattening of ellipsoid                 (input)
 *   Ellipsoid_Code   : 2-letter code for ellipsoid             (input)
 */
{ /* Set_USNG_Parameters  */

  double inv_f = 1 / f;
  long Error_Code = USNG_NO_ERROR;

  if (a <= 0.0)
  { /* Semi-major axis must be greater than zero */
    Error_Code |= USNG_A_ERROR;
  }
  if ((inv_f < 250) || (inv_f > 350))
  { /* Inverse flattening must be between 250 and 350 */
    Error_Code |= USNG_INV_F_ERROR;
  }
  if (!Error_Code)
  { /* no errors */
    USNG_a = a;
    USNG_f = f;
    USNG_recpf = inv_f;
    strcpy (USNG_Ellipsoid_Code, Ellipsoid_Code);
  }
  return (Error_Code);
}  /* Set_USNG_Parameters  */


void Get_USNG_Parameters (double *a,
                          double *f,
                          char* Ellipsoid_Code)
/*
 * The function Get_USNG_Parameters returns the current ellipsoid
 * parameters.
 *
 *  a                : Semi-major axis of ellipsoid, in meters (output)
 *  f                : Flattening of ellipsoid                 (output)
 *  Ellipsoid_Code   : 2-letter code for ellipsoid             (output)
 */
{ /* Get_USNG_Parameters */
  *a = USNG_a;
  *f = USNG_f;
  strcpy (Ellipsoid_Code, USNG_Ellipsoid_Code);
  return;
} /* Get_USNG_Parameters */


long Convert_Geodetic_To_USNG (double Latitude,
                               double Longitude,
                               long Precision,
                               char* USNG)
/*
 * The function Convert_Geodetic_To_USNG converts Geodetic (latitude and
 * longitude) coordinates to a USNG coordinate string, according to the
 * current ellipsoid parameters.  If any errors occur, the error code(s)
 * are returned by the function, otherwise USNG_NO_ERROR is returned.
 *
 *    Latitude   : Latitude in radians              (input)
 *    Longitude  : Longitude in radians             (input)
 *    Precision  : Precision level of USNG string   (input)
 *    USNG       : USNG coordinate string           (output)
 *
 */
{ /* Convert_Geodetic_To_USNG */
  long zone;
  char hemisphere;
  double easting;
  double northing;
  long temp_error_code = USNG_NO_ERROR;
  long error_code = USNG_NO_ERROR;

  if ((Latitude < -PI_OVER_2) || (Latitude > PI_OVER_2))
  { /* Latitude out of range */
    error_code |= USNG_LAT_ERROR;
  }
  if ((Longitude < -PI) || (Longitude > (2*PI)))
  { /* Longitude out of range */
    error_code |= USNG_LON_ERROR;
  }
  if ((Precision < 0) || (Precision > MAX_PRECISION))
    error_code |= USNG_PRECISION_ERROR;
  if (!error_code)
  {
    if ((Latitude < MIN_UTM_LAT) || (Latitude > MAX_UTM_LAT))
    {
      temp_error_code = Set_UPS_Parameters (USNG_a, USNG_f);
      if(!temp_error_code)
      {
        temp_error_code |= Convert_Geodetic_To_UPS (Latitude, Longitude, &hemisphere, &easting, &northing);
        if(!temp_error_code)
          error_code |= Convert_UPS_To_USNG (hemisphere, easting, northing, Precision, USNG);
        else
        {
          if(temp_error_code & UPS_LAT_ERROR)
            error_code |= USNG_LAT_ERROR;
          if(temp_error_code & UPS_LON_ERROR)
            error_code |= USNG_LON_ERROR;
        }
      }
      else
      {
        if(temp_error_code & UPS_A_ERROR)
          error_code |= USNG_A_ERROR;
        if(temp_error_code & UPS_INV_F_ERROR)
          error_code |= USNG_INV_F_ERROR;
      }
    }
    else
    {
      temp_error_code = Set_UTM_Parameters (USNG_a, USNG_f, 0);
      if(!temp_error_code)
      {
        temp_error_code |= Convert_Geodetic_To_UTM (Latitude, Longitude, &zone, &hemisphere, &easting, &northing);
        if(!temp_error_code)
          error_code |= UTM_To_USNG (zone, Latitude, easting, northing, Precision, USNG);
        else
        {
          if(temp_error_code & UTM_LAT_ERROR)
            error_code |= USNG_LAT_ERROR;
          if(temp_error_code & UTM_LON_ERROR)
            error_code |= USNG_LON_ERROR;
          if(temp_error_code & UTM_ZONE_OVERRIDE_ERROR)
            error_code |= USNG_ZONE_ERROR;
          if(temp_error_code & UTM_EASTING_ERROR)
            error_code |= USNG_EASTING_ERROR;
          if(temp_error_code & UTM_NORTHING_ERROR)
            error_code |= USNG_NORTHING_ERROR;
        }
      }
      else
      {
        if(temp_error_code & UTM_A_ERROR)
          error_code |= USNG_A_ERROR;
        if(temp_error_code & UTM_INV_F_ERROR)
          error_code |= USNG_INV_F_ERROR;
        if(temp_error_code & UTM_ZONE_OVERRIDE_ERROR)
          error_code |= USNG_ZONE_ERROR;
      }
    }
  }
  return (error_code);
} /* Convert_Geodetic_To_USNG */


long Convert_USNG_To_Geodetic (char* USNG,
                               double *Latitude,
                               double *Longitude)
/*
 * The function Convert_USNG_To_Geodetic converts a USNG coordinate string
 * to Geodetic (latitude and longitude) coordinates
 * according to the current ellipsoid parameters.  If any errors occur,
 * the error code(s) are returned by the function, otherwise UTM_NO_ERROR
 * is returned.
 *
 *    USNG       : USNG coordinate string           (input)
 *    Latitude   : Latitude in radians              (output)
 *    Longitude  : Longitude in radians             (output)
 *
 */
{ /* Convert_USNG_To_Geodetic */
  long zone;
  char hemisphere = '?';
  double easting;
  double northing;
  long zone_exists;
  long temp_error_code = USNG_NO_ERROR;
  long error_code = USNG_NO_ERROR;

  error_code = USNG_Check_Zone(USNG, &zone_exists);
  if (!error_code)
  {
    if (zone_exists)
    {
      error_code |= Convert_USNG_To_UTM (USNG, &zone, &hemisphere, &easting, &northing);
      if(!error_code || (error_code & USNG_LAT_WARNING))
      {
        temp_error_code = Set_UTM_Parameters (USNG_a, USNG_f, 0);
        if(!temp_error_code)
        {
          temp_error_code |= Convert_UTM_To_Geodetic (zone, hemisphere, easting, northing, Latitude, Longitude);
          if(temp_error_code)
          {
            if((temp_error_code & UTM_ZONE_ERROR) || (temp_error_code & UTM_HEMISPHERE_ERROR))
              error_code |= USNG_STRING_ERROR;
            if(temp_error_code & UTM_EASTING_ERROR)
              error_code |= USNG_EASTING_ERROR;
            if(temp_error_code & UTM_NORTHING_ERROR)
              error_code |= USNG_NORTHING_ERROR;
          }
        }
        else
        {
          if(temp_error_code & UTM_A_ERROR)
            error_code |= USNG_A_ERROR;
          if(temp_error_code & UTM_INV_F_ERROR)
            error_code |= USNG_INV_F_ERROR;
          if(temp_error_code & UTM_ZONE_OVERRIDE_ERROR)
            error_code |= USNG_ZONE_ERROR;
        }
      }
    }
    else
    {
      error_code |= Convert_USNG_To_UPS (USNG, &hemisphere, &easting, &northing);
      if(!error_code)
      {
        temp_error_code = Set_UPS_Parameters (USNG_a, USNG_f);
        if(!temp_error_code)
        {
          temp_error_code |= Convert_UPS_To_Geodetic (hemisphere, easting, northing, Latitude, Longitude);
          if(temp_error_code)
          {
            if(temp_error_code & UPS_HEMISPHERE_ERROR)
              error_code |= USNG_STRING_ERROR;
            if(temp_error_code & UPS_EASTING_ERROR)
              error_code |= USNG_EASTING_ERROR;
            if(temp_error_code & UPS_LAT_ERROR)
              error_code |= USNG_NORTHING_ERROR;
          }
        }
        else
        {
          if(temp_error_code & UPS_A_ERROR)
            error_code |= USNG_A_ERROR;
          if(temp_error_code & UPS_INV_F_ERROR)
            error_code |= USNG_INV_F_ERROR;
        }
      }
    }
  }
  return (error_code);
} /* END OF Convert_USNG_To_Geodetic */


long Convert_UTM_To_USNG (long Zone,
                          char Hemisphere,
                          double Easting,
                          double Northing,
                          long Precision,
                          char* USNG)
/*
 * The function Convert_UTM_To_USNG converts UTM (zone, easting, and
 * northing) coordinates to a USNG coordinate string, according to the
 * current ellipsoid parameters.  If any errors occur, the error code(s)
 * are returned by the function, otherwise USNG_NO_ERROR is returned.
 *
 *    Zone       : UTM zone                         (input)
 *    Hemisphere : North or South hemisphere        (input)
 *    Easting    : Easting (X) in meters            (input)
 *    Northing   : Northing (Y) in meters           (input)
 *    Precision  : Precision level of USNG string   (input)
 *    USNG       : USNG coordinate string           (output)
 */
{ /* Convert_UTM_To_USNG */
  double latitude;           /* Latitude of UTM point */
  double longitude;          /* Longitude of UTM point */
  long utm_error_code = USNG_NO_ERROR;
  long error_code = USNG_NO_ERROR;

  if ((Zone < 1) || (Zone > 60))
    error_code |= USNG_ZONE_ERROR;
  if ((Hemisphere != 'S') && (Hemisphere != 'N'))
    error_code |= USNG_HEMISPHERE_ERROR;
  if ((Easting < MIN_EASTING) || (Easting > MAX_EASTING))
    error_code |= USNG_EASTING_ERROR;
  if ((Northing < MIN_NORTHING) || (Northing > MAX_NORTHING))
    error_code |= USNG_NORTHING_ERROR;
  if ((Precision < 0) || (Precision > MAX_PRECISION))
    error_code |= USNG_PRECISION_ERROR;
  if (!error_code)
  {
    Set_UTM_Parameters (USNG_a, USNG_f, 0);
    utm_error_code = Convert_UTM_To_Geodetic (Zone, Hemisphere, Easting, Northing, &latitude, &longitude);

    if(utm_error_code)
    {
      if((utm_error_code & UTM_ZONE_ERROR) || (utm_error_code & UTM_HEMISPHERE_ERROR))
        error_code |= USNG_STRING_ERROR;
      if(utm_error_code & UTM_EASTING_ERROR)
        error_code |= USNG_EASTING_ERROR;
      if(utm_error_code & UTM_NORTHING_ERROR)
        error_code |= USNG_NORTHING_ERROR;
    }

    error_code |= UTM_To_USNG (Zone, latitude, Easting, Northing, Precision, USNG);
  }
  return (error_code);
} /* Convert_UTM_To_USNG */


long Convert_USNG_To_UTM (char   *USNG,
                          long   *Zone,
                          char   *Hemisphere,
                          double *Easting,
                          double *Northing)
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
{ /* Convert_USNG_To_UTM */
  double min_northing;
  double northing_offset;
  long ltr2_low_value;
  long ltr2_high_value;
  double pattern_offset;
  double upper_lat_limit;     /* North latitude limits based on 1st letter  */
  double lower_lat_limit;     /* South latitude limits based on 1st letter  */
  double grid_easting;        /* Easting for 100,000 meter grid square      */
  double grid_northing;       /* Northing for 100,000 meter grid square     */
  long letters[USNG_LETTERS];
  long in_precision;
  double latitude = 0.0;
  double longitude = 0.0;
  double divisor = 1.0;
  long utm_error_code = USNG_NO_ERROR;
  long error_code = USNG_NO_ERROR;

  error_code = Break_USNG_String (USNG, Zone, letters, Easting, Northing, &in_precision);
  if (!*Zone)
    error_code |= USNG_STRING_ERROR;
  else
  {
    if (!error_code)
    {
      if ((letters[0] == LETTER_X) && ((*Zone == 32) || (*Zone == 34) || (*Zone == 36)))
        error_code |= USNG_STRING_ERROR;
      else
      {
        if (letters[0] < LETTER_N)
          *Hemisphere = 'S';
        else
          *Hemisphere = 'N';

        ltr2_low_value = LETTER_A;	// Make compiler shut up about possibly uninitialized values.
	ltr2_high_value = LETTER_Z;	// They should be set by the following but compiler doesn't know.

        USNG_Get_Grid_Values(*Zone, &ltr2_low_value, &ltr2_high_value, &pattern_offset);

        /* Check that the second letter of the USNG string is within
         * the range of valid second letter values
         * Also check that the third letter is valid */
        if ((letters[1] < ltr2_low_value) || (letters[1] > ltr2_high_value) || (letters[2] > LETTER_V))
          error_code |= USNG_STRING_ERROR;

        if (!error_code)
        {
          double row_letter_northing = (double)(letters[2]) * ONEHT;
          grid_easting = (double)((letters[1]) - ltr2_low_value + 1) * ONEHT;
          if ((ltr2_low_value == LETTER_J) && (letters[1] > LETTER_O))
            grid_easting = grid_easting - ONEHT;

          if (letters[2] > LETTER_O)
            row_letter_northing = row_letter_northing - ONEHT;

          if (letters[2] > LETTER_I)
            row_letter_northing = row_letter_northing - ONEHT; 

          if (row_letter_northing >= TWOMIL)
            row_letter_northing = row_letter_northing - TWOMIL;

          error_code = USNG_Get_Latitude_Band_Min_Northing(letters[0], &min_northing, &northing_offset);
          if (!error_code)
          {
            grid_northing = row_letter_northing - pattern_offset;
            if(grid_northing < 0)
              grid_northing += TWOMIL;
            
            grid_northing += northing_offset;

            if(grid_northing < min_northing)
              grid_northing += TWOMIL;

            *Easting = grid_easting + *Easting;
            *Northing = grid_northing + *Northing;

            /* check that point is within Zone Letter bounds */
            utm_error_code = Set_UTM_Parameters(USNG_a, USNG_f, 0);
            if (!utm_error_code)
            {
              utm_error_code = Convert_UTM_To_Geodetic(*Zone,*Hemisphere,*Easting,*Northing,&latitude,&longitude);
              if (!utm_error_code)
              {
                divisor = pow (10.0, in_precision);
                error_code = USNG_Get_Latitude_Range(letters[0], &upper_lat_limit, &lower_lat_limit);
                if (!error_code)
                {
                  if (!(((lower_lat_limit - DEG_TO_RAD/divisor) <= latitude) && (latitude <= (upper_lat_limit + DEG_TO_RAD/divisor))))
                    error_code |= USNG_LAT_ERROR;
                }
              }
              else
              {
                if((utm_error_code & UTM_ZONE_ERROR) || (utm_error_code & UTM_HEMISPHERE_ERROR))
                  error_code |= USNG_STRING_ERROR;
                if(utm_error_code & UTM_EASTING_ERROR)
                  error_code |= USNG_EASTING_ERROR;
                if(utm_error_code & UTM_NORTHING_ERROR)
                  error_code |= USNG_NORTHING_ERROR;
              }
            }
            else
            {
              if(utm_error_code & UTM_A_ERROR)
                error_code |= USNG_A_ERROR;
              if(utm_error_code & UTM_INV_F_ERROR)
                error_code |= USNG_INV_F_ERROR;
              if(utm_error_code & UTM_ZONE_OVERRIDE_ERROR)
                error_code |= USNG_ZONE_ERROR;
            }
          }
        }
      }
    }
  }
  return (error_code);
} /* Convert_USNG_To_UTM */


long Convert_UPS_To_USNG (char   Hemisphere,
                          double Easting,
                          double Northing,
                          long   Precision,
                          char*  USNG)
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
{ /* Convert_UPS_To_USNG */
  double false_easting;       /* False easting for 2nd letter                 */
  double false_northing;      /* False northing for 3rd letter                */
  double grid_easting;        /* Easting used to derive 2nd letter of USNG    */
  double grid_northing;       /* Northing used to derive 3rd letter of USNG   */
  long ltr2_low_value;        /* 2nd letter range - low number                */
  int letters[USNG_LETTERS];  /* Number location of 3 letters in alphabet     */
  double divisor;
  int index = 0;
  long error_code = USNG_NO_ERROR;

  if ((Hemisphere != 'N') && (Hemisphere != 'S'))
    error_code |= USNG_HEMISPHERE_ERROR;
  if ((Easting < MIN_EAST_NORTH) || (Easting > MAX_EAST_NORTH))
    error_code |= USNG_EASTING_ERROR;
  if ((Northing < MIN_EAST_NORTH) || (Northing > MAX_EAST_NORTH))
    error_code |= USNG_NORTHING_ERROR;
  if ((Precision < 0) || (Precision > MAX_PRECISION))
    error_code |= USNG_PRECISION_ERROR;
  if (!error_code)
  {
    divisor = pow (10.0, (5 - Precision));
    Easting = (long)(Easting/divisor + 1.0e-9) * divisor;
    Northing = (long)(Northing/divisor) * divisor;

    if (Hemisphere == 'N')
    {
      if (Easting >= TWOMIL)
        letters[0] = LETTER_Z;
      else
        letters[0] = LETTER_Y;

      index = letters[0] - 22;
      ltr2_low_value = UPS_Constant_Table[index].ltr2_low_value;
      false_easting = UPS_Constant_Table[index].false_easting;
      false_northing = UPS_Constant_Table[index].false_northing;
    }
    else
    {
      if (Easting >= TWOMIL)
        letters[0] = LETTER_B;
      else
        letters[0] = LETTER_A;

      ltr2_low_value = UPS_Constant_Table[letters[0]].ltr2_low_value;
      false_easting = UPS_Constant_Table[letters[0]].false_easting;
      false_northing = UPS_Constant_Table[letters[0]].false_northing;
    }

    grid_northing = Northing;
    grid_northing = grid_northing - false_northing;
    letters[2] = (long)(grid_northing / ONEHT);

    if (letters[2] > LETTER_H)
      letters[2] = letters[2] + 1;

    if (letters[2] > LETTER_N)
      letters[2] = letters[2] + 1;

    grid_easting = Easting;
    grid_easting = grid_easting - false_easting;
    letters[1] = ltr2_low_value + ((long)(grid_easting / ONEHT));

    if (Easting < TWOMIL)
    {
      if (letters[1] > LETTER_L)
        letters[1] = letters[1] + 3;

      if (letters[1] > LETTER_U)
        letters[1] = letters[1] + 2;
    }
    else
    {
      if (letters[1] > LETTER_C)
        letters[1] = letters[1] + 2;

      if (letters[1] > LETTER_H)
        letters[1] = letters[1] + 1;

      if (letters[1] > LETTER_L)
        letters[1] = letters[1] + 3;
    }

    Make_USNG_String (USNG, 0, letters, Easting, Northing, Precision);
  }
  return (error_code);
} /* Convert_UPS_To_USNG */


long Convert_USNG_To_UPS ( char   *USNG,
                           char   *Hemisphere,
                           double *Easting,
                           double *Northing)
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
{ /* Convert_USNG_To_UPS */
  long ltr2_high_value;       /* 2nd letter range - high number             */
  long ltr3_high_value;       /* 3rd letter range - high number (UPS)       */
  long ltr2_low_value;        /* 2nd letter range - low number              */
  double false_easting;       /* False easting for 2nd letter               */
  double false_northing;      /* False northing for 3rd letter              */
  double grid_easting;        /* easting for 100,000 meter grid square      */
  double grid_northing;       /* northing for 100,000 meter grid square     */
  long zone = 0;
  long letters[USNG_LETTERS];
  long in_precision = 0;
  int index = 0;
  long error_code = USNG_NO_ERROR;

  error_code = Break_USNG_String (USNG, &zone, letters, Easting, Northing, &in_precision);
  if (zone)
    error_code |= USNG_STRING_ERROR;
  else
  {
    if (!error_code)
    {
      if (letters[0] >= LETTER_Y)
      {
        *Hemisphere = 'N';

        index = letters[0] - 22;
        ltr2_low_value = UPS_Constant_Table[index].ltr2_low_value;
        ltr2_high_value = UPS_Constant_Table[index].ltr2_high_value;
        ltr3_high_value = UPS_Constant_Table[index].ltr3_high_value;
        false_easting = UPS_Constant_Table[index].false_easting;
        false_northing = UPS_Constant_Table[index].false_northing;
      }
      else
      {
        *Hemisphere = 'S';

        ltr2_low_value = UPS_Constant_Table[letters[0]].ltr2_low_value;
        ltr2_high_value = UPS_Constant_Table[letters[0]].ltr2_high_value;
        ltr3_high_value = UPS_Constant_Table[letters[0]].ltr3_high_value;
        false_easting = UPS_Constant_Table[letters[0]].false_easting;
        false_northing = UPS_Constant_Table[letters[0]].false_northing;
      }

      /* Check that the second letter of the USNG string is within
       * the range of valid second letter values
       * Also check that the third letter is valid */
      if ((letters[1] < ltr2_low_value) || (letters[1] > ltr2_high_value) ||
          ((letters[1] == LETTER_D) || (letters[1] == LETTER_E) ||
          (letters[1] == LETTER_M) || (letters[1] == LETTER_N) ||
          (letters[1] == LETTER_V) || (letters[1] == LETTER_W)) ||
          (letters[2] > ltr3_high_value))
          error_code = USNG_STRING_ERROR;

      if (!error_code)
      {
        grid_northing = (double)letters[2] * ONEHT + false_northing;
        if (letters[2] > LETTER_I)
          grid_northing = grid_northing - ONEHT;

        if (letters[2] > LETTER_O)
          grid_northing = grid_northing - ONEHT;

        grid_easting = (double)((letters[1]) - ltr2_low_value) * ONEHT + false_easting;
        if (ltr2_low_value != LETTER_A)
        {
          if (letters[1] > LETTER_L)
            grid_easting = grid_easting - 300000.0;

          if (letters[1] > LETTER_U)
            grid_easting = grid_easting - 200000.0;
        }
        else
        {
          if (letters[1] > LETTER_C)
            grid_easting = grid_easting - 200000.0;

          if (letters[1] > LETTER_I)
            grid_easting = grid_easting - ONEHT;

          if (letters[1] > LETTER_L)
            grid_easting = grid_easting - 300000.0;
        }

        *Easting = grid_easting + *Easting;
        *Northing = grid_northing + *Northing;
      }
    }
  }
  return (error_code);
} /* Convert_USNG_To_UPS */
