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


/********************************************************************************
 *
 * File:	rrbb.c
 *
 * Purpose:	Raw Received Bit Buffer.
 *		An array of bits used to hold data out of
 *		the demodulator before feeding it into the HLDC decoding.
 *
 * Version 1.2: Save initial state of 9600 baud descrambler so we can
 *		attempt bit fix up on G3RUH/K9NG scrambled data.
 *
 * Version 1.3:	Store as bytes rather than packing 8 bits per byte.
 *
 *******************************************************************************/

#define RRBB_C

#include "direwolf.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "textcolor.h"
#include "ax25_pad.h"
#include "rrbb.h"


#define MAGIC1 0x12344321
#define MAGIC2 0x56788765


volatile static int new_count = 0;
volatile static int delete_count = 0;


/***********************************************************************************
 *
 * Name:	rrbb_new	
 *
 * Purpose:	Allocate space for an array of samples.
 *
 * Inputs:	chan	- Radio channel from whence it came.
 *
 *		subchan	- Which demodulator of the channel.
 *
 *		slice	- multiple thresholds per demodulator.
 *
 *		is_scrambled - Is data scrambled? (true, false)
 *
 *		descram_state - State of data descrambler.
 *
 *		prev_descram - Previous descrambled bit.
 *
 * Returns:	Handle to be used by other functions.
 *		
 * Description:	
 *
 ***********************************************************************************/

rrbb_t rrbb_new (int chan, int subchan, int slice, int is_scrambled, int descram_state, int prev_descram)
{
	rrbb_t result;

	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);
	assert (slice >= 0 && slice < MAX_SLICERS);

	result = malloc(sizeof(struct rrbb_s));
	if (result == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	result->magic1 = MAGIC1;
	result->chan = chan;
	result->subchan = subchan;
	result->slice = slice;
	result->magic2 = MAGIC2;

	new_count++;

	if (new_count > delete_count + 100) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("MEMORY LEAK, rrbb_new, new_count=%d, delete_count=%d\n", new_count, delete_count);
	}

	rrbb_clear (result, is_scrambled, descram_state, prev_descram);

	return (result);
}

/***********************************************************************************
 *
 * Name:	rrbb_clear	
 *
 * Purpose:	Clear by setting length to zero, etc.
 *
 * Inputs:	b 		-Handle for sample array.
 *
 *		is_scrambled 	- Is data scrambled? (true, false)
 *
 *		descram_state 	- State of data descrambler.
 *
 *		prev_descram 	- Previous descrambled bit.
 *
 ***********************************************************************************/

void rrbb_clear (rrbb_t b, int is_scrambled, int descram_state, int prev_descram)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	assert (is_scrambled == 0 || is_scrambled == 1);
	assert (prev_descram == 0 || prev_descram == 1);

	b->nextp = NULL;

	b->alevel.rec = 9999;	// TODO: was there some reason for this instead of 0 or -1?
	b->alevel.mark = 9999;
	b->alevel.space = 9999;

	b->len = 0;

	b->is_scrambled = is_scrambled;
	b->descram_state = descram_state;
	b->prev_descram = prev_descram;
}


/***********************************************************************************
 *
 * Name:	rrbb_append_bit	
 *
 * Purpose:	Append another bit to the end.
 *
 * Inputs:	Handle for sample array.
 *		Value for the sample.
 *
 ***********************************************************************************/

/* Definition in header file so it can be inlined. */


/***********************************************************************************
 *
 * Name:	rrbb_chop8	
 *
 * Purpose:	Remove 8 from the length.
 *
 * Inputs:	Handle for bit array.
 *		
 * Description:	Back up after appending the flag sequence.
 *
 ***********************************************************************************/

void rrbb_chop8 (rrbb_t b)
{
	
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	if (b->len >= 8) {
	  b->len -= 8;
	}
}

/***********************************************************************************
 *
 * Name:	rrbb_get_len	
 *
 * Purpose:	Get number of bits in the array.
 *
 * Inputs:	Handle for bit array.
 *		
 ***********************************************************************************/

int rrbb_get_len (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	return (b->len);
}



/***********************************************************************************
 *
 * Name:	rrbb_get_bit	
 *
 * Purpose:	Get value of bit in specified position.
 *
 * Inputs:	Handle for sample array.
 *		Index into array.
 *		
 ***********************************************************************************/

/* Definition in header file so it can be inlined. */




/***********************************************************************************
 *
 * Name:	rrbb_flip_bit	
 *
 * Purpose:	Complement the value of bit in specified position.
 *
 * Inputs:	Handle for bit array.
 *		Index into array.
 *		
 ***********************************************************************************/

//void rrbb_flip_bit (rrbb_t b, unsigned int ind)
//{
//	unsigned int di, mi;
//
//	assert (b != NULL);
//	assert (b->magic1 == MAGIC1);
//	assert (b->magic2 == MAGIC2);
//
//	assert (ind < b->len);
//
//	di = ind / SOI;
//	mi = ind % SOI;
//
//	b->data[di] ^= masks[mi];
//}

