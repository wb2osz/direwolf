//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014  John Langner, WB2OSZ
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
 * File:	hdlc_rec2.c
 *
 * Purpose:	Extract HDLC frame from a block of bits after someone
 *		else has done the work of pulling it out from between
 *		the special "flag" sequences.
 *
 *
 * New in version 1.1:
 *
 *		Several enhancements provided by Fabrice FAURE:
 *
 *		- Additional types of attempts to fix a bad CRC.
 *		- Optimized code to reduce execution time.
 *		- Improved detection of duplicate packets from different fixup attempts.
 *		- Set limit on number of packets in fix up later queue.
 *
 *		One of the new recovery attempt cases recovers three additional 
 *		packets that were lost before.  The one thing I disagree with is
 *		use of the word "swap" because that sounds like two things 
 *		are being exchanged for each other.  I would prefer "flip"
 *		or "invert" to describe changing a bit to the opposite state.
 *		I took "swap" out of the user-visible messages but left the 
 *		rest of the source code as provided.
 *
 *******************************************************************************/

#include <stdio.h>
#include <assert.h>
#include <ctype.h>

//Optimize processing by accessing directly to decoded bits
#define RRBB_C 1
#include "direwolf.h"
#include "hdlc_rec2.h"
#include "fcs_calc.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "rrbb.h"
#include "rdq.h"
#include "multi_modem.h"
//#define DEBUG 1
//#define DEBUGx 1
//#define DEBUG_LATER 1


/* 
 * Minimum & maximum sizes of an AX.25 frame including the 2 octet FCS. 
 */

#define MIN_FRAME_LEN ((AX25_MIN_PACKET_LEN) + 2)
				
#define MAX_FRAME_LEN ((AX25_MAX_PACKET_LEN) + 2)	

/*
 * This is the current state of the HDLC decoder.
 *
 * It is possible to run multiple decoders concurrently by
 * having a separate set of state variables for each.
 *
 * Should have a reset function instead of initializations here.
 */

struct hdlc_state_s {

	int prev_raw;			/* Keep track of previous bit so */
					/* we can look for transitions. */
					/* Should be only 0 or 1. */

	unsigned char pat_det; 		/* 8 bit pattern detector shift register. */
					/* See below for more details. */

	unsigned char oacc;		/* Accumulator for building up an octet. */

	int olen;			/* Number of bits in oacc. */
					/* When this reaches 8, oacc is copied */
					/* to the frame buffer and olen is zeroed. */

	unsigned char frame_buf[MAX_FRAME_LEN];
					/* One frame is kept here. */

	int frame_len;			/* Number of octets in frame_buf. */
					/* Should be in range of 0 .. MAX_FRAME_LEN. */

};

#define MAX_RETRY_SWAP_BITS 24		/* Maximum number of contiguous bits to swap */
#define MAX_RETRY_REMOVE_SEPARATED_BITS 24	/* Maximum number of contiguous bits to remove */

static int try_decode (rrbb_t block, int chan, int subchan, int alevel, retry_conf_t retry_conf);
static int try_to_fix_quick_now (rrbb_t block, int chan, int subchan, int alevel, retry_t fix_bits);
static int sanity_check (unsigned char *buf, int blen, retry_t bits_flipped);
#if DEBUG_LATER
static double dtime_now (void);
#endif

/***********************************************************************************
 *
 * Name:	hdlc_rec2_block
 *
 * Purpose:	Extract HDLC frame from a stream of bits.
 *
 * Inputs:	block 		- Handle for bit array.
 *		fix_bits	- Level of effort to recover frames with bad FCS.
 *
 * Description:	The other (original) hdlc decoder took one bit at a time
 *		right out of the demodulator.
 *
 *		This is different in that it processes a block of bits
 *		previously extracted from between two "flag" patterns.
 *
 *		This allows us to try decoding the same received data more
 *		than once.
 *
 * Bugs:	This does not work for 9600 baud, more accurately when
 *		the transmitted bits are scrambled.
 *
 *		Currently we unscramble the bits as they come from the 
 *		receiver.  Instead we need to save the original received
 *		bits and apply the descrambling after flipping the bits.
 *
 ***********************************************************************************/


