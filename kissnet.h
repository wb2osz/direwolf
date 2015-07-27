
/* 
 * Name:	kissnet.h
 */


#include "ax25_pad.h"		/* for packet_t */

#include "config.h"




void kissnet_init (struct misc_config_s *misc_config);

void kissnet_send_rec_packet (int chan, unsigned char *fbuf,  int flen);

void kiss_net_set_debug (int n);


/* end kissnet.h */
