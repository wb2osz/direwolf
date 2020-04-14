
/* 
 * Name:	server.h
 */


#include "ax25_pad.h"		/* for packet_t */

#include "config.h"


void server_set_debug (int n);

void server_init (struct audio_s *audio_config_p, struct misc_config_s *misc_config);

void server_send_rec_packet (int chan, packet_t pp, unsigned char *fbuf,  int flen);

void server_send_monitored (int chan, packet_t pp, int own_xmit);

int server_callsign_registered_by_client (char *callsign);


void server_link_established (int chan, int client, char *remote_call, char *own_call, int incoming);

void server_link_terminated (int chan, int client, char *remote_call, char *own_call, int timeout);

void server_rec_conn_data (int chan, int client, char *remote_call, char *own_call, int pid, char *data_ptr, int data_len);

void server_outstanding_frames_reply (int chan, int client, char *own_call, char *remote_call, int count);


/* end server.h */
