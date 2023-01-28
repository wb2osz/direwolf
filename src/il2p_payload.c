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
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "textcolor.h"
#include "il2p.h"


/*--------------------------------------------------------------------------------
 *
 * File:	il2p_payload.c
 *
 * Purpose:	Functions dealing with the payload.
 *
 *--------------------------------------------------------------------------------*/


/*--------------------------------------------------------------------------------
 *
 * Function:	il2p_payload_compute
 *
 * Purpose:	Compute number and sizes of data blocks based on total size.
 *
 * Inputs:	payload_size	0 to 1023.  (IL2P_MAX_PAYLOAD_SIZE)
 *		max_fec		true for 16 parity symbols, false for automatic.
 *
 * Outputs:	*p		Payload block sizes and counts.
 *				Number of parity symbols per block.
 *
 * Returns:	Number of bytes in the encoded format.
 *		Could be 0 for no payload blocks.
 *		-1 for error (i.e. invalid unencoded size: <0 or >1023)
 *
 *--------------------------------------------------------------------------------*/

int il2p_payload_compute (il2p_payload_properties_t *p, int payload_size, int max_fec)
{
	memset (p, 0, sizeof(il2p_payload_properties_t));

	if (payload_size < 0 || payload_size > IL2P_MAX_PAYLOAD_SIZE) {
	    return (-1);
	}
	if (payload_size == 0) {
	    return (0);
	}

	if (max_fec) {
	    p->payload_byte_count = payload_size;
	    p->payload_block_count = (p->payload_byte_count + 238)  / 239;
	    p->small_block_size = p->payload_byte_count / p->payload_block_count;
	    p->large_block_size = p->small_block_size + 1;
	    p->large_block_count = p->payload_byte_count - (p->payload_block_count * p->small_block_size);
	    p->small_block_count = p->payload_block_count - p->large_block_count;
	    p->parity_symbols_per_block = 16;
	}
	else {
	    p->payload_byte_count = payload_size;
	    p->payload_block_count = (p->payload_byte_count + 246) / 247;
	    p->small_block_size = p->payload_byte_count / p->payload_block_count;
	    p->large_block_size = p->small_block_size + 1;
	    p->large_block_count = p->payload_byte_count - (p->payload_block_count * p->small_block_size);
	    p->small_block_count = p->payload_block_count - p->large_block_count;
	    //p->parity_symbols_per_block = (p->small_block_size / 32) + 2;  // Looks like error in documentation

	    // It would work if the number of parity symbols was based on large block size.

	    if (p->small_block_size <= 61) p->parity_symbols_per_block = 2;
	    else if (p->small_block_size <= 123) p->parity_symbols_per_block = 4;
	    else if (p->small_block_size <= 185) p->parity_symbols_per_block = 6;
	    else if (p->small_block_size <= 247) p->parity_symbols_per_block = 8;
	    else {
	        // Should not happen.  But just in case...
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("IL2P parity symbol per payload block error.  small_block_size = %d\n", p->small_block_size);
	        return (-1);
	    }
	}

	// Return the total size for the encoded format.

	return (p->small_block_count * (p->small_block_size + p->parity_symbols_per_block) +
                p->large_block_count * (p->large_block_size + p->parity_symbols_per_block));

} // end il2p_payload_compute



/*--------------------------------------------------------------------------------
 *
 * Function:	il2p_encode_payload
 *
 * Purpose:	Split payload into multiple blocks such that each set
 *		of data and parity symbols fit into a 255 byte RS block.
 *
 * Inputs:	*payload	Array of bytes.
 *		payload_size	0 to 1023.  (IL2P_MAX_PAYLOAD_SIZE)
 *		max_fec		true for 16 parity symbols, false for automatic.
 *
 * Outputs:	*enc		Encoded payload for transmission.
 *				Up to IL2P_MAX_ENCODED_SIZE bytes.
 *
 * Returns:	-1 for error (i.e. invalid size)
 *		0 for no blocks.  (i.e. size zero)
 *		Number of bytes generated.  Maximum IL2P_MAX_ENCODED_SIZE.
 *
 * Note:	I interpreted the protocol spec as saying the LFSR state is retained
 *		between data blocks.  During interoperability testing, I found that
 *		was not the case.  It is reset for each data block.
 *
 *--------------------------------------------------------------------------------*/


