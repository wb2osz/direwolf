
/* dwgps.h */

#ifndef DWGPS_H
#define DWGPS_H 1


#include <time.h>
#include "config.h"	/* for struct misc_config_s */


/*
 * Values for fix, equivalent to values from libgps.
 *	-2 = not initialized.
 *	-1 = error communicating with GPS receiver.
 *	0 = nothing heard yet.
 *	1 = had signal but lost it.
 *	2 = 2D.
 *	3 = 3D.
 *
 * Undefined float & double values are set to G_UNKNOWN.
 *
 */

enum dwfix_e { DWFIX_NOT_INIT= -2, DWFIX_ERROR= -1, DWFIX_NOT_SEEN=0, DWFIX_NO_FIX=1, DWFIX_2D=2, DWFIX_3D=3 };

typedef enum dwfix_e dwfix_t;

typedef struct dwgps_info_s {
	time_t timestamp;	/* When last updated.  System time. */
	dwfix_t fix;		/* Quality of position fix. */
	double dlat;		/* Latitude.  Valid if fix >= 2. */
	double dlon;		/* Longitude. Valid if fix >= 2. */
	float speed_knots;	/* libgps uses meters/sec but we use GPS usual knots. */
	float track;		/* What is difference between track and course? */
	float altitude;		/* meters above mean sea level. Valid if fix == 3. */
} dwgps_info_t;





void dwgps_init (struct misc_config_s *pconfig, int debug);

void dwgps_clear (dwgps_info_t *gpsinfo);

dwfix_t dwgps_read (dwgps_info_t *gpsinfo);

void dwgps_print (char *msg, dwgps_info_t *gpsinfo);

void dwgps_set_data (dwgps_info_t *gpsinfo);


#endif /* DWGPS_H 1 */

/* end dwgps.h */



