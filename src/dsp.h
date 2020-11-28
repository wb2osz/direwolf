
/* dsp.h */

// TODO:  put prefixes on these names.

float window (bp_window_t type, int size, int j);

void gen_lowpass (float fc, float *lp_filter, int filter_size, bp_window_t wtype);

void gen_bandpass (float f1, float f2, float *bp_filter, int filter_size, bp_window_t wtype);

void gen_ms (int fc, int samples_per_sec, float *sin_table, float *cos_table, int filter_size, int wtype);


__attribute__((const)) float rrc (float t, float a);

void gen_rrc_lowpass (float *pfilter, int filter_taps, float rolloff, float samples_per_symbol);
