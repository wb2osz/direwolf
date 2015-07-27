

/* telemetry.h */

void telemetry_data_original (char *station, char *info, int quiet, char *output, char *comment);
 
void telemetry_data_base91 (char *station, char *cdata, char *output);
 
void telemetry_name_message (char *station, char *msg);
 
void telemetry_unit_label_message (char *station, char *msg);

void telemetry_coefficents_message (char *station, char *msg, int quiet);

void telemetry_bit_sense_message (char *station, char *msg, int quiet);
