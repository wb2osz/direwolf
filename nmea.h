
/* 
 * Name:	nmea.h
 */


#include "ax25_pad.h"		/* for packet_t */

#include "config.h"		/* for struct misc_config_s */


void nmea_init (struct misc_config_s *misc_config);

void nmea_set_debug (int n);

void nmea_send_waypoint (char *wname_in, double dlat, double dlong, char symtab, char symbol, 
			float alt, float course, float speed, char *comment);

/* end nmea.h */
