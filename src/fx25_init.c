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
// -----------------------------------------------------------------------
//
//
// Some of this is based on:
//
// FX.25 Encoder
//	Author: Jim McGuire KB3MPL
//	Date: 	23 October 2007
//
// This program is a single-file implementation of the FX.25 encapsulation 
// structure for use with AX.25 data packets.  Details of the FX.25 
// specification are available at:
//     http://www.stensat.org/Docs/Docs.htm
//
// This program implements a single RS(255,239) FEC structure.  Future
// releases will incorporate more capabilities as accommodated in the FX.25
// spec.  
//
// The Reed Solomon encoding routines are based on work performed by
// Phil Karn.  Phil was kind enough to release his code under the GPL, as
// noted below.  Consequently, this FX.25 implementation is also released
// under the terms of the GPL.  
//
// Phil Karn's original copyright notice:
  /* Test the Reed-Solomon codecs
   * for various block sizes and with random data and random error patterns
   *
   * Copyright 2002 Phil Karn, KA9Q
   * May be used under the terms of the GNU General Public License (GPL)
   *
   */
 
#include "direwolf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>		// uint64_t
#include <inttypes.h>		// PRIx64
#include <assert.h>

#include "fx25.h"
#include "textcolor.h"


#define NTAB 3

static struct {
  int symsize;		// Symbol size, bits (1-8).  Always 8 for this application.
  int genpoly;		// Field generator polynomial coefficients.
  int fcs;		// First root of RS code generator polynomial, index form.
  int prim;		// Primitive element to generate polynomial roots.
  int nroots;		// RS code generator polynomial degree (number of roots).
			// Same as number of check bytes added.
  struct rs *rs;	// Pointer to RS codec control block.  Filled in at init time.
} Tab[NTAB] = {
  {8, 0x11d,   1,   1, 16, NULL },  // RS(255,239)
  {8, 0x11d,   1,   1, 32, NULL },  // RS(255,223)
  {8, 0x11d,   1,   1, 64, NULL },  // RS(255,191)
};

/*
 * Reference:	http://www.stensat.org/docs/FX-25_01_06.pdf
 *				FX.25
 *		Forward Error Correction Extension to
 *		AX.25 Link Protocol For Amateur Packet Radio
 *		Version: 0.01 DRAFT
 *		Date: 01 September 2006
 */

struct correlation_tag_s {
	uint64_t value;		// 64 bit value, send LSB first.
	int n_block_radio;	// Size of transmitted block, all in bytes.
	int k_data_radio;	// Size of transmitted data part.
	int n_block_rs;		// Size of RS algorithm block.
	int k_data_rs;		// Size of RS algorithm data part.
	int itab;		// Index into Tab array.
};

static const struct correlation_tag_s tags[16] = {
    /* Tag_00 */ { 0x566ED2717946107ELL,   0,   0,   0,   0, -1 },  //  Reserved

    /* Tag_01 */ { 0xB74DB7DF8A532F3ELL, 255, 239, 255, 239, 0 },  //  RS(255, 239) 16-byte check value, 239 information bytes
    /* Tag_02 */ { 0x26FF60A600CC8FDELL, 144, 128, 255, 239, 0 },  //  RS(144,128) - shortened RS(255, 239), 128 info bytes
    /* Tag_03 */ { 0xC7DC0508F3D9B09ELL,  80,  64, 255, 239, 0 },  //  RS(80,64) - shortened RS(255, 239), 64 info bytes
    /* Tag_04 */ { 0x8F056EB4369660EELL,  48,  32, 255, 239, 0 },  //  RS(48,32) - shortened RS(255, 239), 32 info bytes

    /* Tag_05 */ { 0x6E260B1AC5835FAELL, 255, 223, 255, 223, 1 },  //  RS(255, 223) 32-byte check value, 223 information bytes
    /* Tag_06 */ { 0xFF94DC634F1CFF4ELL, 160, 128, 255, 223, 1 },  //  RS(160,128) - shortened RS(255, 223), 128 info bytes
    /* Tag_07 */ { 0x1EB7B9CDBC09C00ELL,  96,  64, 255, 223, 1 },  //  RS(96,64) - shortened RS(255, 223), 64 info bytes
    /* Tag_08 */ { 0xDBF869BD2DBB1776LL,  64,  32, 255, 223, 1 },  //  RS(64,32) - shortened RS(255, 223), 32 info bytes

    /* Tag_09 */ { 0x3ADB0C13DEAE2836LL, 255, 191, 255, 191, 2 },  //  RS(255, 191) 64-byte check value, 191 information bytes
    /* Tag_0A */ { 0xAB69DB6A543188D6LL, 192, 128, 255, 191, 2 },  //  RS(192, 128) - shortened RS(255, 191), 128 info bytes
    /* Tag_0B */ { 0x4A4ABEC4A724B796LL, 128,  64, 255, 191, 2 },  //  RS(128, 64) - shortened RS(255, 191), 64 info bytes

    /* Tag_0C */ { 0x0293D578626B67E6LL,   0,   0,   0,   0, -1 },  //  Undefined
    /* Tag_0D */ { 0xE3B0B0D6917E58A6LL,   0,   0,   0,   0, -1 },  //  Undefined
    /* Tag_0E */ { 0x720267AF1BE1F846LL,   0,   0,   0,   0, -1 },  //  Undefined
    /* Tag_0F */ { 0x93210201E8F4C706LL,   0,   0,   0,   0, -1 }   //  Undefined
};



