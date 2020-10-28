/* morse.h */

int morse_init (struct audio_s *audio_config_p, int amp) ;

int morse_send (int chan, char *str, int wpm, int txdelay, int txtail);

#define MORSE_DEFAULT_WPM 10

