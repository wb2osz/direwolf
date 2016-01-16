
/* 
 * Name:	server.h
 */


#include "ax25_pad.h"		/* for packet_t */

#include "config.h"


void server_set_debug (int n);

void server_init (struct audio_s *audio_config_p, struct misc_config_s *misc_config);

void server_send_rec_packet (int chan, packet_t pp, unsigned char *fbuf,  int flen);

int server_callsign_registered_by_client (char *callsign);



/* end server.h */