void hdlc_rec2_block (rrbb_t block, retry_t fix_bits)
{
	int chan = rrbb_get_chan(block);
	int subchan = rrbb_get_subchan(block);
	int alevel = rrbb_get_audio_level(block);
	int ok;
	int n;

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("\n--- try to decode ---\n");
#endif

#if SLICENDICE
/*
 * Unfinished experiment.  Get back to this again someday.
 * The demodulator output is (should be) roughly in the range of -1 to 1.
 * Formerly we sliced it at 0 and saved only a single bit for the sample.
 * Now we save the sample so we can try adjusting the slicing point.
 *
 * First time thru, set the slicing point to 0.
 */

	for (n = 0; n < 1 ; n++) {

	  rrbb_set_slice_val (block, n);

	  ok = try_decode (block, chan, subchan, alevel, RETRY_NONE, -1, -1, -1);

	  if (ok) {
//#if DEBUG
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Got it with no errors. Slice val = %d \n", n);
//#endif
	    rrbb_delete (block);
	    return;
	  }
	}
	rrbb_set_slice_val (block, 0);

#else /* not SLICENDICE */
	/* Create an empty retry configuration */
	retry_conf_t retry_cfg;

	/* By default we don't try to alter any bits */
	retry_cfg.type = RETRY_TYPE_NONE;
	retry_cfg.mode = RETRY_MODE_CONTIGUOUS;
	retry_cfg.retry = RETRY_NONE;
	retry_cfg.u_bits.contig.nr_bits = 0;
	retry_cfg.u_bits.contig.bit_idx = 0;
	/* Prepare the decoded bits in an array for faster processing 
	 *(at cost of memory but 1 or 2 kbytes is nothing compared to processing time ) */
	rrbb_compute_bits(block);
	ok = try_decode (block, chan, subchan, alevel, retry_cfg);

	if (ok) {
#if DEBUG
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Got it the first time.\n");
#endif
	 rrbb_delete (block);
	 return;
	}
#endif
	
	if (try_to_fix_quick_now (block, chan, subchan, alevel, fix_bits)) {
	  rrbb_delete (block);
	  return;
	}

/*
 * Put in queue for retrying later at lower priority.
 */

	if (fix_bits < RETRY_SWAP_TWO_SEP) {
	  rrbb_delete (block); 
	  return;
	}

	rdq_append (block);

}


