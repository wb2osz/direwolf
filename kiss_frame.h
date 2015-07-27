
/* kiss_frame.h */

#include "audio.h"		/* for struct audio_s */


/*
 * Special characters used by SLIP protocol.
 */

#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD


enum kiss_state_e {
	KS_SEARCHING,		/* Looking for FEND to start KISS frame. */
	KS_COLLECTING};		/* In process of collecting KISS frame. */


#define MAX_KISS_LEN 2048	/* Spec calls for at least 1024. */
				/* Might want to make it longer to accomodate */
				/* maximum packet length. */

#define MAX_NOISE_LEN 100

typedef struct kiss_frame_s {
	
	enum kiss_state_e state;

	unsigned char kiss_msg[MAX_KISS_LEN];
				/* Leading FEND is optional. */
				/* Contains escapes and ending FEND. */
	int kiss_len;

	unsigned char noise[MAX_NOISE_LEN];
	int noise_len;

} kiss_frame_t;


void kiss_frame_init (struct audio_s *pa);

int kiss_encapsulate (unsigned char *in, int ilen, unsigned char *out);

void kiss_rec_byte (kiss_frame_t *kf, unsigned char ch, int debug, void (*sendfun)(int,unsigned char*,int)); 
 

typedef enum fromto_e { FROM_CLIENT=0, TO_CLIENT=1 } fromto_t;

void kiss_debug_print (fromto_t fromto, char *special, unsigned char *pmsg, int msg_len);

/* end kiss_frame.h */