
/* hdlc_send.h */

// In version 1.7 an extra layer of abstraction was added here.
// Rather than calling hdlc_send_frame, we now use another function
// which sends AX.25, FX.25, or IL2P depending on mode.

// eas_send fits here logically because it also serializes a packet.


#include "ax25_pad.h"
#include "audio.h"

int layer2_send_frame (int chan, packet_t pp, int bad_fcs, struct audio_s *audio_config_p);

int layer2_preamble_postamble (int chan, int flags, int finish, struct audio_s *audio_config_p);

int eas_send (int chan, unsigned char *str, int repeat, int txdelay, int txtail);

/* end hdlc_send.h */


