
/* 
 * Name:	kissserial.h
 */


#include "ax25_pad.h"		/* for packet_t */

#include "config.h"

#include "kiss_frame.h"


void kissserial_init (struct misc_config_s *misc_config);

void kissserial_send_rec_packet (int chan, int kiss_cmd, unsigned char *fbuf,  int flen,
		struct kissport_status_s *notused1, int notused2);


void kissserial_set_debug (int n);


/* end kissserial.h */