static int try_to_fix_quick_now (rrbb_t block, int chan, int subchan, int alevel, retry_t fix_bits)
{
	int ok;
	int len, i,j;


	len = rrbb_get_len(block);
	/* Prepare the retry configuration */
        retry_conf_t retry_cfg;
	/* Will modify only contiguous bits*/
	retry_cfg.mode = RETRY_MODE_CONTIGUOUS; 
/* 
 * Try fixing one bit.   
 */
	if (fix_bits < RETRY_SWAP_SINGLE) {
	  return 0;
	}
	/* Try to swap one bit */
	retry_cfg.type = RETRY_TYPE_SWAP;
	retry_cfg.retry = RETRY_SWAP_SINGLE;
	retry_cfg.u_bits.contig.nr_bits = 1;

	for (i=0; i<len; i++) {
	  /* Set the index of the bit to swap */
	  retry_cfg.u_bits.contig.bit_idx = i;
	  ok = try_decode (block, chan, subchan, alevel, retry_cfg);
	  if (ok) {
#if DEBUG
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("*** Success by flipping SINGLE bit %d of %d ***\n", i, len);
#endif
	    return 1;
	  }
	}

/* 
 * Try fixing two adjacent bits.  
 */
	if (fix_bits < RETRY_SWAP_DOUBLE) {
	  return 0;
	}
	/* Try to swap two contiguous bits */
	retry_cfg.retry = RETRY_SWAP_DOUBLE;
	retry_cfg.u_bits.contig.nr_bits = 2;


	for (i=0; i<len-1; i++) {
	  retry_cfg.u_bits.contig.bit_idx = i;
	  ok = try_decode (block, chan, subchan, alevel, retry_cfg);
	  if (ok) {
#if DEBUG
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("*** Success by flipping DOUBLE bit %d of %d ***\n", i, len);
#endif
	    return 1;
	  }
	}

/*
 * Try fixing adjacent three bits.
 */
	if (fix_bits < RETRY_SWAP_TRIPLE) {
	  return 0;
	}
	/* Try to swap three contiguous bits */
	retry_cfg.retry = RETRY_SWAP_TRIPLE;
	retry_cfg.u_bits.contig.nr_bits = 3;

	for (i=0; i<len-2; i++) {
	  retry_cfg.u_bits.contig.bit_idx = i;
	  ok = try_decode (block, chan, subchan, alevel, retry_cfg);
	  if (ok) {
#if DEBUG
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("*** Success by flipping TRIPLE bit %d of %d ***\n", i, len);
#endif
	    return 1;
	  }
	}

	if (fix_bits < RETRY_REMOVE_SINGLE) {
	  return 0;
	}

/* 
 * Try removing one bit.   
 */
		retry_cfg.type = RETRY_TYPE_REMOVE;
		retry_cfg.retry = RETRY_REMOVE_SINGLE;
		retry_cfg.u_bits.contig.nr_bits = 1;

		for (i=0; i<len; i++) {
		  retry_cfg.u_bits.contig.bit_idx = i;
		  ok = try_decode (block, chan, subchan, alevel, retry_cfg);
		  if (ok) {
#if DEBUG
		    text_color_set(DW_COLOR_ERROR);
		    dw_printf ("*** Success by removing SINGLE bit %d of %d ***\n", i, len);
#endif
		    return 1;
		  }
		}
	if (fix_bits < RETRY_REMOVE_DOUBLE) {
	  return 0;
	}


/* 
 * Try removing two contiguous bits.   
 */
#if DEBUG
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("*** Try removing DOUBLE bits *** for %d bits\n", len);
#endif
		retry_cfg.retry = RETRY_REMOVE_DOUBLE;
		retry_cfg.u_bits.contig.nr_bits = 2;

		for (i=0; i<len-1; i++) {
	  	  retry_cfg.u_bits.contig.bit_idx = i;
		  ok = try_decode (block, chan, subchan, alevel, retry_cfg);
		  if (ok) {
#if DEBUG
		    text_color_set(DW_COLOR_ERROR);
		    dw_printf ("*** Success by removing DOUBLE bits %d of %d ***\n", i, len);
#endif
		    return 1;
		  }
		}
	if (fix_bits < RETRY_REMOVE_TRIPLE) {
	  return 0;
	}

/* 
 * Try removing three contiguous bits.
 */
#if DEBUG
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("*** Try removing TRIPLE bits *** for %d bits\n", len);
#endif
		retry_cfg.retry = RETRY_REMOVE_TRIPLE;
		retry_cfg.u_bits.contig.nr_bits = 3;

		for (i=0; i<len-2; i++) {
	  	  retry_cfg.u_bits.contig.bit_idx = i;
		  ok = try_decode (block, chan, subchan, alevel, retry_cfg);
		  if (ok) {
#if DEBUG
		    text_color_set(DW_COLOR_ERROR);
		    dw_printf ("*** Success by removing TRIPLE bits %d of %d ***\n", i, len);
#endif
		    return 1;
		  }
		}
	if (fix_bits < RETRY_INSERT_SINGLE) {
	  return 0;
	}

/* 
 * Try inserting one bit (two values possibles for this inserted bit).   
 */
#if DEBUG
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("*** Try inserting SINGLE bit *** for %d bits\n", len);
#endif
		retry_cfg.type = RETRY_TYPE_INSERT;
		retry_cfg.retry = RETRY_INSERT_SINGLE;
		retry_cfg.u_bits.contig.nr_bits = 1;


		for (i=0; i<len; i++) {
	  	  retry_cfg.u_bits.contig.bit_idx = i;
		  retry_cfg.insert_value=0;
		  ok = try_decode (block, chan, subchan, alevel, retry_cfg);
		  if (!ok) {
		    retry_cfg.insert_value=1;
		    ok = try_decode (block, chan, subchan, alevel, retry_cfg);
		  }
		  if (ok) {
#if DEBUG
		    text_color_set(DW_COLOR_ERROR);
		    dw_printf ("*** Success by inserting SINGLE bit %d of %d ***\n", i, len);
#endif
		    return 1;
		  }
		}
	if (fix_bits < RETRY_INSERT_DOUBLE) {
	  return 0;
	}

/* 
 * Try inserting two contiguous bits (4 possible values for two bits).   
 */
#if DEBUG
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("*** Try inserting DOUBLE bits *** for %d bits\n", len);
#endif
		retry_cfg.retry = RETRY_INSERT_DOUBLE;
		retry_cfg.u_bits.contig.nr_bits = 2;


		for (i=0; i<len-1; i++) {
	  	  retry_cfg.u_bits.contig.bit_idx = i;
		  for (j=0;j<4;j++) {
		    retry_cfg.insert_value=j;
		    ok = try_decode (block, chan, subchan, alevel, retry_cfg);

		    if (ok) {
#if DEBUG
		      text_color_set(DW_COLOR_ERROR);
		      dw_printf ("*** Success by inserting DOUBLE bits %d of %d ***\n", i, len);
#endif
		      return 1;
		    }
		  }
		}
	return 0;
}

