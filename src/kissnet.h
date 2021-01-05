
/* 
 * Name:	kissnet.h
 */

#ifndef KISSNET_H
#define KISSNET_H

#include "ax25_pad.h"		/* for packet_t */

#include "config.h"

#include "kiss_frame.h"



void kissnet_init (struct misc_config_s *misc_config);

void kissnet_send_rec_packet (int chan, int kiss_cmd, unsigned char *fbuf,  int flen,
			struct kissport_status_s *onlykps, int onlyclient);

void kiss_net_set_debug (int n);

void kissnet_copy (unsigned char *kiss_msg, int kiss_len, int chan, int cmd, struct kissport_status_s *from_kps, int from_client);


#endif  // KISSNET_H

/* end kissnet.h */
