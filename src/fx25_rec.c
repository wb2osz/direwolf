//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2019  John Langner, WB2OSZ
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
 * File:        fx25_rec.c
 *
 * Purpose:     Extract FX.25 codeblocks from a stream of bits and process them.
 *
 *******************************************************************************/

#include "direwolf.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
//#if __WIN32__
//#include <fcntl.h>
//#endif

#include "fx25.h"

#include "fcs_calc.h"
#include "textcolor.h"
#include "multi_modem.h"
#include "demod.h"

struct fx_context_s {

	enum { FX_TAG=0, FX_DATA, FX_CHECK } state;
	uint64_t accum;		// Accumulate bits for matching to correlation tag.
	int ctag_num;		// Correlation tag number, CTAG_MIN to CTAG_MAX if approx. match found.
	int k_data_radio;	// Expected size of "data" sent over radio.
	int coffs;		// Starting offset of the check part.
	int nroots;		// Expected number of check bytes. 
	int dlen;		// Accumulated length in "data" below.
	int clen;		// Accumulated length in "check" below.
	unsigned char imask;	// Mask for storing a bit.
	unsigned char block[FX25_BLOCK_SIZE+1];
};

static struct fx_context_s *fx_context[MAX_RADIO_CHANS][MAX_SUBCHANS][MAX_SLICERS];

static void process_rs_block (int chan, int subchan, int slice, struct fx_context_s *F);

static int my_unstuff (int chan, int subchan, int slice, unsigned char * restrict pin, int ilen, unsigned char * restrict frame_buf);

//#define FXTEST 1	// Define for standalone test application.
			// It expects to find files fx01.dat, fx02.dat, ..., fx0b.dat/

#if FXTEST
static int fx25_test_count = 0;

int main ()
{
	fx25_init(3);

	for (int i = CTAG_MIN; i <= CTAG_MAX; i++) {
	
	  char fname[32];
	  snprintf (fname, sizeof(fname), "fx%02x.dat", i);
	  FILE *fp = fopen(fname, "rb");
	  if (fp == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\n");
	    dw_printf ("****** Could not open %s ******\n", fname);
	    dw_printf ("****** Did you generate the test files first? ******\n");
	    exit (EXIT_FAILURE);
	  }

//#if 0  // reminder for future if reading from stdin.
//#if __WIN32__
//	// So 0x1a byte does not signal EOF.
//	_setmode(_fileno(stdin), _O_BINARY);
//#endif
//	fp = stdin;
//#endif
	  unsigned char ch;
	  while (fread(&ch, 1, 1, fp) == 1) {
	    for (unsigned char imask = 0x01; imask != 0; imask <<=1) {
	      fx25_rec_bit (0, 0, 0, ch & imask);
	    }
	  }
	  fclose (fp);
	}

	if (fx25_test_count == 11) {
	  text_color_set(DW_COLOR_REC);
	  dw_printf ("\n");
	  dw_printf ("\n");
	  dw_printf ("\n");
	  dw_printf ("***** FX25 unit test Success - all tests passed. *****\n");
	  exit (EXIT_SUCCESS);
	}
	text_color_set(DW_COLOR_ERROR);
	dw_printf ("\n");
	dw_printf ("\n");
	dw_printf ("***** FX25 unit test FAILED.  Only %d/11 tests passed. *****\n", fx25_test_count);
	exit (EXIT_SUCCESS);

} // end main 

#endif  // FXTEST



/***********************************************************************************
 *
 * Name:        fx25_rec_bit
 *
 * Purpose:     Extract FX.25 codeblocks from a stream of bits.
 *		In a completely integrated AX.25 / FX.25 receive system,
 *		this would see the same bit stream as hdlc_rec_bit.
 *
 * Inputs:      chan    - Channel number.
 *
 *              subchan - This allows multiple demodulators per channel.
 *
 *              slice   - Allows multiple slicers per demodulator (subchannel).
 *
 *              dbit	- Data bit after NRZI and any descrambling.
 *			  Any non-zero value is logic '1'.
 *
 * Description: This is called once for each received bit.
 *              For each valid frame, process_rec_frame() is called for further processing.
 *		It can gather multiple candidates from different parallel demodulators
 *		("subchannels") and slicers, then decide which one is the best.
 *
 ***********************************************************************************/