void hdlc_rec2_try_to_fix_later (rrbb_t block, int chan, int subchan, int alevel)
{
	int ok;
	int len, i, j;
	retry_t fix_bits;
#if DEBUG_LATER
	double tstart, tend;
#endif
	retry_conf_t retry_cfg;
	len = rrbb_get_len(block);
	fix_bits = rrbb_get_fix_bits (block);

	if (fix_bits < RETRY_SWAP_TWO_SEP) {
	  return ;
	}


	retry_cfg.mode = RETRY_MODE_SEPARATED; 
/*
 * Two  non-adjacent ("separated") single bits.
 * It chews up a lot of CPU time.  Test takes 4 times longer to run.
 *
 * Ran up to xx seconds (TODO check again with optimized code) seconds for 1040 bits before giving up .
 * Processing time is order N squared so time goes up rapidly with larger frames.
 */
	retry_cfg.type = RETRY_TYPE_SWAP;
	retry_cfg.retry = RETRY_SWAP_TWO_SEP;
	retry_cfg.u_bits.sep.bit_idx_c = -1;

#ifdef DEBUG_LATER
	tstart = dtime_now();
	dw_printf ("*** Try flipping TWO SEPARATED BITS %d bits\n", len);
#endif
	len = rrbb_get_len(block);
	for (i=0; i<len-2; i++) {
	  retry_cfg.u_bits.sep.bit_idx_a = i;
	  int j;

	  ok = 0;
	  for (j=i+2; j<len; j++) {
	    retry_cfg.u_bits.sep.bit_idx_b = j;
	    ok = try_decode (block, chan, subchan, alevel, retry_cfg);
	    if (ok) {
	      break;
	    }

	  }	  
	  if (ok) {
#if DEBUG
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("*** Success by flipping TWO SEPARATED bits %d and %d of %d \n", i, j, len);
#endif
	    return;
	  }
	}
#if DEBUG_LATER
	tend = dtime_now();
	text_color_set(DW_COLOR_ERROR);
	dw_printf ("*** No luck flipping TWO SEPARATED bits of %d *** %.3f sec.\n", len, tend-tstart);
#endif

	if (fix_bits < RETRY_SWAP_MANY) {
	  return ;
	}
	/* Try to swap many contiguous bits */
	retry_cfg.mode = RETRY_MODE_CONTIGUOUS; 
	retry_cfg.type = RETRY_TYPE_SWAP;
	retry_cfg.retry = RETRY_SWAP_MANY;

#ifdef DEBUG_LATER
	tstart = dtime_now();
	dw_printf ("*** Try swapping many BITS %d bits\n", len);
#endif
	len = rrbb_get_len(block);
	for (i=0; i<len; i++) {
	  for (j=1; j<len-i && j < MAX_RETRY_SWAP_BITS;j++) {
	    retry_cfg.u_bits.contig.bit_idx = i;
	    retry_cfg.u_bits.contig.nr_bits = j;
//	    dw_printf ("*** Trying swapping %d bits starting at %d of %d ***\n", j,i, len);
	    ok = try_decode (block, chan, subchan, alevel, retry_cfg);
	    if (ok) {
#if DEBUG
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("*** Success by swapping %d bits starting at %d of %d ***\n", j,i, len);
#endif
	      return ;
	    }
	  }
	}
#if DEBUG_LATER
	tend = dtime_now();
	text_color_set(DW_COLOR_ERROR);
	dw_printf ("*** No luck swapping many bits for len %d  in %.3f sec.\n",len, tend-tstart);
#endif

	if (fix_bits < RETRY_REMOVE_MANY) {
	  return ;
	}


	/* Try to remove many contiguous bits */
	retry_cfg.type = RETRY_TYPE_REMOVE;
	retry_cfg.retry = RETRY_REMOVE_MANY;
#ifdef DEBUG_LATER
	tstart = dtime_now();
	dw_printf ("*** Trying removing many bits for len\n", len);
#endif


	len = rrbb_get_len(block);
	for (i=0; i<2; i++) {
	  for (j=1; j<len-i && j<len/2;j++) {
	    retry_cfg.u_bits.contig.bit_idx = i;
	    retry_cfg.u_bits.contig.nr_bits = j;
#ifdef DEBUG
	    dw_printf ("*** Trying removing %d bits starting at %d of %d ***\n", j,i, len);
#endif
	    ok = try_decode (block, chan, subchan, alevel, retry_cfg);
	    if (ok) {
#if DEBUG
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("*** Success by removing %d bits starting at %d of %d ***\n", j,i, len);
#endif
	      return ;
	    }
	  }
	}
#if DEBUG_LATER
	tend = dtime_now();
	text_color_set(DW_COLOR_ERROR);
	dw_printf ("*** No luck removing many bits for len %d *** in %.3f sec.\n", len, tend-tstart);
#endif

	if (fix_bits < RETRY_REMOVE_TWO_SEP) {
	  return ;
	}

/*
 * Try to remove Two  non-adjacent ("separated") single bits.
 */
	retry_cfg.mode = RETRY_MODE_SEPARATED; 
	retry_cfg.type = RETRY_TYPE_REMOVE;
	retry_cfg.retry = RETRY_REMOVE_TWO_SEP;
	retry_cfg.u_bits.sep.bit_idx_c = -1;

#if DEBUG_LATER 
	tstart = dtime_now();
	dw_printf ("*** Try removing TWO SEPARATED BITS %d bits\n", len);
#endif
	len = rrbb_get_len(block);
	for (i=0; i<len-2; i++) {
	  retry_cfg.u_bits.sep.bit_idx_a = i;
	  int j;
	  ok = 0;
	  for (j=i+2; j<len && j - i < MAX_RETRY_REMOVE_SEPARATED_BITS; j++) {
	    retry_cfg.u_bits.sep.bit_idx_b = j;
	    ok = try_decode (block, chan, subchan, alevel, retry_cfg);
	    if (ok) {
	      break;
	    }

	  }	  
	  if (ok) {
#if DEBUG_LATER
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("*** Success by removing TWO SEPARATED bits %d and %d of %d \n", i, j, len);
#endif
	    return;
	  }
	}
#if DEBUG_LATER
	tend = dtime_now();
	text_color_set(DW_COLOR_ERROR);
	dw_printf ("*** No luck removing TWO SEPARATED bits of %d *** %.3f sec.\n", len, tend-tstart);
#endif
	return;
}
/* Check if the specified index of bit has been modified with the current type of configuration
 * Provide a specific implementation for contiguous mode to optimize number of tests done in the loop */
