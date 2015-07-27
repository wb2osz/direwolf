
/* tt_user.h */


void tt_user_init (struct tt_config_s *p);

int tt_user_heard (char *callsign, int ssid, char overlay, char symbol, double latitude, 
		double longitude, char *freq, char *comment, char mic_e, char *dao);

void tt_user_background (void);