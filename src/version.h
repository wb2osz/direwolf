
/* Dire Wolf version 1.6 */

// Put in destination field to identify the equipment used.

#define APP_TOCALL "APDW"		// Assigned by WB4APR in tocalls.txt

// This now comes from compile command line options.

//#define MAJOR_VERSION 1
//#define MINOR_VERSION 6
//#define EXTRA_VERSION "Beta Test"


// For user-defined data format.
// APRS protocol spec Chapter 18 and http://www.aprs.org/aprs11/expfmts.txt

#define USER_DEF_USER_ID 'D'		// user id D for direwolf

#define USER_DEF_TYPE_AIS 'A'		// data type A for AIS NMEA sentence
#define USER_DEF_TYPE_EAS 'E'		// data type E for EAS broadcasts