inline static char is_contig_bit_modified(int bit_idx, retry_conf_t retry_conf) {
	  int cont_bit_idx = retry_conf.u_bits.contig.bit_idx;
	  int cont_nr_bits = retry_conf.u_bits.contig.nr_bits;

	  if (bit_idx >= cont_bit_idx && (bit_idx < cont_bit_idx + cont_nr_bits )) 
		return 1;
	  else 
		return 0;
}
/* Check  if the specified index of bit has been modified with the current type of configuration in separated bit index mode
 * Provide a specific implementation for separated mode to optimize number of tests done in the loop */
inline static char is_sep_bit_modified(int bit_idx, retry_conf_t retry_conf) {
	  if (bit_idx == retry_conf.u_bits.sep.bit_idx_a || 
	      bit_idx == retry_conf.u_bits.sep.bit_idx_b ||
	      bit_idx == retry_conf.u_bits.sep.bit_idx_c)
	    return 1;
	  else
	    return 0;
}

/* Get the bit value from a precalculated array to optimize access time in the loop */
inline static unsigned int get_bit (const rrbb_t b,const unsigned int ind)
{
	return b->computed_data[ind];
}

static int try_decode (rrbb_t block, int chan, int subchan, int alevel, retry_conf_t retry_conf)
{
	struct hdlc_state_s H;	
	int blen;			/* Block length in bits. */
	int i;
	unsigned int raw;			/* From demodulator. */
	int crc_failed = 1;
	int retry_conf_mode = retry_conf.mode;
	int retry_conf_type = retry_conf.type;
	int retry_conf_retry = retry_conf.retry;


	H.prev_raw = get_bit (block, 0);	  /* Actually last bit of the */
					/* opening flag so we can derive the */
					/* first data bit.  */

	/* Does this make sense? */
	/* This is the last bit of the "flag" pattern. */
	/* If it was corrupted we wouldn't have detected */
	/* the start of frame. */
	if (retry_conf.mode == RETRY_MODE_CONTIGUOUS && is_contig_bit_modified(0, retry_conf) ||
	    retry_conf.mode == RETRY_MODE_SEPARATED && is_sep_bit_modified(0, retry_conf)) {
	  H.prev_raw = ! H.prev_raw;
	}

	H.pat_det = 0;
	H.oacc = 0;
	H.olen = 0;
	H.frame_len = 0;

	blen = rrbb_get_len(block);
	/* Prepare space for the inserted bits in contiguous mode (separated mode for insert is not supported yet) */
	if (retry_conf.type == RETRY_TYPE_INSERT && retry_conf.mode == RETRY_MODE_CONTIGUOUS)
		blen+=retry_conf.u_bits.contig.nr_bits;

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
        if (retry_conf.type == RETRY_TYPE_NONE) 
        	dw_printf ("try_decode: blen=%d\n", blen);
#endif
	for (i=1; i<blen; i++) {
	  /* Get the value for the current bit */
	  raw = get_bit (block, i);
	  /* If swap two sep mode , swap the bit if needed */
	  if (retry_conf_retry == RETRY_SWAP_TWO_SEP) {
	      if (is_sep_bit_modified(i, retry_conf))
	        raw = ! raw;
	  /* Else if remove two sep bits mode , remove the bit if needed */
	  } else if (retry_conf_retry == RETRY_REMOVE_TWO_SEP) {
	      if (is_sep_bit_modified(i, retry_conf))
	         //Remove (ignore) this bit from the buffer!
                 continue;
	  }
	  /* Else handle all the others contiguous modes */
	  else if (retry_conf_mode == RETRY_MODE_CONTIGUOUS) {
	    /* If contiguous remove, ignore this bit from the buffer */
   	    if (retry_conf_type == RETRY_TYPE_REMOVE)  {
	      if ( is_contig_bit_modified(i, retry_conf))
	         //Remove (ignore) this bit from the buffer!
                 continue;
	    }
	    /* If insert bits mode */
            else if (retry_conf_type == RETRY_TYPE_INSERT) {
	        int nr_bits = retry_conf.u_bits.contig.nr_bits;
	        int bit_idx = retry_conf.u_bits.contig.bit_idx;
		/* If bit is after the index to insert, use the existing bit value (but shifted from the array) */
	        if (i >= bit_idx + nr_bits)
	          raw = get_bit (block, i-nr_bits);
		/* Else if this is a bit to insert, calculate the value of the bit from insert_value */
	        else if (is_contig_bit_modified(i, retry_conf)) {
	          raw = (retry_conf.insert_value >> (i-bit_idx)) & 1;
/*        	  dw_printf ("raw is %d for i %d bit_idx %d insert_value %d\n", 
	            raw, i, bit_idx, retry_conf.insert_value);*/
	        /* Else use the original bit value from the buffer */
	        } else {
	          /* Already set before */
		}
	    /* If in swap mode */
            } else if (retry_conf_type == RETRY_TYPE_SWAP) {
	        /* If this is the bit to swap */
	        if (is_contig_bit_modified(i, retry_conf))
	          raw = ! raw;
            } 

	  } else {
	  }
/*
 * Octets are sent LSB first.
 * Shift the most recent 8 bits thru the pattern detector.
 */
	    H.pat_det >>= 1;

/*
 * Using NRZI encoding,
 *   A '0' bit is represented by an inversion since previous bit.
 *   A '1' bit is represented by no change.
 *   Note: this code can be factorized with the raw != H.prev_raw code at the cost of processing time 
 */
	    if (raw == H.prev_raw) {
	      H.pat_det |= 0x80;
	      /* Valid data will never have 7 one bits in a row: exit. */
	      if (H.pat_det == 0xfe) {
#if DEBUGx
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("try_decode: found abort, i=%d\n", i);
#endif
	        return 0;
	      }
	      H.oacc >>= 1;
	      H.oacc |= 0x80;
	    } else {
	      H.prev_raw = raw;
	      /* The special pattern 01111110 indicates beginning and ending of a frame: exit. */
	      if (H.pat_det == 0x7e) {
#if DEBUGx
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("try_decode: found flag, i=%d\n", i);
#endif
	      return 0;
/*
 * If we have five '1' bits in a row, followed by a '0' bit,
 *
 *	011111xx
 *
 * the current '0' bit should be discarded because it was added for 
 * "bit stuffing."
 */
	
	      } else if ( (H.pat_det >> 2) == 0x1f ) {
	        continue;
	      }
	      H.oacc >>= 1;
	    }

/*
 * Now accumulate bits into octets, and complete octets
 * into the frame buffer.
 */

	    H.olen++;

	    if (H.olen & 8) {
	      H.olen = 0;

	      if (H.frame_len < MAX_FRAME_LEN) {
	        H.frame_buf[H.frame_len] = H.oacc;
		H.frame_len++;
	      
	      }
	    }
	  }	/* end of loop on all bits in block */
/* 
 * Do we have a minimum number of complete bytes?
 */

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("try_decode: olen=%d, frame_len=%d\n", H.olen, H.frame_len);
#endif

	if (H.olen == 0 && H.frame_len >= MIN_FRAME_LEN) {

	  unsigned short actual_fcs, expected_fcs;

#if DEBUGx 
        if (retry_conf.type == RETRY_TYPE_NONE) {
	  int j;
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("NEW WAY: frame len = %d\n", H.frame_len);
	  for (j=0; j<H.frame_len; j++) {
	    dw_printf ("  %02x", H.frame_buf[j]);
	  }
	  dw_printf ("\n");

        }
#endif
	  /* Check FCS, low byte first, and process... */

	  /* Alternatively, it is possible to include the two FCS bytes */
	  /* in the CRC calculation and look for a magic constant.  */
	  /* That would be easier in the case where the CRC is being */
	  /* accumulated along the way as the octets are received. */
	  /* I think making a second pass over it and comparing is */
	  /* easier to understand. */

	  actual_fcs = H.frame_buf[H.frame_len-2] | (H.frame_buf[H.frame_len-1] << 8);

	  expected_fcs = fcs_calc (H.frame_buf, H.frame_len - 2);

	  if (actual_fcs == expected_fcs && sanity_check (H.frame_buf, H.frame_len - 2, retry_conf.retry)) {

	
	      // TODO: Shouldn't be necessary to pass chan, subchan, alevel into
	      // try_decode because we can obtain them from block.
	      // Let's make sure that assumption is good...

	      assert (rrbb_get_chan(block) == chan);
	      assert (rrbb_get_subchan(block) == subchan);
	      assert (rrbb_get_audio_level(block) == alevel);

	      multi_modem_process_rec_frame (chan, subchan, H.frame_buf, H.frame_len - 2, alevel, retry_conf.retry);   /* len-2 to remove FCS. */
	      return 1;		/* success */
	  } else {

              goto failure;
          }
	} else {
              crc_failed = 0;
              goto failure;
	}
failure:
#if DEBUGx
        if (retry_conf.type == RETRY_TYPE_NONE ) {
              int j;
	      text_color_set(DW_COLOR_ERROR);
              if (crc_failed)
	            dw_printf ("CRC failed\n");
	      if (H.olen != 0) 
		      dw_printf ("Bad olen: %d \n", H.olen);
	      else if (H.frame_len < MIN_FRAME_LEN) {
		      dw_printf ("Frame too small\n");
                      goto end;
	      }

	      dw_printf ("FAILURE with frame: frame len = %d\n", H.frame_len);
	      dw_printf ("\n");
	      for (j=0; j<H.frame_len; j++) {
                      dw_printf (" %02x", H.frame_buf[j]);
	      }
	  dw_printf ("\nDEC\n");
	  for (j=0; j<H.frame_len; j++) {
	    dw_printf ("%c", H.frame_buf[j]>>1);
	  }
	  dw_printf ("\nORIG\n");
          for (j=0; j<H.frame_len; j++) {
	    dw_printf ("%c", H.frame_buf[j]);
	  }
	  dw_printf ("\n");
        }
#endif
end:
	return 0;	/* failure. */

} /* end try_decode */


