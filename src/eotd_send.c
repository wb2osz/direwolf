#include "direwolf.h"

#include <stdio.h>

#include "eotd_send.h"
#include "audio.h"
#include "gen_tone.h"
#include "eotd_defs.h"

#define EOTD_SILENCE_SAMPLES 1000
//#define EOTD_SEND_DEBUG

static int eotd_fs[] = {1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0};
static int hotd_fs[] = {1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1 };

void my_tone_gen_put_bit (int chan, int bit) {

#ifdef EOTD_SEND_DEBUG
	printf("mytone bit %d\n", bit);
#endif
	tone_gen_put_bit (chan, bit);
}

void my_gen_tone_put_sample (int chan, int a, int sam) {

#ifdef EOTD_SEND_DEBUG
	printf("mysilence sample %d\n", sam);
#endif
	gen_tone_put_sample (chan, a, sam);
}

void send_preamble(int chan, char type) {
	int bit = 0;
	int preamble_count;
	int fs_count;
        int *fs;

	if (type == EOTD_TYPE_R2F) {
	  bit = 0;
	  preamble_count = 69;
	  fs_count = sizeof(eotd_fs) / sizeof(int);
	  fs = eotd_fs;
        } else {
          preamble_count = 456;
	  fs_count = sizeof(hotd_fs) / sizeof(int);
	  fs = hotd_fs;
	}

	for (int i = 0; i < preamble_count; i++) {
	  my_tone_gen_put_bit (chan, bit);
	  bit ^= 1;
	}

#ifdef EOTD_SEND_DEBUG
	printf("end-of-preamble\n");
#endif
	// send FS
	for (int i = 0; i < fs_count; i++) {
	  my_tone_gen_put_bit (chan, fs[i]);
	}

#ifdef EOTD_SEND_DEBUG
	printf("end-of-fs\n");
#endif
}

void send_silence(int chan) {
	int a = ACHAN2ADEV(chan);

	for (int i = 0; i < EOTD_SILENCE_SAMPLES; i++) {
	  my_gen_tone_put_sample (chan, a, 0);
	}
}

int eotd_send_block (int chan, char *str, char type) {

	unsigned int b[EOTD_LENGTH];

        int status = sscanf(str, "%x %x %x %x %x %x %x %x", b, b+1, b+2, b+3, b+4, b+5, b+6, b+7);
        if (status != EOTD_LENGTH) {
                    fprintf(stderr, "Error: expected 8, read %d", status);
		    return -1;
        }

	send_preamble(chan, type);

	for (int i = 7; i >= 0; i--) {
	    int byte = b[i];	// Copy this non-destructively so we can repeat it later, per spec.
	    for (int j = 0; j < 8; j++) {
	      int bit = byte & 0x01;
	      byte >>= 1;
	      my_tone_gen_put_bit (chan, bit);
	  }
        }

#ifdef EOTD_SEND_DEBUG
	printf("end-of-data\n");
#endif
	send_silence(chan);

	return 0;
}