#define CLOSE_ENOUGH 8		// How many bits can be wrong in tag yet consider it a match?
				// Needs to be large enough to match with significant errors
				// but not so large to get frequent false matches.
				// Probably don't want >= 16 because the hamming distance between
				// any two pairs is 32.
				// What is a good number?  8??  12??  15?? 
				// 12 got many false matches with random noise.
				// Even 8 might be too high.  We see 2 or 4 bit errors here
				// at the point where decoding the block is very improbable.
				// After 2 months of continuous operation as a digipeater/iGate,
				// no false triggers were observed.  So 8 doesn't seem to be too
				// high for 1200 bps.  No study has been done for 9600 bps.

// Given a 64 bit correlation tag value, find acceptable match in table.
// Return index into table or -1 for no match.

// Both gcc and clang have a built in function to count the number of '1' bits
// in an integer.  This can result in a single machine instruction.  You might need
// to supply your own popcount function if using a different compiler.

int fx25_tag_find_match (uint64_t t)
{
	for (int c = CTAG_MIN; c <= CTAG_MAX; c++) {
	  if (__builtin_popcountll(t ^ tags[c].value) <= CLOSE_ENOUGH) {
	    //printf ("%016" PRIx64 " received\n", t);
	    //printf ("%016" PRIx64 " tag %d\n", tags[c].value, c);
	    //printf ("%016" PRIx64 " xor, popcount = %d\n", t ^ tags[c].value, __builtin_popcountll(t ^ tags[c].value));
	    return (c);
	  }
	}
	return (-1);
}



void free_rs_char(struct rs *rs){
  free(rs->alpha_to);
  free(rs->index_of);
  free(rs->genpoly);
  free(rs);
}


/*-------------------------------------------------------------
 *
 * Name:	fx25_init
 *
 * Purpose:	This must be called once before any of the other fx25 functions.
 *
 * Inputs:	debug_level - Controls level of informational / debug messages.
 *
 *			0		Only errors.
 *			1 (default)	Transmitting ctag. Currently no other way to know this.
 *			2 		Receive correlation tag detected.  FEC decode complete.
 *			3		Dump data going in and out.
 *
 *			Use command line -dx to increase level or -qx for quiet.
 *
 * Description:	Initialize 3 Reed-Solomon codecs, for 16, 32, and 64 check bytes.
 *
 *--------------------------------------------------------------*/

static int g_debug_level;

