
/* kiss_frame.h */


/*
 * Special characters used by SLIP protocol.
 */

#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD



enum kiss_state_e {
	KS_SEARCHING,		/* Looking for FEND to start KISS frame. */
	KS_COLLECTING,		/* In process of collecting KISS frame. */
	KS_ESCAPE };		/* FESC found in frame. */

#define MAX_KISS_LEN 2048	/* Spec calls for at least 1024. */

#define MAX_NOISE_LEN 100

typedef struct kiss_frame_s {
	
	enum kiss_state_e state;

	unsigned char kiss_msg[MAX_KISS_LEN];
	int kiss_len;

	unsigned char noise[MAX_NOISE_LEN];
	int noise_len;

} kiss_frame_t;



int kiss_frame (kiss_frame_t *kf, unsigned char ch, int debug, void (*sendfun)(int,unsigned char*,int)); 
 
void kiss_process_msg (kiss_frame_t *kf, int debug);

typedef enum fromto_e { FROM_CLIENT=0, TO_CLIENT=1 } fromto_t;

void kiss_debug_print (fromto_t fromto, char *special, unsigned char *pmsg, int msg_len);

/* end kiss_frame.h */