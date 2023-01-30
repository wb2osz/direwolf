

extern double dtime_realtime (void);

extern double dtime_monotonic (void);


void timestamp_now (char *result, int result_size, int show_ms);

void timestamp_user_format (char *result, int result_size, char *user_format);

void timestamp_filename (char *result, int result_size);


// FIXME:  remove temp workaround.
// Needs many scattered updates.

#define dtime_now dtime_realtime
