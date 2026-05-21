//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2025
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
 * Module:      fm_demod.c
 *
 * Purpose:     FM demodulator for IQ input streams.
 *              Converts complex float IQ samples to real audio samples
 *              using quadrature demodulation (phase difference method).
 *
 * Description: This implements a simple but effective FM demodulation
 *              algorithm based on the arctangent of the phase difference
 *              between consecutive IQ samples.
 *
 *              The phase difference is proportional to the instantaneous
 *              frequency, which is what we want for FM demodulation.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "fm_demod.h"
#include "textcolor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* FM demodulator state */
struct fm_demod_state_s {
    int sample_rate;          /* IQ sample rate in Hz */
    float max_deviation;      /* Maximum frequency deviation in Hz */
    float last_i;             /* Previous I sample for quadrature discriminator */
    float last_q;             /* Previous Q sample for quadrature discriminator */
};


/*------------------------------------------------------------------
 *
 * Name:        fm_demod_init
 *
 * Purpose:     Initialize FM demodulator state.
 *
 * Inputs:      sample_rate    - IQ sample rate in Hz (e.g., 48000)
 *              max_deviation  - Maximum frequency deviation in Hz
 *                               (e.g., 5000 for narrow FM APRS, 2500 for tighter)
 *
 * Returns:     Pointer to allocated and initialized state structure
 *              NULL on error
 *
 *----------------------------------------------------------------*/

struct fm_demod_state_s *fm_demod_init(int sample_rate, float max_deviation)
{
    struct fm_demod_state_s *state;

    if (sample_rate <= 0 || max_deviation <= 0) {
        text_color_set(DW_COLOR_ERROR);
        dw_printf("fm_demod_init: Invalid parameters sample_rate=%d max_deviation=%.1f\n",
                  sample_rate, max_deviation);
        return NULL;
    }

    state = calloc(1, sizeof(struct fm_demod_state_s));
    if (state == NULL) {
        text_color_set(DW_COLOR_ERROR);
        dw_printf("fm_demod_init: Out of memory\n");
        return NULL;
    }

    state->sample_rate = sample_rate;
    state->max_deviation = max_deviation;
    state->last_i = 0.0f;
    state->last_q = 0.0f;

    text_color_set(DW_COLOR_INFO);
    dw_printf("FM demodulator initialized: %d Hz sample rate, %.1f Hz max deviation\n",
              sample_rate, max_deviation);

    return state;
}


/*------------------------------------------------------------------
 *
 * Name:        fm_demod_process
 *
 * Purpose:     Demodulate IQ samples to audio using quadrature FM demodulation.
 *
 * Inputs:      state         - Demodulator state
 *              iq_samples    - Interleaved I/Q samples (I0,Q0,I1,Q1,...)
 *                              as float values
 *              num_iq_pairs  - Number of I/Q pairs to process
 *              audio_out     - Output buffer for audio samples (float)
 *
 * Outputs:     audio_out     - Filled with demodulated audio samples
 *                              Will contain num_iq_pairs samples
 *
 * Description: Uses the phase difference method:
 *              1. Calculate phase difference between consecutive samples
 *              2. Phase difference is proportional to frequency
 *              3. Apply DC blocking and de-emphasis filtering
 *
 *              Phase difference calculation using:
 *              φ = atan2(I[n]*Q[n-1] - Q[n]*I[n-1], I[n]*I[n-1] + Q[n]*Q[n-1])
 *
 *              This is more numerically stable than calculating individual
 *              phase angles and subtracting them.
 *
 *----------------------------------------------------------------*/

void fm_demod_process(struct fm_demod_state_s *state,
                      const float *iq_samples,
                      int num_iq_pairs,
                      float *audio_out)
{
    int i;
    if (state == NULL || iq_samples == NULL || audio_out == NULL) {
        return;
    }

    /* Use csdr's quadrature discriminator method:
     * output = K * (I[n]*(Q[n]-Q[n-1]) - Q[n]*(I[n]-I[n-1])) / (I[n]^2 + Q[n]^2)
     * This is the cross-product method, normalized by magnitude squared.
     * K = 0.340447... is csdr's scaling constant */
    const float K = 0.340447550238101026565118445432744920253753662109375f;
    
    for (i = 0; i < num_iq_pairs; i++) {
        float curr_i = iq_samples[i * 2];
        float curr_q = iq_samples[i * 2 + 1];
        float di, dq, mag_sq, output;
        
        /* Calculate differences from previous sample */
        if (i == 0) {
            /* Use stored last sample for first iteration */
            di = curr_i - state->last_i;
            dq = curr_q - state->last_q;
        } else {
            di = curr_i - iq_samples[(i-1) * 2];
            dq = curr_q - iq_samples[(i-1) * 2 + 1];
        }
        
        /* Calculate magnitude squared */
        mag_sq = curr_i * curr_i + curr_q * curr_q;
        
        /* Quadrature discriminator formula */
        /* Protect against division by zero or very small magnitudes */
        if (mag_sq > 1e-5f) {
            output = K * (curr_i * dq - curr_q * di) / mag_sq;
            /* Clamp to reasonable range and check for inf/nan */
            if (!isfinite(output)) output = 0;
            if (output > 100.0f) output = 100.0f;
            if (output < -100.0f) output = -100.0f;
        } else {
            output = 0;
        }
        
        audio_out[i] = output;
    }
    
    /* Save last sample for next call */
    if (num_iq_pairs > 0) {
        state->last_i = iq_samples[(num_iq_pairs-1) * 2];
        state->last_q = iq_samples[(num_iq_pairs-1) * 2 + 1];
    }
}


/*------------------------------------------------------------------
 *
 * Name:        fm_demod_term
 *
 * Purpose:     Clean up and free FM demodulator resources.
 *
 * Inputs:      state - Demodulator state to free
 *
 *----------------------------------------------------------------*/

void fm_demod_term(struct fm_demod_state_s *state)
{
    if (state != NULL) {
        free(state);
    }
}
