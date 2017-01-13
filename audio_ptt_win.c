
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015  John Langner, WB2OSZ
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
 * Module:      audio_ptt_win.c
 *
 * Purpose:   	Interface to audio device commonly called a "sound card" for
 *		historical reasons.		
 *
 *		This version uses the native Windows sound interface.
 *
 *---------------------------------------------------------------*/

#if __WIN32__
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <io.h>
#include <fcntl.h>
#include <math.h>
#include <limits.h>

#include <windows.h>		
#include <mmsystem.h>

#ifndef WAVE_FORMAT_96M16
#define WAVE_FORMAT_96M16 0x40000
#define WAVE_FORMAT_96S16 0x80000
#endif

#include "direwolf.h"
#include "audio.h"
#include "audio_stats.h"
#include "textcolor.h"
#include "ptt.h"
#include "demod.h"		/* for alevel_t & demod_get_audio_level() */
#include "audio_ptt.h"

static struct audio_s          *save_audio_config_p;

static unsigned __stdcall ptt_thread ( void *arg );

HANDLE start_ptt_thread (struct audio_s *pa , int ch)
{
    save_audio_config_p = pa;

    return (HANDLE)_beginthreadex (NULL, 0, ptt_thread, (void*)(long)ch, 0, NULL);
}

unsigned __stdcall ptt_thread (void *arg)
{
    WAVEFORMATEX wf;
    HWAVEOUT hWaveOut;
    int ch = (int)(long)arg; // channel number.
    int channel = save_audio_config_p->achan[ch].octrl[OCTYPE_PTT].ptt_channel;
    int freq = save_audio_config_p->achan[channel].octrl[OCTYPE_PTT].ptt_frequency;
    int a = ACHAN2ADEV( channel );
    int err;

    if( save_audio_config_p->adev[a].defined ) {
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = save_audio_config_p->adev[a].num_channels;
        wf.nSamplesPerSec = save_audio_config_p->adev[a].samples_per_sec;
        wf.wBitsPerSample = save_audio_config_p->adev[a].bits_per_sample;
        wf.nBlockAlign = ( wf.wBitsPerSample / 8 ) * wf.nChannels;
        wf.nAvgBytesPerSec = wf.nBlockAlign * wf.nSamplesPerSec;
        wf.cbSize = 0;

          /*
          * Open the audio output device.
          * Soundcard is only possibility at this time.
          */

        err = waveOutOpen ( &hWaveOut, atoi( save_audio_config_p->adev[a].adevice_out ), &wf, (DWORD_PTR)NULL, 0, CALLBACK_NULL );
        if( err == MMSYSERR_NOERROR ) {
            WAVEHDR waveHeader;
            SHORT* pnData;
            SHORT sample;
            int nsamples = save_audio_config_p->adev[a].samples_per_sec / freq;
            int i;

            if( save_audio_config_p->adev[a].num_channels == 1 ) {
                waveHeader.dwBufferLength = 1 * nsamples * sizeof( SHORT );
            }
            else {
                waveHeader.dwBufferLength = 2 * nsamples * sizeof( SHORT );
            }
            waveHeader.lpData = malloc( waveHeader.dwBufferLength );
            waveHeader.dwUser = 0;
            waveHeader.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
            waveHeader.dwLoops = 0xFFFF;

            pnData = (SHORT*)waveHeader.lpData;

            if( save_audio_config_p->adev[a].num_channels == 1 ) {
                for( i = 0; i < nsamples; i++ ) {
                    sample = (SHORT)( (double)SHRT_MAX * sin( ( (double)i / (double)nsamples ) * 2.0 * M_PI ) );
                    pnData[i] = sample;
                }
            }
            else {
                for( i = 0; i < nsamples; i++ ) {
                    sample = (SHORT)( (double)SHRT_MAX * sin( ( (double)i / (double)nsamples ) * 2.0 * M_PI ) );
                    if( channel == ADEVFIRSTCHAN( a ) ) {

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

            err = waveOutPrepareHeader ( hWaveOut, &waveHeader, sizeof( WAVEHDR ) );
            if( err == MMSYSERR_NOERROR ) {
                HANDLE handles[3];
                DWORD dwWait;

                handles[0] = save_audio_config_p->achan[ch].octrl[OCTYPE_PTT].ptt_start;
                handles[1] = save_audio_config_p->achan[ch].octrl[OCTYPE_PTT].ptt_stop;
                handles[2] = save_audio_config_p->achan[ch].octrl[OCTYPE_PTT].ptt_close;

                while( 1 )
                {
                    dwWait = WaitForMultipleObjects ( 3, handles, FALSE, INFINITE );

                    if( dwWait == WAIT_OBJECT_0 + 0 )
                    {
                        //
                        // ptt_set on
                        //

                        waveOutWrite ( hWaveOut, &waveHeader, sizeof( WAVEHDR ) );
                    }
                    else if( dwWait == WAIT_OBJECT_0 + 1 )
                    {
                        //
                        // ptt_set off
                        //

                        waveOutReset ( hWaveOut );
                    }
                    else if( dwWait == WAIT_OBJECT_0 + 2 )
                    {
                        //
                        // close
                        //

                        waveOutReset ( hWaveOut );
                        waveOutUnprepareHeader ( hWaveOut, &waveHeader, sizeof( WAVEHDR ) );

                        break;
                    }
                }
            }

            waveOutClose ( hWaveOut );

            free( waveHeader.lpData );
        }
    }

    return 0;
}
#endif