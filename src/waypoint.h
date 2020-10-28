
/* 
 * Name:	waypoint.h
 */


#include "ax25_pad.h"		/* for packet_t */

#include "config.h"		/* for struct misc_config_s */


void waypoint_init (struct misc_config_s *misc_config);

void waypoint_set_debug (int n);

void waypoint_send_sentence (char *wname_in, double dlat, double dlong, char symtab, char symbol, 
			float alt, float course, float speed, char *comment_in);

void waypoint_send_ais (char *sentence);

void waypoint_term ();


/* end waypoint.h */
