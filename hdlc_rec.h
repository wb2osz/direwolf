

#include "audio.h"


void hdlc_rec_init (struct audio_s *pa);

void hdlc_rec_bit (int chan, int subchan, int slice, int raw, int is_scrambled, int descram_state);

/* Provided elsewhere to process a complete frame. */

//void process_rec_frame (int chan, unsigned char *fbuf, int flen, int level);


/* Is HLDC decoder is currently gathering bits into a frame? */
/* Similar to, but not exactly the same as, data carrier detect. */
/* We use this to influence the PLL inertia. */

int hdlc_rec_gathering (int chan, int subchan, int slice);

/* Transmit needs to know when someone else is transmitting. */

void dcd_change (int chan, int subchan, int slice, int state);

int hdlc_rec_data_detect_any (int chan);