/*
 * Try to weed out bogus packets from initially failed FCS matches.
 */

static int sanity_check (unsigned char *buf, int blen, retry_t bits_flipped)
{
	int alen;		/* Length of address part. */
	int j;

/*
 * No sanity check if we didn't try fixing the data.
 * Should we have different levels of checking depending on 
 * how much we try changing the raw data?
 */
	if (bits_flipped == RETRY_NONE) {
	  return 1;
	}

#if DEBUGx
	text_color_set(DW_COLOR_XMIT);
	dw_printf ("sanity_check: address part length = %d\n", alen);
#endif

/*
 * Address part must be a multiple of 7. 
 */

	alen = 0;
	for (j=0; j<blen && alen==0; j++) {
	  if (buf[j] & 0x01) {
	    alen = j + 1;
	  }
	}

	if (alen % 7 != 0) {
#if DEBUGx
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("sanity_check: FAILED.  Address part not multiple of 7.\n");
#endif
	  return 0;
	}

/*
 * Need at least 2 addresses and maximum of 8 digipeaters. 
 */

	if (alen/7 < 2 || alen/7 > 10) {
#if DEBUGx
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("sanity_check: FAILED.  Too few or many addresses.\n");
#endif
	  return 0;
	}

/* 
 * Addresses can contain only upper case letters, digits, and space. 
 */

	for (j=0; j<alen; j+=7) {

	  char addr[7];

	  addr[0] = buf[j+0] >> 1;
	  addr[1] = buf[j+1] >> 1;
	  addr[2] = buf[j+2] >> 1;
	  addr[3] = buf[j+3] >> 1;
	  addr[4] = buf[j+4] >> 1;
	  addr[5] = buf[j+5] >> 1;
	  addr[6] = '\0';


	  if ( (! isupper(addr[0]) && ! isdigit(addr[0])) ||
	       (! isupper(addr[1]) && ! isdigit(addr[1]) && addr[1] != ' ') ||
	       (! isupper(addr[2]) && ! isdigit(addr[2]) && addr[2] != ' ') ||
	       (! isupper(addr[3]) && ! isdigit(addr[3]) && addr[3] != ' ') ||
	       (! isupper(addr[4]) && ! isdigit(addr[4]) && addr[4] != ' ') ||
	       (! isupper(addr[5]) && ! isdigit(addr[5]) && addr[5] != ' ')) {
#if DEBUGx	  
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("sanity_check: FAILED.  Invalid characters in addresses \"%s\"\n", addr);
#endif
	    return 0;
	  }
	}

/*
 * The next two bytes should be 0x03 and 0xf0 for APRS.
 * Checking that would mean precluding use for other types of packet operation.
 *
 * The next section is also assumes APRS and might discard good data
 * for other protocols.   
 */


/*
 * Finally, look for bogus characters in the information part.
 * In theory, the bytes could have any values.
 * In practice, we find only printable ASCII characters and:
 *	
 *	0x0a	line feed
 *	0x0d	carriage return	
 *	0x1c	MIC-E
 *	0x1d	MIC-E
 *	0x1e	MIC-E
 *	0x1f	MIC-E
 *	0x7f	MIC-E
 *	0x80	"{UIV32N}<0x0d><0x9f><0x80>"
 *	0x9f	"{UIV32N}<0x0d><0x9f><0x80>"
 *	0xb0	degree symbol, ISO LATIN1
 *		  (Note: UTF-8 uses two byte sequence 0xc2 0xb0.)
 *	0xbe	invalid MIC-E encoding.
 *	0xf8	degree symbol, Microsoft code page 437
 *
 * So, if we have something other than these (in English speaking countries!), 
 * chances are that we have bogus data from twiddling the wrong bits.
 *
 * Notice that we shouldn't get here for good packets.  This extra level
 * of checking happens only if we twiddled a couple of bits, possibly
 * creating bad data.  We want to be very fussy.
 */

 	for (j=alen+2; j<blen; j++) {
	  int ch = buf[j];

	  if ( ! (( ch >= 0x1c && ch <= 0x7f) 
			|| ch == 0x0a 
			|| ch == 0x0d
			|| ch == 0x80
			|| ch == 0x9f
			|| ch == 0xb0
			|| ch == 0xf8) ) {
#if DEBUGx
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("sanity_check: FAILED.  Probably bogus info char 0x%02x\n", ch);
#endif
	    return 0;
	  }
	}

	return 1;
}


/* end hdlc_rec2.c */




// TODO:  Also in xmit.c.  Move to some common location.


/* Current time in seconds but more resolution than time(). */

/* We don't care what date a 0 value represents because we */
/* only use this to calculate elapsed time. */



static double dtime_now (void)
{
#if __WIN32__
	/* 64 bit integer is number of 100 nanosecond intervals from Jan 1, 1601. */

	FILETIME ft;
	
	GetSystemTimeAsFileTime (&ft);

	return ((( (double)ft.dwHighDateTime * (256. * 256. * 256. * 256.) + 
			(double)ft.dwLowDateTime ) / 10000000.) - 11644473600.);
#else
	/* tv_sec is seconds from Jan 1, 1970. */

	struct timespec ts;

	clock_gettime (CLOCK_REALTIME, &ts);

	return (ts.tv_sec + ts.tv_nsec / 1000000000.);
#endif
}

