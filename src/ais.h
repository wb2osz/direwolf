

void ais_to_nmea (unsigned char *ais, int ais_len, char *nema, int nema_size);

int ais_parse (char *sentence, int quiet, char *descr, int descr_size, char *mssi, int mssi_size, double *odlat, double *odlon,
			float *ofknots, float *ofcourse, float *ofalt_m, char *symtab, char *symbol, char *comment, int comment_size);

int ais_check_length (int type, int length);
