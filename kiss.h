
/* 
 * Name:	kiss.h
 */


#include "ax25_pad.h"		/* for packet_t */

#include "config.h"




void kiss_init (struct misc_config_s *misc_config);

void kiss_send_rec_packet (int chan, unsigned char *fbuf,  int flen);

void kiss_serial_set_debug (int n);


/* end kiss.h */
