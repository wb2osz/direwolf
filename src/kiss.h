
/* 
 * Name:	kiss.h
 *
 * This is for the pseudo terminal KISS interface.
 */


#include "ax25_pad.h"		/* for packet_t */

#include "config.h"

#include "kiss_frame.h"		// for struct kissport_status_s


void kisspt_init (struct misc_config_s *misc_config);

void kisspt_send_rec_packet (int chan, int kiss_cmd, unsigned char *fbuf,  int flen,
		struct kissport_status_s *notused1, int notused2);

void kisspt_set_debug (int n);


/* end kiss.h */
