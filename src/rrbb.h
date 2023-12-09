
#ifndef RRBB_H

#define RRBB_H


#define FASTER13 1		// Don't pack 8 samples per byte.


//typedef short slice_t;


/* 
 * Maximum size (in bytes) of an AX.25 frame including the 2 octet FCS. 
 */

#define MAX_FRAME_LEN ((AX25_MAX_PACKET_LEN) + 2)	

/*
 * Maximum number of bits in AX.25 frame excluding the flags.
 * Adequate for extreme case of bit stuffing after every 5 bits
 * which could never happen.
 */

#define MAX_NUM_BITS (MAX_FRAME_LEN * 8 * 6 / 5)

typedef struct rrbb_s {
	int magic1;
	struct rrbb_s* nextp;	/* Next pointer to maintain a queue. */

	int chan;		/* Radio channel from which it was received. */
	int subchan;		/* Which modem when more than one per channel. */
	int slice;		/* Which slicer. */

	alevel_t alevel;	/* Received audio level at time of frame capture. */
	float speed_error;	/* Received data speed error as percentage. */
	unsigned int len;	/* Current number of samples in array. */

	int is_scrambled;	/* Is data scrambled G3RUH / K9NG style? */
	int descram_state;	/* Descrambler state before first data bit of frame. */
	int prev_descram;	/* Previous descrambled bit. */

	unsigned char fdata[MAX_NUM_BITS];

	int magic2;
} *rrbb_t;



rrbb_t rrbb_new (int chan, int subchan, int slice, int is_scrambled, int descram_state, int prev_descram);

void rrbb_clear (rrbb_t b, int is_scrambled, int descram_state, int prev_descram);


static inline /*__attribute__((always_inline))*/ void rrbb_append_bit (rrbb_t b, const unsigned char val)
{
	if (b->len >= MAX_NUM_BITS) {
	  return;	/* Silently discard if full. */
	}
	b->fdata[b->len] = val;
	b->len++;
}

static inline /*__attribute__((always_inline))*/ unsigned char rrbb_get_bit (const rrbb_t b, const int ind)
{
	return (b->fdata[ind]);
}


void rrbb_chop8 (rrbb_t b);

int rrbb_get_len (rrbb_t b);

//void rrbb_flip_bit (rrbb_t b, unsigned int ind);

void rrbb_delete (rrbb_t b);

void rrbb_set_nextp (rrbb_t b, rrbb_t np);
rrbb_t rrbb_get_nextp (rrbb_t b);

int rrbb_get_chan (rrbb_t b);
int rrbb_get_subchan (rrbb_t b);
int rrbb_get_slice (rrbb_t b);

void rrbb_set_audio_level (rrbb_t b, alevel_t alevel);
alevel_t rrbb_get_audio_level (rrbb_t b);

void rrbb_set_speed_error (rrbb_t b, float speed_error);
float rrbb_get_speed_error (rrbb_t b);

int rrbb_get_is_scrambled (rrbb_t b);
int rrbb_get_descram_state (rrbb_t b);
int rrbb_get_prev_descram (rrbb_t b);


#endif