void fx25_init ( int debug_level )
{
	g_debug_level = debug_level;

	for (int i = 0 ; i < NTAB ; i++) {
	  Tab[i].rs = INIT_RS(Tab[i].symsize, Tab[i].genpoly, Tab[i].fcs,  Tab[i].prim, Tab[i].nroots);
	  if (Tab[i].rs == NULL) {
	        text_color_set(DW_COLOR_ERROR);
		dw_printf("FX.25 internal error: init_rs_char failed!\n");
		exit(EXIT_FAILURE);
	  }
	}

	// Verify integrity of tables and assumptions.
	// This also does a quick check for the popcount function.

	for (int j = 0; j < 16 ; j++) {
	  for (int k = 0; k < 16; k++) {
	    if (j == k) {
	      assert (__builtin_popcountll(tags[j].value ^ tags[k].value) == 0);
	    }
	    else {
	      assert (__builtin_popcountll(tags[j].value ^ tags[k].value) == 32);
	    }
          }
	}

	for (int j = CTAG_MIN; j <= CTAG_MAX; j++) {
	  assert (tags[j].n_block_radio - tags[j].k_data_radio == Tab[tags[j].itab].nroots);
	  assert (tags[j].n_block_rs - tags[j].k_data_rs == Tab[tags[j].itab].nroots);
	  assert (tags[j].n_block_rs == FX25_BLOCK_SIZE);
	}

	assert (fx25_pick_mode (100+1, 239) == 1);
	assert (fx25_pick_mode (100+1, 240) == -1);

	assert (fx25_pick_mode (100+5, 223) == 5);
	assert (fx25_pick_mode (100+5, 224) == -1);

	assert (fx25_pick_mode (100+9, 191) == 9);
	assert (fx25_pick_mode (100+9, 192) == -1);

	assert (fx25_pick_mode (16, 32) == 4);
	assert (fx25_pick_mode (16, 64) == 3);
	assert (fx25_pick_mode (16, 128) == 2);
	assert (fx25_pick_mode (16, 239) == 1);
	assert (fx25_pick_mode (16, 240) == -1);

	assert (fx25_pick_mode (32, 32) == 8);
	assert (fx25_pick_mode (32, 64) == 7);
	assert (fx25_pick_mode (32, 128) == 6);
	assert (fx25_pick_mode (32, 223) == 5);
	assert (fx25_pick_mode (32, 234) == -1);

	assert (fx25_pick_mode (64, 64) == 11);
	assert (fx25_pick_mode (64, 128) == 10);
	assert (fx25_pick_mode (64, 191) == 9);
	assert (fx25_pick_mode (64, 192) == -1);

	assert (fx25_pick_mode (1, 32) == 4);
	assert (fx25_pick_mode (1, 33) == 3);
	assert (fx25_pick_mode (1, 64) == 3);
	assert (fx25_pick_mode (1, 65) == 6);
	assert (fx25_pick_mode (1, 128) == 6);
	assert (fx25_pick_mode (1, 191) == 9);
	assert (fx25_pick_mode (1, 223) == 5);
	assert (fx25_pick_mode (1, 239) == 1);
	assert (fx25_pick_mode (1, 240) == -1);

}  // fx25_init


// Get properties of specified CTAG number.

struct rs *fx25_get_rs (int ctag_num)
{
	assert (ctag_num >= CTAG_MIN && ctag_num <= CTAG_MAX);
	assert (tags[ctag_num].itab >= 0 && tags[ctag_num].itab < NTAB);
	assert (Tab[tags[ctag_num].itab].rs != NULL);
	return (Tab[tags[ctag_num].itab].rs);
}

uint64_t fx25_get_ctag_value (int ctag_num)
{
	assert (ctag_num >= CTAG_MIN && ctag_num <= CTAG_MAX);
	return (tags[ctag_num].value);
}

int fx25_get_k_data_radio (int ctag_num)
{
	assert (ctag_num >= CTAG_MIN && ctag_num <= CTAG_MAX);
	return (tags[ctag_num].k_data_radio);
}

int fx25_get_k_data_rs (int ctag_num)
{
	assert (ctag_num >= CTAG_MIN && ctag_num <= CTAG_MAX);
	return (tags[ctag_num].k_data_rs);
}

int fx25_get_nroots (int ctag_num)
{
	assert (ctag_num >= CTAG_MIN && ctag_num <= CTAG_MAX);
	return (Tab[tags[ctag_num].itab].nroots);
}

int fx25_get_debug (void)
{
	return (g_debug_level);
}