#define FENCE 0x55		// to detect buffer overflow.

void fx25_rec_bit (int chan, int subchan, int slice, int dbit)
{

// Allocate context blocks only as needed.

	struct fx_context_s *F = fx_context[chan][subchan][slice];
	if (F == NULL) {
          assert (chan >= 0 && chan < MAX_RADIO_CHANS);
          assert (subchan >= 0 && subchan < MAX_SUBCHANS);
          assert (slice >= 0 && slice < MAX_SLICERS);
	  F = fx_context[chan][subchan][slice] = (struct fx_context_s *)malloc(sizeof (struct fx_context_s));
	  assert (F != NULL);
	  memset (F, 0, sizeof(struct fx_context_s));
	}

// State machine to identify correlation tag then gather appropriate number of data and check bytes.
	  
	switch (F->state) {
	  case FX_TAG:
	    F->accum >>= 1;
	    if (dbit) F->accum |= 1LL << 63;
	    int c = fx25_tag_find_match (F->accum);
	    if (c >= CTAG_MIN && c <= CTAG_MAX) {
	      
	      F->ctag_num = c;
	      F->k_data_radio = fx25_get_k_data_radio (F->ctag_num);
	      F->nroots = fx25_get_nroots (F->ctag_num);
	      F->coffs = fx25_get_k_data_rs (F->ctag_num);
	      assert (F->coffs == FX25_BLOCK_SIZE - F->nroots);

	      if (fx25_get_debug() >= 2) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf ("FX.25[%d.%d]: Matched correlation tag 0x%02x with %d bit errors.  Expecting %d data & %d check bytes.\n",
			chan, slice,	// ideally subchan too only if applicable
			c,
			__builtin_popcountll(F->accum ^ fx25_get_ctag_value(c)),
			F->k_data_radio, F->nroots);
	      }

	      F->imask = 0x01;
	      F->dlen = 0;
	      F->clen = 0;
	      memset (F->block, 0, sizeof(F->block));
	      F->block[FX25_BLOCK_SIZE] = FENCE;
	      F->state = FX_DATA;
	    }
	    break;

	  case FX_DATA:
	    if (dbit) F->block[F->dlen] |= F->imask;
	    F->imask <<= 1;
	    if (F->imask == 0) {
	      F->imask = 0x01;
	      F->dlen++;
	      if (F->dlen >= F->k_data_radio) {
	         F->state = FX_CHECK;
	      }
	    }
	    break;

	  case FX_CHECK:
	    if (dbit) F->block[F->coffs + F->clen] |= F->imask;
	    F->imask <<= 1;
	    if (F->imask == 0) {
	      F->imask = 0x01;
	      F->clen++;
	      if (F->clen >= F->nroots) {

	        process_rs_block (chan, subchan, slice, F);		// see below

	        F->ctag_num = -1;
	        F->accum = 0;
	        F->state = FX_TAG;
	      }
	    }
	    break;
	}
}



/***********************************************************************************
 *
 * Name:        fx25_rec_busy
 *
 * Purpose:     Is FX.25 reception currently in progress?
 *
 * Inputs:      chan    - Channel number.
 *
 * Returns:	True if currently in progress for the specified channel.
 *
 * Description: This is required for duplicate removal.  One channel and can have
 *		multiple demodulators (called subchannels) running in parallel.
 *		Each of them can have multiple slicers.  Duplicates need to be
 *		removed.  Normally a delay of a couple bits (or more accurately
 *		symbols) was fine because they all took about the same amount of time.
 *		Now, we can have an additional delay of up to 64 check bytes and
 *		some filler in the data portion.  We can't simply wait that long.
 *		With normal AX.25 a couple frames can come and go during that time.	
 *		We want to delay the duplicate removal while FX.25 block reception
 *		is going on.
 *
 ***********************************************************************************/

