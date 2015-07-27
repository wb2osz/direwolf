
/* latlong.h */


/* Use this value for unknown latitude/longitude or other values. */

#define G_UNKNOWN (-999999)


void latitude_to_str (double dlat, int ambiguity, char *slat);
void longitude_to_str (double dlong, int ambiguity, char *slong);

void latitude_to_comp_str (double dlat, char *clat);
void longitude_to_comp_str (double dlon, char *clon);

void latitude_to_nmea (double dlat, char *slat, char *hemi);
void longitude_to_nmea (double dlong, char *slong, char *hemi);

double latitude_from_nmea (char *pstr, char *phemi);
double longitude_from_nmea (char *pstr, char *phemi);