/*-------------------------------------------------------------
 *
 * Name:	fx25_pick_mode
 *
 * Purpose:	Pick suitable transmission format based on user preference
 *		and size of data part required.
 *
 * Inputs:	fx_mode	- 0 = none.
 *			1 = pick a tag automatically.
 *			16, 32, 64 = use this many check bytes.
 *			100 + n = use tag n.
 *
 *			0 and 1 would be the most common.
 *			Others are mostly for testing.
 *
 *		dlen - 	Required size for transmitted "data" part, in bytes.
 *			This includes the AX.25 frame with bit stuffing and a flag
 *			pattern on each end.
 *
 * Returns:	Correlation tag number in range of CTAG_MIN thru CTAG_MAX.
 *		-1 is returned for failure.
 *		The caller should fall back to using plain old AX.25.
 *
 *--------------------------------------------------------------*/

int fx25_pick_mode (int fx_mode, int dlen)
{
	if (fx_mode <= 0) return (-1);

// Specify a specific tag by adding 100 to the number.
// Fails if data won't fit.

	if (fx_mode - 100 >= CTAG_MIN && fx_mode - 100 <= CTAG_MAX) {
	  if (dlen <= fx25_get_k_data_radio(fx_mode - 100)) {
	    return (fx_mode - 100);
	  }
	  else {
	    return (-1);	// Assuming caller prints failure message.
	  }
	}

// Specify number of check bytes.
// Pick the shortest one that can handle the required data length.

	else if (fx_mode == 16 || fx_mode == 32 || fx_mode == 64) {
	  for (int k = CTAG_MAX; k >= CTAG_MIN; k--) {
	    if (fx_mode == fx25_get_nroots(k) && dlen <= fx25_get_k_data_radio(k)) {
	      return (k);
	    }
	  }
	  return (-1);
	}

// For any other number, [[ or if the preference was not possible, ?? ]]
// try to come up with something reasonable.  For shorter frames,
// use smaller overhead.  For longer frames, where an error is
// more probable, use more check bytes.  When the data gets even
// larger, check bytes must be reduced to fit in block size.
// When all else fails, fall back to normal AX.25.
// Some of this is from observing UZ7HO Soundmodem behavior.
//
//	Tag 	Data 	Check 	Max Num
//	Number	Bytes	Bytes	Repaired
//	------	-----	-----	-----
//	0x04	32	16	8
//	0x03	64	16	8
//	0x06	128	32	16
//	0x09	191	64	32
//	0x05	223	32	16
//	0x01	239	16	8
//	none	larger		
//
// The PRUG FX.25 TNC has additional modes that will handle larger frames
// by using multiple RS blocks.  This is a future possibility but needs
// to be coordinated with other FX.25 developers so we maintain compatibility.
// See https://web.tapr.org/meetings/DCC_2020/JE1WAZ/DCC-2020-PRUG-FINAL.pptx

	static const int prefer[6] = { 0x04, 0x03, 0x06, 0x09, 0x05, 0x01 };
	for (int k = 0; k < 6; k++) {
	  int m = prefer[k];
	  if (dlen <= fx25_get_k_data_radio(m)) {
	    return (m);
	  }
	}
	return (-1);

// TODO: revisit error messages, produced by caller, when this returns -1.

}


/* Initialize a Reed-Solomon codec
 *   symsize = symbol size, bits (1-8) - always 8 for this application.
 *   gfpoly = Field generator polynomial coefficients
 *   fcr = first root of RS code generator polynomial, index form
 *   prim = primitive element to generate polynomial roots
 *   nroots = RS code generator polynomial degree (number of roots)
 */