int il2p_encode_payload (unsigned char *payload, int payload_size, int max_fec, unsigned char *enc)
{
	if (payload_size > IL2P_MAX_PAYLOAD_SIZE) return (-1);
	if (payload_size == 0) return (0);

// Determine number of blocks and sizes.

	il2p_payload_properties_t ipp;
	int e;
	e = il2p_payload_compute (&ipp, payload_size, max_fec);
	if (e <= 0) {
	    return (e);
	}

	unsigned char *pin = payload;
	unsigned char *pout = enc;
	int encoded_length = 0;
	unsigned char scram[256];
	unsigned char parity[IL2P_MAX_PARITY_SYMBOLS];

// First the large blocks.

	for (int b = 0; b < ipp.large_block_count; b++) {

	    il2p_scramble_block (pin, scram, ipp.large_block_size);
	    memcpy (pout, scram, ipp.large_block_size);
	    pin += ipp.large_block_size;
	    pout += ipp.large_block_size;
	    encoded_length += ipp.large_block_size;
	    il2p_encode_rs (scram, ipp.large_block_size, ipp.parity_symbols_per_block, parity);
	    memcpy (pout, parity, ipp.parity_symbols_per_block);
	    pout += ipp.parity_symbols_per_block;
	    encoded_length += ipp.parity_symbols_per_block;
	}

// Then the small blocks.

	for (int b = 0; b < ipp.small_block_count; b++) {

	    il2p_scramble_block (pin, scram, ipp.small_block_size);
	    memcpy (pout, scram, ipp.small_block_size);
	    pin += ipp.small_block_size;
	    pout += ipp.small_block_size;
	    encoded_length += ipp.small_block_size;
	    il2p_encode_rs (scram, ipp.small_block_size, ipp.parity_symbols_per_block, parity);
	    memcpy (pout, parity, ipp.parity_symbols_per_block);
	    pout += ipp.parity_symbols_per_block;
	    encoded_length += ipp.parity_symbols_per_block;
	}

	return (encoded_length);

} // end il2p_encode_payload


/*--------------------------------------------------------------------------------
 *
 * Function:	il2p_decode_payload
 *
 * Purpose:	Extract original data from encoded payload.
 *
 * Inputs:	received	Array of bytes.  Size is unknown but in practice it
 *				must not exceed IL2P_MAX_ENCODED_SIZE.
 *		payload_size	0 to 1023.  (IL2P_MAX_PAYLOAD_SIZE)
 *				Expected result size based on header.
 *		max_fec		true for 16 parity symbols, false for automatic.
 *
 * Outputs:	payload_out	Recovered payload.
 *
 * In/Out:	symbols_corrected	Number of symbols corrected.
 *				
 *
 * Returns:	Number of bytes extracted.  Should be same as payload_size going in.
 *		-3 for unexpected internal inconsistency.
 *		-2 for unable to recover from signal corruption.
 *		-1 for invalid size.
 *		0 for no blocks.  (i.e. size zero)
 *		
 * Description:	Each block is scrambled separately but the LSFR state is carried
 *		from the first payload block to the next.
 *
 *--------------------------------------------------------------------------------*/

int il2p_decode_payload (unsigned char *received, int payload_size, int max_fec, unsigned char *payload_out, int *symbols_corrected)
{
// Determine number of blocks and sizes.

	il2p_payload_properties_t ipp;
	int e;
	e = il2p_payload_compute (&ipp, payload_size, max_fec);
	if (e <= 0) {
	    return (e);
	}

	unsigned char *pin = received;
	unsigned char *pout = payload_out;
	int decoded_length = 0;
	int failed = 0;

// First the large blocks.

	for (int b = 0; b < ipp.large_block_count; b++) {
	    unsigned char corrected_block[255];
	    int e = il2p_decode_rs (pin, ipp.large_block_size, ipp.parity_symbols_per_block, corrected_block);

	    // dw_printf ("%s:%d: large block decode_rs returned status = %d\n", __FILE__, __LINE__, e);

	    if (e < 0) failed = 1;
	    *symbols_corrected += e;

	    il2p_descramble_block (corrected_block, pout, ipp.large_block_size);

	    if (il2p_get_debug() >= 2) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("Descrambled large payload block, %d bytes:\n", ipp.large_block_size);
	        fx_hex_dump(pout, ipp.large_block_size);
	    }

	    pin += ipp.large_block_size + ipp.parity_symbols_per_block;
	    pout += ipp.large_block_size;
	    decoded_length += ipp.large_block_size;
	}

// Then the small blocks.

	for (int b = 0; b < ipp.small_block_count; b++) {
	    unsigned char corrected_block[255];
	    int e = il2p_decode_rs (pin, ipp.small_block_size, ipp.parity_symbols_per_block, corrected_block);

	    // dw_printf ("%s:%d: small block decode_rs returned status = %d\n", __FILE__, __LINE__, e);

	    if (e < 0) failed = 1;
	    *symbols_corrected += e;

	    il2p_descramble_block (corrected_block, pout, ipp.small_block_size);

	    if (il2p_get_debug() >= 2) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("Descrambled small payload block, %d bytes:\n", ipp.small_block_size);
	        fx_hex_dump(pout, ipp.small_block_size);
	    }

	    pin += ipp.small_block_size + ipp.parity_symbols_per_block;
	    pout += ipp.small_block_size;
	    decoded_length += ipp.small_block_size;
	}

	if (failed) {
	    //dw_printf ("%s:%d: failed = %0x\n", __FILE__, __LINE__, failed);
	    return (-2);
	}

	if (decoded_length != payload_size) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("IL2P Internal error: decoded_length = %d, payload_size = %d\n", decoded_length, payload_size);
	    return (-3);
	}

	return (decoded_length);

} // end il2p_decode_payload

// end il2p_payload.c

