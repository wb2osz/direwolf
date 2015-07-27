
/* latlong.h */


/* Use this value for unknown latitude/longitude or other values. */

#define G_UNKNOWN (-999999)


void latitude_to_str (double dlat, int ambiguity, char *slat);
void longitude_to_str (double dlong, int ambiguity, char *slong);
void latitude_to_comp_str (double dlat, char *clat);
void longitude_to_comp_str (double dlon, char *clon);
