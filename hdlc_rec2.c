//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013  John Langner, WB2OSZ
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
 *******************************************************************************/

#include <stdio.h>
#include <assert.h>
#include <ctype.h>

#include "direwolf.h"
#include "hdlc_rec2.h"
#include "fcs_calc.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "rrbb.h"
#include "rdq.h"
#include "multi_modem.h"


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




static int try_decode (rrbb_t block, int chan, int subchan, int alevel, retry_t bits_flipped, int flip_a, int flip_b, int flip_c);
static int try_to_fix_quick_now (rrbb_t block, int chan, int subchan, int alevel, retry_t fix_bits);
static int sanity_check (unsigned char *buf, int blen, retry_t bits_flipped);
#if DEBUG
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

	ok = try_decode (block, chan, subchan, alevel, RETRY_NONE, -1, -1, -1);

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

	if (fix_bits < RETRY_TWO_SEP) {
	  rrbb_delete (block); 
	  return;
	}

	rdq_append (block);

}


static int try_to_fix_quick_now (rrbb_t block, int chan, int subchan, int alevel, retry_t fix_bits)
{
	int ok;
	int len, i;


	len = rrbb_get_len(block);

/* 
 * Try fixing one bit.   
 */
	if (fix_bits < RETRY_SINGLE) {
	  return 0;
	}

	for (i=0; i<len; i++) {
	  ok = try_decode (block, chan, subchan, alevel, RETRY_SINGLE, i, -1, -1);
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
	if (fix_bits < RETRY_DOUBLE) {
	  return 0;
	}

	for (i=0; i<len-1; i++) {
	  ok = try_decode (block, chan, subchan, alevel, RETRY_DOUBLE, i, i+1, -1);
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
	if (fix_bits < RETRY_TRIPLE) {
	  return 0;
	}

	len = rrbb_get_len(block);
	for (i=0; i<len-2; i++) {
	  ok = try_decode (block, chan, subchan, alevel, RETRY_TRIPLE, i, i+1, i+2);
	  if (ok) {
#if DEBUG
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("*** Success by flipping TRIPLE bit %d of %d ***\n", i, len);
#endif
	    return 1;
	  }
	}

	return 0;
}

void hdlc_rec2_try_to_fix_later (rrbb_t block, int chan, int subchan, int alevel)
{
	int ok;
	int len, i;
#if DEBUG
	double tstart, tend;
#endif

	len = rrbb_get_len(block);

/*
 * Two  non-adjacent ("separated") single bits.
 * It chews up a lot of CPU time.  Test takes 4 times longer to run.
 *
 * Ran up to 4.82 seconds for 1040 bits before giving up.
 * Processing time is order N squared so time goes up rapidly with larger frames.
 */

#if DEBUG
	tstart = dtime_now();
#endif
	len = rrbb_get_len(block);
	for (i=0; i<len-2; i++) {
	  int j;

	  ok = 0;
	  for (j=i+2; j<len; j++) {
	    ok = try_decode (block, chan, subchan, alevel, RETRY_TWO_SEP, i, j, -1);  
	    if (ok)
	      break;
	  }	  
	  if (ok) {
#if DEBUG
	    tend = dtime_now();
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("*** Success by flipping TWO SEPARATED bits %d and %d of %d *** %.3f sec.\n", i, j, len, tend-tstart);
#endif
	    return;
	  }
	}
#if DEBUGx
	tend = dtime_now();
	text_color_set(DW_COLOR_ERROR);
	dw_printf ("*** No luck flipping TWO SEPARATED bits of %d *** %.3f sec.\n", len, tend-tstart);
#endif

	return;
}




static int try_decode (rrbb_t block, int chan, int subchan, int alevel, retry_t bits_flipped, int flip_a, int flip_b, int flip_c)
{
	struct hdlc_state_s H;	
	int blen;			/* Block length in bits. */
	int i;
	int raw;			/* From demodulator. */
	int dbit;			/* Data bit after undoing NRZI. */


	H.prev_raw = rrbb_get_bit (block, 0);	  /* Actually last bit of the */
					/* opening flag so we can derive the */
					/* first data bit.  */

	/* Does this make sense? */
	/* This is the last bit of the "flag" pattern. */
	/* If it was corrupted we wouldn't have detected */
	/* the start of frame. */

	if (0 == flip_a || 0 == flip_b || 0 == flip_c){
	  H.prev_raw = ! H.prev_raw;
	}

	H.pat_det = 0;
	H.oacc = 0;
	H.olen = 0;
	H.frame_len = 0;

	blen = rrbb_get_len (block);

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("try_decode: blen=%d\n", blen);
#endif

	for (i=1; i<blen; i++) {

	  raw = rrbb_get_bit (block, i);

	  if (i == flip_a || i == flip_b || i == flip_c){
	    raw = ! raw;
	  }

/*
 * Using NRZI encoding,
 *   A '0' bit is represented by an inversion since previous bit.
 *   A '1' bit is represented by no change.
 */

	  dbit = (raw == H.prev_raw);
	  H.prev_raw = raw;

/*
 * Octets are sent LSB first.
 * Shift the most recent 8 bits thru the pattern detector.
 */
	  H.pat_det >>= 1;
	  if (dbit) {
	    H.pat_det |= 0x80;
	  }

	  if (H.pat_det == 0x7e) {
	    /* The special pattern 01111110 indicates beginning and ending of a frame. */
#if DEBUGx
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("try_decode: found flag, i=%d\n", i);
#endif
	    return 0;
	  }
	  else if (H.pat_det == 0xfe) {
	    /* Valid data will never have 7 one bits in a row. */
#if DEBUGx
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("try_decode: found abort, i=%d\n", i);
#endif
 	    return 0;
	  }
	  else if ( (H.pat_det & 0xfc) == 0x7c ) {
/*
 * If we have five '1' bits in a row, followed by a '0' bit,
 *
 *	0111110xx
 *
 * the current '0' bit should be discarded because it was added for 
 * "bit stuffing."
 */
	    ;
	  } else {

/*
 * In all other cases, accumulate bits into octets, and complete octets
 * into the frame buffer.
 */

	    H.oacc >>= 1;
	    if (dbit) {
	      H.oacc |= 0x80;
	    }
	    H.olen++;

	    if (H.olen == 8) {
	      H.olen = 0;

	      if (H.frame_len < MAX_FRAME_LEN) {
		H.frame_buf[H.frame_len] = H.oacc;
		H.frame_len++;
	      }
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
	  int j;
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("NEW WAY: frame len = %d\n", H.frame_len);
	  for (j=0; j<H.frame_len; j++) {
	    dw_printf ("  %02x", H.frame_buf[j]);
	  }
	  dw_printf ("\n");
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

	  if (actual_fcs == expected_fcs && sanity_check (H.frame_buf, H.frame_len - 2, bits_flipped)) {

	
	      // TODO: Shouldn't be necessary to pass chan, subchan, alevel into
	      // try_decode because we can obtain them from block.
	      // Let's make sure that assumption is good...

	      assert (rrbb_get_chan(block) == chan);
	      assert (rrbb_get_subchan(block) == subchan);
	      assert (rrbb_get_audio_level(block) == alevel);

	      multi_modem_process_rec_frame (chan, subchan, H.frame_buf, H.frame_len - 2, alevel, bits_flipped);   /* len-2 to remove FCS. */
	      return 1;		/* success */
	  }
	}
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


#if DEBUG

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

#endif 