struct rs *INIT_RS(unsigned int symsize,unsigned int gfpoly,unsigned fcr,unsigned prim,
		unsigned int nroots){
  struct rs *rs;
  int i, j, sr,root,iprim;

  if(symsize > 8*sizeof(DTYPE))
    return NULL; /* Need version with ints rather than chars */

  if(fcr >= (1<<symsize))
    return NULL;
  if(prim == 0 || prim >= (1<<symsize))
    return NULL;
  if(nroots >= (1<<symsize))
    return NULL; /* Can't have more roots than symbol values! */

  rs = (struct rs *)calloc(1,sizeof(struct rs));
  if (rs == NULL) {
    text_color_set(DW_COLOR_ERROR);
    dw_printf ("FATAL ERROR: Out of memory.\n");
    exit (EXIT_FAILURE);
  }
  rs->mm = symsize;
  rs->nn = (1<<symsize)-1;

  rs->alpha_to = (DTYPE *)calloc((rs->nn+1),sizeof(DTYPE));
  if(rs->alpha_to == NULL){
    text_color_set(DW_COLOR_ERROR);
    dw_printf ("FATAL ERROR: Out of memory.\n");
    exit (EXIT_FAILURE);
  }
  rs->index_of = (DTYPE *)calloc((rs->nn+1),sizeof(DTYPE));
  if(rs->index_of == NULL){
    text_color_set(DW_COLOR_ERROR);
    dw_printf ("FATAL ERROR: Out of memory.\n");
    exit (EXIT_FAILURE);
  }

  /* Generate Galois field lookup tables */
  rs->index_of[0] = A0; /* log(zero) = -inf */
  rs->alpha_to[A0] = 0; /* alpha**-inf = 0 */
  sr = 1;
  for(i=0;i<rs->nn;i++){
    rs->index_of[sr] = i;
    rs->alpha_to[i] = sr;
    sr <<= 1;
    if(sr & (1<<symsize))
      sr ^= gfpoly;
    sr &= rs->nn;
  }
  if(sr != 1){
    /* field generator polynomial is not primitive! */
    free(rs->alpha_to);
    free(rs->index_of);
    free(rs);
    return NULL;
  }

  /* Form RS code generator polynomial from its roots */
  rs->genpoly = (DTYPE *)calloc((nroots+1),sizeof(DTYPE));
  if(rs->genpoly == NULL){
    text_color_set(DW_COLOR_ERROR);
    dw_printf ("FATAL ERROR: Out of memory.\n");
    exit (EXIT_FAILURE);
  }
 rs->fcr = fcr;
  rs->prim = prim;
  rs->nroots = nroots;

  /* Find prim-th root of 1, used in decoding */
  for(iprim=1;(iprim % prim) != 0;iprim += rs->nn)
    ;
  rs->iprim = iprim / prim;

  rs->genpoly[0] = 1;
  for (i = 0,root=fcr*prim; i < nroots; i++,root += prim) {
    rs->genpoly[i+1] = 1;

    /* Multiply rs->genpoly[] by  @**(root + x) */
    for (j = i; j > 0; j--){
      if (rs->genpoly[j] != 0)
	rs->genpoly[j] = rs->genpoly[j-1] ^ rs->alpha_to[modnn(rs,rs->index_of[rs->genpoly[j]] + root)];
      else
	rs->genpoly[j] = rs->genpoly[j-1];
    }
    /* rs->genpoly[0] can never be zero */
    rs->genpoly[0] = rs->alpha_to[modnn(rs,rs->index_of[rs->genpoly[0]] + root)];
  }
    /* convert rs->genpoly[] to index form for quicker encoding */
  for (i = 0; i <= nroots; i++) {
    rs->genpoly[i] = rs->index_of[rs->genpoly[i]];
  }
  
// diagnostic prints
#if 0
  printf("Alpha To:\n\r");
  for (i=0; i < sizeof(DTYPE)*(rs->nn+1); i++) 
    printf("0x%2x,", rs->alpha_to[i]);
  printf("\n\r");

  printf("Index Of:\n\r");
  for (i=0; i < sizeof(DTYPE)*(rs->nn+1); i++) 
    printf("0x%2x,", rs->index_of[i]);
  printf("\n\r");
  
  printf("GenPoly:\n\r");
  for (i = 0; i <= nroots; i++) 
    printf("0x%2x,", rs->genpoly[i]);
  printf("\n\r");
#endif
  return rs;
}

  
// TEMPORARY!!!
// FIXME: We already have multiple copies of this.
// Consolidate them into one somewhere.

void fx_hex_dump (unsigned char *p, int len) 
{
	int n, i, offset;

	offset = 0;
	while (len > 0) {
	  n = len < 16 ? len : 16; 
	  dw_printf ("  %03x: ", offset);
	  for (i=0; i<n; i++) {
	    dw_printf (" %02x", p[i]);
	  }
	  for (i=n; i<16; i++) {
	    dw_printf ("   ");
	  }
	  dw_printf ("  ");
	  for (i=0; i<n; i++) {
	    dw_printf ("%c", isprint(p[i]) ? p[i] : '.');
	  }
	  dw_printf ("\n");
	  p += 16;
	  offset += 16;
	  len -= 16;
	}
}

