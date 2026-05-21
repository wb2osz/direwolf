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
 * Module:      fm_demod.h
 *
 * Purpose:     FM demodulator for IQ input streams.
 *              Converts complex float IQ samples to real audio samples
 *              using quadrature demodulation.
 *
 *---------------------------------------------------------------*/

#ifndef FM_DEMOD_H
#define FM_DEMOD_H 1

#include <stdint.h>

/* Opaque structure for FM demodulator state */
struct fm_demod_state_s;

/*
 * Initialize FM demodulator
 * 
 * sample_rate - IQ sample rate in Hz (e.g., 48000)
 * max_deviation - Maximum frequency deviation in Hz (e.g., 5000 for narrow FM)
 * 
 * Returns: Pointer to demodulator state structure, or NULL on error
 */
struct fm_demod_state_s *fm_demod_init(int sample_rate, float max_deviation);

/*
 * Process IQ samples and produce audio output
 * 
 * state - Demodulator state from fm_demod_init()
 * iq_samples - Pointer to interleaved I/Q float samples
 * num_iq_pairs - Number of I/Q pairs (so there are num_iq_pairs*2 floats)
 * audio_out - Output buffer for demodulated audio samples (floats)
 * 
 * The output will have num_iq_pairs samples.
 * Audio samples will be in the range approximately -1.0 to +1.0
 */
void fm_demod_process(struct fm_demod_state_s *state, 
                      const float *iq_samples, 
                      int num_iq_pairs,
                      float *audio_out);

/*
 * Clean up and free FM demodulator resources
 */
void fm_demod_term(struct fm_demod_state_s *state);

#endif /* FM_DEMOD_H */
