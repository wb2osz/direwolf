

#include "audio.h"

#include "rrbb.h"		/* Possibly defines SLICENDICE. */


void hdlc_rec_init (struct audio_s *pa);

#if SLICENDICE
void hdlc_rec_bit_sam (int chan, int subchan, int raw, float demod_out);
#else
void hdlc_rec_bit (int chan, int subchan, int raw, int is_scrambled, int descram_state);
#endif


/* Provided elsewhere to process a complete frame. */

//void process_rec_frame (int chan, unsigned char *fbuf, int flen, int level);

/* Transmit needs to know when someone else is transmitting. */

int hdlc_rec_data_detect_1 (int chan, int subchan);
int hdlc_rec_data_detect_any (int chan);
