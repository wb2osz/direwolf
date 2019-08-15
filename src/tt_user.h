
/* tt_user.h */


#include "audio.h"

void tt_user_init (struct audio_s *p_audio_config, struct tt_config_s *p);

int tt_user_heard (char *callsign, int ssid, char overlay, char symbol, char *loc_text, double latitude, 
		double longitude, int ambiguity, char *freq, char *ctcss, char *comment, char mic_e, char *dao);

int tt_3char_suffix_search (char *suffix, char *callsign);

void tt_user_background (void);
void tt_user_dump (void);