int fx25_rec_busy (int chan)
{
	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

	// This could be a little faster if we knew number of
	// subchannels and slicers but it is probably insignificant.

	for (int i = 0; i < MAX_SUBCHANS; i++) {
	  for (int j = 0; j < MAX_SLICERS; j++) {
	    if (fx_context[chan][i][j] != NULL) {
	      if (fx_context[chan][i][j]->state != FX_TAG) {
	        return (1);
	      }
	    }
	  }
	}
	return (0);

} // end fx25_rec_busy



/***********************************************************************************
 *
 * Name:	process_rs_block
 *
 * Purpose:     After the correlation tag was detected and the appropriate number
 *		of data and check bytes are accumulated, this performs the processing
 *
 * Inputs:	chan, subchan, slice
 *
 *		F->ctag_num	- Correlation tag number  (index into table)
 *
 *		F->dlen		- Number of "data" bytes.
 *
 *		F->clen		- Number of "check" bytes"
 *
 *		F->block	- Codeblock.  Always 255 total bytes.
 *				  Anything left over after data and check
 *				  bytes is filled with zeros.
 *
 *		<- - - - - - - - - - - 255 bytes total - - - - - - - - ->
 *		+-----------------------+---------------+---------------+
 *		|  dlen bytes "data"    |  zero fill    |  check bytes  |
 *		+-----------------------+---------------+---------------+
 *
 * Description:	Use Reed-Solomon decoder to fix up any errors.
 *		Extract the AX.25 frame from the corrected data.
 *
 ***********************************************************************************/

static void process_rs_block (int chan, int subchan, int slice, struct fx_context_s *F)
{
	if (fx25_get_debug() >= 3) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("FX.25[%d.%d]: Received RS codeblock.\n", chan, slice);
	  fx_hex_dump (F->block, FX25_BLOCK_SIZE);
	}
	assert (F->block[FX25_BLOCK_SIZE] == FENCE);

	int derrlocs[FX25_MAX_CHECK];	// Half would probably be OK.
	struct rs *rs = fx25_get_rs(F->ctag_num);

	int derrors = DECODE_RS(rs, F->block, derrlocs, 0);

	if (derrors >= 0) {		// -1 for failure.  >= 0 for success, number of bytes corrected.

	  if (fx25_get_debug() >= 2) {
	    text_color_set(DW_COLOR_INFO);
	    if (derrors == 0) {
	      dw_printf ("FX.25[%d.%d]: FEC complete with no errors.\n", chan, slice);
	    }
	    else {
	      dw_printf ("FX.25[%d.%d]: FEC complete, fixed %2d errors in byte positions:", chan, slice, derrors);
	      for (int k = 0; k < derrors; k++) {
	        dw_printf (" %d", derrlocs[k]);
	      }
	      dw_printf ("\n");
	    }
	  }

	  unsigned char frame_buf[FX25_MAX_DATA+1];	// Out must be shorter than input.
	  int frame_len = my_unstuff (chan, subchan, slice, F->block, F->dlen, frame_buf);

	  if (frame_len >= 14 + 1 + 2) {		// Minimum length: Two addresses & control & FCS.

	    unsigned short actual_fcs = frame_buf[frame_len-2] | (frame_buf[frame_len-1] << 8);
	    unsigned short expected_fcs = fcs_calc (frame_buf, frame_len - 2);
	    if (actual_fcs == expected_fcs) {

	      if (fx25_get_debug() >= 3) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("FX.25[%d.%d]: Extracted AX.25 frame:\n", chan, slice);
	        fx_hex_dump (frame_buf, frame_len);
	      }

#if FXTEST 
	      fx25_test_count++;
#else
	      alevel_t alevel = demod_get_audio_level (chan, subchan);

	      multi_modem_process_rec_frame (chan, subchan, slice, frame_buf, frame_len - 2, alevel, derrors, 1);   /* len-2 to remove FCS. */

#endif
	    } else {
	      // Most likely cause is defective sender software.
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("FX.25[%d.%d]: Bad FCS for AX.25 frame.\n", chan, slice);
	      fx_hex_dump (F->block, F->dlen);
	      fx_hex_dump (frame_buf, frame_len);
	    }
	  }
	  else {
	    // Most likely cause is defective sender software.
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("FX.25[%d.%d]: AX.25 frame is shorter than minimum length.\n", chan, slice);
	    fx_hex_dump (F->block, F->dlen);
	    fx_hex_dump (frame_buf, frame_len);
	  }
	}
	else if (fx25_get_debug() >= 2) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FX.25[%d.%d]: FEC failed.  Too many errors.\n", chan, slice);
	}

} // process_rs_block