/***********************************************************************************
 *
 * Name:	rrbb_delete	
 *
 * Purpose:	Free the storage associated with the bit array.
 *
 * Inputs:	Handle for bit array.
 *		
 ***********************************************************************************/

void rrbb_delete (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	b->magic1 = 0;
	b->magic2 = 0;
	
	free (b);

	delete_count++;
}


/***********************************************************************************
 *
 * Name:	rrbb_set_netxp	
 *
 * Purpose:	Set the nextp field, used to maintain a queue.
 *
 * Inputs:	b	Handle for bit array.
 *		np	New value for nextp.
 *		
 ***********************************************************************************/

void rrbb_set_nextp (rrbb_t b, rrbb_t np)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	b->nextp = np;
}


/***********************************************************************************
 *
 * Name:	rrbb_get_netxp	
 *
 * Purpose:	Get value of nextp field.
 *
 * Inputs:	b	Handle for bit array.
 *		
 ***********************************************************************************/

rrbb_t rrbb_get_nextp (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	return (b->nextp);
}

/***********************************************************************************
 *
 * Name:	rrbb_get_chan	
 *
 * Purpose:	Get channel from which bit buffer was received.
 *
 * Inputs:	b	Handle for bit array.
 *		
 ***********************************************************************************/

int rrbb_get_chan (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	assert (b->chan >= 0 && b->chan < MAX_CHANS);

	return (b->chan);
}


/***********************************************************************************
 *
 * Name:	rrbb_get_subchan	
 *
 * Purpose:	Get subchannel from which bit buffer was received.
 *
 * Inputs:	b	Handle for bit array.
 *		
 ***********************************************************************************/

int rrbb_get_subchan (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	assert (b->subchan >= 0 && b->subchan < MAX_SUBCHANS);

	return (b->subchan);
}


/***********************************************************************************
 *
 * Name:	rrbb_get_slice	
 *
 * Purpose:	Get slice number from which bit buffer was received.
 *
 * Inputs:	b	Handle for bit array.
 *		
 ***********************************************************************************/

int rrbb_get_slice (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	assert (b->slice >= 0 && b->slice < MAX_SLICERS);

	return (b->slice);
}


/***********************************************************************************
 *
 * Name:	rrbb_set_audio_level	
 *
 * Purpose:	Set audio level at time the frame was received.
 *
 * Inputs:	b	Handle for bit array.
 *		alevel	Audio level.
 *		
 ***********************************************************************************/

void rrbb_set_audio_level (rrbb_t b, alevel_t alevel)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	b->alevel = alevel;
}


/***********************************************************************************
 *
 * Name:	rrbb_get_audio_level	
 *
 * Purpose:	Get audio level at time the frame was received.
 *
 * Inputs:	b	Handle for bit array.
 *		
 ***********************************************************************************/

alevel_t rrbb_get_audio_level (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	return (b->alevel);
}



/***********************************************************************************
 *
 * Name:	rrbb_set_speed_error
 *
 * Purpose:	Set speed error of the received frame.
 *
 * Inputs:	b		Handle for bit array.
 *		speed_error	In percentage.
 *
 ***********************************************************************************/

void rrbb_set_speed_error (rrbb_t b, float speed_error)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	b->speed_error = speed_error;
}


/***********************************************************************************
 *
 * Name:	rrbb_get_speed_error
 *
 * Purpose:	Get speed error of the received frame.
 *
 * Inputs:	b	Handle for bit array.
 *
 * Returns:	speed error in percentage.
 *
 ***********************************************************************************/

float rrbb_get_speed_error (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	return (b->speed_error);
}



/***********************************************************************************
 *
 * Name:	rrbb_get_is_scrambled	
 *
 * Purpose:	Find out if using scrambled data.
 *
 * Inputs:	b	Handle for bit array.
 *
 * Returns:	True (for 9600 baud) or false (for slower AFSK).
 *		
 ***********************************************************************************/

int rrbb_get_is_scrambled (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	return (b->is_scrambled);
}



/***********************************************************************************
 *
 * Name:	rrbb_get_descram_state	
 *
 * Purpose:	Get data descrambler state before first data bit of frame.
 *
 * Inputs:	b	Handle for bit array.
 *		
 ***********************************************************************************/

int rrbb_get_descram_state (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	return (b->descram_state);
}


/***********************************************************************************
 *
 * Name:	rrbb_get_prev_descram	
 *
 * Purpose:	Get previous descrambled bit before first data bit of frame.
 *
 * Inputs:	b	Handle for bit array.
 *		
 ***********************************************************************************/

int rrbb_get_prev_descram (rrbb_t b)
{
	assert (b != NULL);
	assert (b->magic1 == MAGIC1);
	assert (b->magic2 == MAGIC2);

	return (b->prev_descram);
}



/* end rrbb.c */


