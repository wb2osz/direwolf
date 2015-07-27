
#ifndef HDLC_REC2_H
#define HDLC_REC2_H 1


#include "ax25_pad.h"	/* for packet_t, alevel_t */
#include "rrbb.h"
#include "audio.h"		/* for struct audio_s */




typedef enum retry_mode_e {
		RETRY_MODE_CONTIGUOUS=0,
		RETRY_MODE_SEPARATED=1,
		}  retry_mode_t;

typedef enum retry_type_e {
		RETRY_TYPE_NONE=0,
		RETRY_TYPE_SWAP=1,
		RETRY_TYPE_REMOVE=2,
		RETRY_TYPE_INSERT=3}  retry_type_t;

typedef struct retry_conf_s {
	retry_t      retry;
        retry_mode_t mode;
        retry_type_t type;
        union {
                struct {
                        int bit_idx_a; /*  */
                        int bit_idx_b; /*  */
                        int bit_idx_c; /*  */
                } sep;       /* RETRY_MODE_SEPARATED */

                struct {
                        int bit_idx;
			int nr_bits;
                } contig;  /* RETRY_MODE_CONTIGUOUS */

        } u_bits;
	int insert_value;

} retry_conf_t;




#if defined(DIREWOLF_C) || defined(ATEST_C) || defined(UDPTEST_C)

static const char * retry_text[] = {
		"NONE",
		"SINGLE",
		"DOUBLE",
		"TRIPLE",
		"REMOVE_SINGLE",
		"REMOVE_DOUBLE",
		"REMOVE_TRIPLE",
		"INSERT_SINGLE",
		"INSERT_DOUBLE",
		"TWO_SEP",
		"MANY",
		"REMOVE_MANY",
		"REMOVE_SEP",
		"PASSALL" };
#endif

void hdlc_rec2_init (struct audio_s *audio_config_p);

void hdlc_rec2_block (rrbb_t block);

int hdlc_rec2_try_to_fix_later (rrbb_t block, int chan, int subchan, alevel_t alevel);

/* Provided by the top level application to process a complete frame. */

void app_process_rec_packet (int chan, int subchan, packet_t pp, alevel_t level, retry_t retries, char *spectrum);


#endif
