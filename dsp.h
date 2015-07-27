
/* dsp.h */

// TODO:  put prefixes on these names.

float window (bp_window_t type, int size, int j);

void gen_lowpass (float fc, float *lp_filter, int filter_size, bp_window_t wtype);

void gen_bandpass (float f1, float f2, float *bp_filter, int filter_size, bp_window_t wtype);