/***********************************************************************************
 *
 * Name:	my_unstuff  
 *
 * Purpose:	Remove HDLC it stuffing and surrounding flag delimiters.
 *
 * Inputs:      chan, subchan, slice	- For error messages.
 *
 *		pin	- "data" part of RS codeblock.
 *			  First byte must be HDLC "flag".
 *			  May be followed by additional flags.
 *			  There must be terminating flag but it might not be byte aligned.
 *
 *		ilen	- Number of bytes in pin.
 *
 * Outputs:	frame_buf - Frame contents including FCS.
 *			    Bit stuffing is gone so it should be a whole number of bytes.
 *
 * Returns:	Number of bytes in frame_buf, including 2 for FCS.
 *		This can never be larger than the max "data" size.
 *		0 if any error.
 *
 * Errors:	First byte is not not flag.
 *		Found seven '1' bits in a row.
 *		Result is not whole number of bytes after removing bit stuffing.
 *		Trailing flag not found.
 *		Most likely cause, for all of these, is defective sender software.
 *
 ***********************************************************************************/

static int my_unstuff (int chan, int subchan, int slice, unsigned char * restrict pin, int ilen, unsigned char * restrict frame_buf)
{
	unsigned char pat_det = 0;	// Pattern detector.
	unsigned char oacc = 0;		// Accumulator for a byte out.
	int olen = 0;			// Number of good bits in oacc.
	int frame_len = 0;		// Number of bytes accumulated, including CRC.
	
	if (*pin != 0x7e) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FX.25[%d.%d] error: Data section did not start with 0x7e.\n", chan, slice);
	  fx_hex_dump (pin, ilen);
	  return (0);
	}
	while (ilen > 0 && *pin == 0x7e) {
	  ilen--;
	  pin++;	// Skip over leading flag byte(s).
	}
 
	for (int i=0; i<ilen; pin++, i++) {
	  for (unsigned char imask = 0x01; imask != 0; imask <<= 1) {
	    unsigned char dbit = (*pin & imask) != 0;

	    pat_det >>= 1;	// Shift the most recent eight bits thru the pattern detector.
	    pat_det |= dbit << 7; 

	    if (pat_det == 0xfe) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("FX.25[%d.%d]: Invalid AX.25 frame - Seven '1' bits in a row.\n", chan, slice);
	      fx_hex_dump (pin, ilen);
	      return 0;
	    }

	    if (dbit) {
	      oacc >>= 1;
	      oacc |= 0x80;
	    } else {
	      if (pat_det == 0x7e) {	// "flag" pattern - End of frame.
	        if (olen == 7) {
	          return (frame_len);	// Whole number of bytes in result including CRC
		}
	        else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("FX.25[%d.%d]: Invalid AX.25 frame - Not a whole number of bytes.\n", chan, slice);
	          fx_hex_dump (pin, ilen);
	          return (0);
	        }
	      } else if ( (pat_det >> 2) == 0x1f ) {
	        continue;	// Five '1' bits in a row, followed by '0'.  Discard the '0'.
	      }
	      oacc >>= 1;
	    }

	    olen++;
	    if (olen & 8) {
	      olen = 0;
              frame_buf[frame_len++] = oacc;
	    }
	  }
	}	/* end of loop on all bits in block */

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("FX.25[%d.%d]: Invalid AX.25 frame - Terminating flag not found.\n", chan, slice);
	fx_hex_dump (pin, ilen);

	return (0);	// Should never fall off the end.

}  // my_unstuff

// end fx25_rec.c