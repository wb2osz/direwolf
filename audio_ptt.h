
#ifndef AUDIO_PTT_H
#define AUDIO_PTT_H 1

#if __WIN32__
extern HANDLE start_ptt_thread ( struct audio_s *pa, int ch );
#else
extern int start_ptt_thread ( struct audio_s *pa, int ch );
#endif

#endif