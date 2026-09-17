

/* mheard.h */

#include "decode_aprs.h"	// for decode_aprs_t


typedef struct mheard_times_s {

	char callsign[AX25_MAX_ADDR_LEN];	// Callsign from the AX.25 source field.

	int chan;				// Channel with which these times are associated.

    time_t first_heard;		// Timestamp when first heard on this channel.

	time_t last_heard;		// Timestamp when last heard on this channel.

} mheard_times_t;


void mheard_init (int debug);

void mheard_save_rf (int chan, decode_aprs_t *A, packet_t pp, alevel_t alevel, retry_t retries);

void mheard_save_is (char *ptext);

int mheard_count (int max_hops, int time_limit);

int mheard_was_recently_nearby (char *role, char *callsign, int time_limit, int max_hops, double dlat, double dlon, double km);

void mheard_set_msp (char *callsign, int num);

int mheard_get_msp (char *callsign);

int mheard_latest_for_channel (int chan, mheard_times_t *times, int num_times);

int mheard_latest_for_is (mheard_times_t *times, int num_times);
