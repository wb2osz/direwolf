
/* pfilter.h */


#include "igate.h"		// for igate_config_s



void pfilter_init (struct igate_config_s *p_igate_config, int debug_level);

int pfilter (int from_chan, int to_chan, char *filter, packet_t pp, int is_aprs);

int is_telem_metadata (char *infop);