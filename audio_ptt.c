// 
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015  John Langner, WB2OSZ
//    Copyright (C) 2017  Andrew Walker, VA7YAA
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//


/*------------------------------------------------------------------
 *
 * Module:      audio_ptt.c
 *
 * Purpose:   	Interface to audio device commonly called a "sound card" for
 *		historical reasons.		
 *
 *		This version uses the native Windows sound interface.
 *
 *---------------------------------------------------------------*/

#if __WIN32__
#else
#include <limits.h>
#include <math.h>
#include <pthread.h>

#if USE_ALSA
#include <alsa/asoundlib.h>
#else
#include <errno.h>
#ifdef __OpenBSD__
#include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#endif

#include "direwolf.h"
#include "audio.h"
#include "audio_stats.h"
#include "textcolor.h"
#include "ptt.h"
#include "audio_ptt.h"

#if USE_ALSA
static int set_alsa_params (int a, snd_pcm_t *handle, struct audio_s *pa, char *name, char *dir);
//static void alsa_select_device (char *pick_dev, int direction, char *result);
#else
static int set_oss_params (int a, int fd, struct audio_s *pa);
#endif

static struct audio_s          *save_audio_config_p;

static void * ptt_thread (void *arg);

int start_ptt_thread (struct audio_s *pa, int ch)
{
    pthread_t tid = 0;
    int e;

    save_audio_config_p = pa;

    e = pthread_create (&tid, NULL, ptt_thread, (void*)(long)ch);
    
    return tid;
}

static void * ptt_thread (void *arg) 
{
  int ch = (int)(long)arg; // channel number.
  int channel = save_audio_config_p->achan[ch].octrl[OCTYPE_PTT].ptt_channel;
  int freq = save_audio_config_p->achan[channel].octrl[OCTYPE_PTT].ptt_frequency;
  int a = ACHAN2ADEV( channel );

  if( save_audio_config_p->adev[a].defined ) {
#if USE_ALSA
    snd_pcm_t *handle;
    int err;

	  err = snd_pcm_open(&handle, save_audio_config_p->adev[a].adevice_out, SND_PCM_STREAM_PLAYBACK, 0);
	  if (err == 0) {
		  snd_pcm_sframes_t frames;
		  snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;

	    err = snd_pcm_set_params(handle, format, SND_PCM_ACCESS_RW_INTERLEAVED,
			                  save_audio_config_p->adev[a].num_channels,
			                  save_audio_config_p->adev[a].samples_per_sec, 1, 500000);
  	  if (err == 0) {
		    short* pnData;
		    short sample;
		    int nSamples = save_audio_config_p->adev[a].samples_per_sec / 10;
		    int nBufferLength = save_audio_config_p->adev[a].num_channels * nSamples * sizeof(short);
		    int i;

		    pnData = (short*)malloc (nBufferLength);

		    if (save_audio_config_p->adev[a].num_channels == 1) {
			    for (i = 0; i < nSamples; i++) {
			      sample = (short)( (double)SHRT_MAX * sin( ( (double)i * freq / (double)save_audio_config_p->adev[a].samples_per_sec ) * 2.0 * M_PI ) );
			      pnData[i] = sample;
			    }
		    }
		    else {
			    for (i = 0; i < nSamples; i++) {
			      sample = (short)( (double)SHRT_MAX * sin( ( (double)i * freq / (double)save_audio_config_p->adev[a].samples_per_sec ) * 2.0 * M_PI ) );
			      if (channel == ADEVFIRSTCHAN( a )) {

			        // Stereo, left channel.

			        pnData[i*2 + 0] = sample;
			        pnData[i*2 + 1] = 0;
			      }
			      else {

			        // Stereo, right channel.

			        pnData[i*2 + 0] = 0;
			        pnData[i*2 + 1] = sample;
			      }
			    }
		    }       
		    
		    //
		    // ptt_set on
		    //

		    for (i=0; i<50; i++) {
			    frames = snd_pcm_writei(handle, pnData, nSamples);
		    }

		    //
		    // ptt_set off
		    //

		    //
		    // close
		    //

		    free (pnData);
		  }

		  snd_pcm_close(handle);
	  }
#else
    int oss_audio_device_fd;

    oss_audio_device_fd = open (save_audio_config_p->adev[a].adevice_out, O_WRONLY);
    if (oss_audio_device_fd != -1) {

    }
#endif
  }
}

#endif
