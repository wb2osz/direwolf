

/* telemetry.h */

void telemetry_data_original (char *station, char *info, int quiet, char *output, size_t outputsize, char *comment, size_t commentsize);
 
void telemetry_data_base91 (char *station, char *cdata, char *output, size_t outputsize);
 
void telemetry_name_message (char *station, char *msg);
 
void telemetry_unit_label_message (char *station, char *msg);

void telemetry_coefficents_message (char *station, char *msg, int quiet);

void telemetry_bit_sense_message (char *station, char *msg, int quiet);
