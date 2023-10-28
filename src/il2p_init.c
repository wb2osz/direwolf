//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2021  John Langner, WB2OSZ
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

#include "direwolf.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "textcolor.h"
#include "fx25.h"		// For Reed Solomon stuff.
#include "il2p.h"

// Interesting related stuff:
// https://www.kernel.org/doc/html/v4.15/core-api/librs.html
// https://berthub.eu/articles/posts/reed-solomon-for-programmers/ 


#define MAX_NROOTS 16

#define NTAB 5

static struct {
  int symsize;          // Symbol size, bits (1-8).  Always 8 for this application.
  int genpoly;          // Field generator polynomial coefficients.
  int fcs;              // First root of RS code generator polynomial, index form.
			// FX.25 uses 1 but IL2P uses 0.
  int prim;             // Primitive element to generate polynomial roots.
  int nroots;           // RS code generator polynomial degree (number of roots).
                        // Same as number of check bytes added.
  struct rs *rs;        // Pointer to RS codec control block.  Filled in at init time.
} Tab[NTAB] = {
  {8, 0x11d,   0,   1, 2, NULL },  // 2 parity
  {8, 0x11d,   0,   1, 4, NULL },  // 4 parity
  {8, 0x11d,   0,   1, 6, NULL },  // 6 parity
  {8, 0x11d,   0,   1, 8, NULL },  // 8 parity
  {8, 0x11d,   0,   1, 16, NULL },  // 16 parity
};



static int g_il2p_debug = 0;


/*-------------------------------------------------------------
 *
 * Name:	il2p_init
 *
 * Purpose:	This must be called at application start up time.
 *		It sets up tables for the Reed-Solomon functions.
 *
 * Inputs:	debug	- Enable debug output.
 *
 *--------------------------------------------------------------*/

void il2p_init (int il2p_debug)
{
	g_il2p_debug = il2p_debug;

        for (int i = 0 ; i < NTAB ; i++) {
	  assert (Tab[i].nroots <= MAX_NROOTS);
          Tab[i].rs = INIT_RS(Tab[i].symsize, Tab[i].genpoly, Tab[i].fcs,  Tab[i].prim, Tab[i].nroots);
          if (Tab[i].rs == NULL) {
                text_color_set(DW_COLOR_ERROR);
                dw_printf("IL2P internal error: init_rs_char failed!\n");
                exit(EXIT_FAILURE);
          }
        }

} // end il2p_init


int il2p_get_debug(void)
{
	return (g_il2p_debug);
}
void il2p_set_debug(int debug)
{
	g_il2p_debug = debug;
}


// Find RS codec control block for specified number of parity symbols.

struct rs *il2p_find_rs(int nparity)
{
	for (int n = 0; n < NTAB; n++) {
	    if (Tab[n].nroots == nparity) {
	        return (Tab[n].rs);
	    }
	}
        text_color_set(DW_COLOR_ERROR);
	dw_printf ("IL2P INTERNAL ERROR: il2p_find_rs: control block not found for nparity = %d.\n", nparity);
	return (Tab[0].rs);
}


/*-------------------------------------------------------------
 *
 * Name:	void il2p_encode_rs
 *
 * Purpose:	Add parity symbols to a block of data.
 *
 * Inputs:	tx_data		Header or other data to transmit.
 *		data_size	Number of data bytes in above.
 *		num_parity	Number of parity symbols to add.
 *				Maximum of IL2P_MAX_PARITY_SYMBOLS.
 *
 * Outputs:	parity_out	Specified number of parity symbols
 *
 * Restriction:	data_size + num_parity <= 255 which is the RS block size.
 *		The caller must ensure this.
 *
 *--------------------------------------------------------------*/

void il2p_encode_rs (unsigned char *tx_data, int data_size, int num_parity, unsigned char *parity_out)
{
	assert (data_size >= 1);
	assert (num_parity == 2 || num_parity == 4 || num_parity == 6 || num_parity == 8 || num_parity == 16);
	assert (data_size + num_parity <= 255);

	unsigned char rs_block[FX25_BLOCK_SIZE];
	memset (rs_block, 0, sizeof(rs_block));
	memcpy (rs_block + sizeof(rs_block) - data_size - num_parity, tx_data, data_size);
	ENCODE_RS (il2p_find_rs(num_parity), rs_block, parity_out);
}

/*-------------------------------------------------------------
 *
 * Name:	void il2p_decode_rs
 *
 * Purpose:	Check and attempt to fix block with FEC.
 *
 * Inputs:	rec_block	Received block composed of data and parity.
 *				Total size is sum of following two parameters.
 *		data_size	Number of data bytes in above.
 *		num_parity	Number of parity symbols (bytes) in above.
 *
 * Outputs:	out		Original with possible corrections applied.
 *				data_size bytes.
 *
 * Returns:	-1 for unrecoverable.
 *		>= 0 for success.  Number of symbols corrected.
 *
 *--------------------------------------------------------------*/

int il2p_decode_rs (unsigned char *rec_block, int data_size, int num_parity, unsigned char *out)
{

	//  Use zero padding in front if data size is too small.

	int n = data_size + num_parity;		// total size in.

	unsigned char rs_block[FX25_BLOCK_SIZE];

	// We could probably do this more efficiently by skipping the
	// processing of the bytes known to be zero.  Good enough for now.

	memset (rs_block, 0, sizeof(rs_block) - n);
	memcpy (rs_block + sizeof(rs_block) - n, rec_block, n);

	if (il2p_get_debug() >= 3) {
            text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("==============================  il2p_decode_rs  ==============================\n");
	    dw_printf ("%d filler zeros, %d data, %d parity\n", (int)(sizeof(rs_block) - n), data_size, num_parity);
	    fx_hex_dump (rs_block, sizeof (rs_block));
	}

	int derrlocs[FX25_MAX_CHECK];	// Half would probably be OK.

	int derrors = DECODE_RS(il2p_find_rs(num_parity), rs_block, derrlocs, 0);
	memcpy (out, rs_block + sizeof(rs_block) - n, data_size);

	if (il2p_get_debug() >= 3) {
	    if (derrors == 0) {
	        dw_printf ("No errors reported for RS block.\n");
	    }
	    else if (derrors > 0) {
	       dw_printf ("%d errors fixed in positions:\n", derrors);
	       for (int j = 0; j < derrors; j++) {
	           dw_printf ("        %3d  (0x%02x)\n", derrlocs[j] , derrlocs[j]);
	       }
	       fx_hex_dump (rs_block, sizeof (rs_block));
	    }
	}

	// It is possible to have a situation where too many errors are
	// present but the algorithm could get a good code block by "fixing"
	// one of the padding bytes that should be 0.

	for (int i = 0; i < derrors; i++) {
	    if (derrlocs[i] < sizeof(rs_block) - n) {
	        if (il2p_get_debug() >= 3) {
		    text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("RS DECODE ERROR!  Padding position %d should be 0 but it was set to %02x.\n", derrlocs[i], rs_block[derrlocs[i]]);
	        }
	        derrors = -1;
	        break;
	    }
	}

	if (il2p_get_debug() >= 3) {
            text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("==============================  il2p_decode_rs  returns %d  ==============================\n", derrors);
	}
	return (derrors);
}

// end il2p_init.c