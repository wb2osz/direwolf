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
#include <assert.h>
#include <string.h>

#include "il2p.h"
#include "textcolor.h"
#include "demod.h"

#include "fcs_calc.h"
#include "audio.h"

/*-------------------------------------------------------------
 *
 * File:	il2p_codec.c
 *
 * Purpose:	Convert IL2P encoded format from and to direwolf internal packet format.
 *
 *--------------------------------------------------------------*/


static int il2p_check_crc(unsigned char *payload, int payload_length, unsigned char *coded_crc);
static int il2p_generate_crc(unsigned char *coded_crc, unsigned char *payload, int payload_length);

#define IL2P_NIBLE_MASK 0x0f
#define IL2P_DECODE_MASK 0x7f

// local variables

static uint8_t encode_table[16] = { 0x0, 0x71, 0x62, 0x13, 0x54, 0x25, 0x36, 0x47, 0x38, 0x49, 0x5a, 0x2b, 0x6c, 0x1d, 0x0e, 0x7f };

static uint8_t decode_table[128] = { 0x0, 0x0, 0x0, 0x3, 0x0, 0x5, 0xe, 0x7,
                                        0x0, 0x9, 0xe, 0xb, 0xe, 0xd, 0xe, 0xe,
                                        0x0, 0x3, 0x3, 0x3, 0x4, 0xd, 0x6, 0x3,
                                        0x8, 0xd, 0xa, 0x3, 0xd, 0xd, 0xe, 0xd,
                                        0x0, 0x5, 0x2, 0xb, 0x5, 0x5, 0x6, 0x5,
                                        0x8, 0xb, 0xb, 0xb, 0xc, 0x5, 0xe, 0xb,
                                        0x8, 0x1, 0x6, 0x3, 0x6, 0x5, 0x6, 0x6,
                                        0x8, 0x8, 0x8, 0xb, 0x8, 0xd, 0x6, 0xf,
                                        0x0, 0x9, 0x2, 0x7, 0x4, 0x7, 0x7, 0x7,
                                        0x9, 0x9, 0xa, 0x9, 0xc, 0x9, 0xe, 0x7,
                                        0x4, 0x1, 0xa, 0x3, 0x4, 0x4, 0x4, 0x7,
                                        0xa, 0x9, 0xa, 0xa, 0x4, 0xd, 0xa, 0xf,
                                        0x2, 0x1, 0x2, 0x2, 0xc, 0x5, 0x2, 0x7,
                                        0xc, 0x9, 0x2, 0xb, 0xc, 0xc, 0xc, 0xf,
                                        0x1, 0x1, 0x2, 0x1, 0x4, 0x1, 0x6, 0xf,
                                        0x8, 0x1, 0xa, 0xf, 0xc, 0xf, 0xf, 0xf };


/*-------------------------------------------------------------
 *
 * Name:	il2p_encode_frame
 *
 * Purpose:	Convert AX.25 frame to IL2P encoding.
 *
 * Inputs:	chan	- Audio channel number, 0 = first.
 *
 *		pp	- Packet object pointer.
 *
 *		max_fec	- 1 to send maximum FEC size rather than automatic.
 *
 * 		use_crc - 1 to use CRC for the payload.
 *
 * Outputs:	iout	- Encoded result, excluding the 3 byte sync word.
 *			  Caller should provide  IL2P_MAX_PACKET_SIZE  bytes.
 *
 * Returns:	Number of bytes for transmission.
 *		-1 is returned for failure.
 *
 * Description:	Encode into IL2P format.
 *
 * Errors:	If something goes wrong, return -1.
 *
 *		Most likely reason is that the frame is too large.
 *		IL2P has a max payload size of 1023 bytes.
 *		For a type 1 header, this is the maximum AX.25 Information part size.
 *		For a type 0 header, this is the entire AX.25 frame.
 *
 *--------------------------------------------------------------*/

int il2p_encode_frame (packet_t pp, int max_fec, unsigned char *iout)
{

// Can a type 1 header be used?

	unsigned char hdr[IL2P_HEADER_SIZE + IL2P_HEADER_PARITY];
	int e;
	int out_len = 0;
	unsigned char crc[IL2P_CODED_CRC_LENGTH];
	int crc_len;
	unsigned char *frame_data;
	int frame_len;


	frame_len = ax25_get_frame_len(pp);
	frame_data = ax25_get_frame_data_ptr(pp);
    crc_len = il2p_generate_crc(crc, frame_data, frame_len);

	e = il2p_type_1_header (pp, max_fec, hdr);
	if (e >= 0) {
	    il2p_scramble_block (hdr, iout, IL2P_HEADER_SIZE);
	    il2p_encode_rs (iout, IL2P_HEADER_SIZE, IL2P_HEADER_PARITY, iout+IL2P_HEADER_SIZE);
	    out_len = IL2P_HEADER_SIZE + IL2P_HEADER_PARITY;

	    if (e == 0) {
	        // Success. No info part.
            memcpy(iout + out_len, crc, crc_len);
			out_len += crc_len;
	        return (out_len);
	    }

	    // Payload is AX.25 info part.
	    unsigned char *pinfo;
	    int info_len;
	    info_len = ax25_get_info (pp, &pinfo);

	    int k = il2p_encode_payload (pinfo, info_len, max_fec, iout+out_len);
	    if (k > 0) {
	        out_len += k;
	        // Success. Info part was <= 1023 bytes.
			memcpy(iout + out_len, crc, crc_len);
			out_len += crc_len;
	        return (out_len);
	    }

	    // Something went wrong with the payload encoding.
	    return (-1);
	}
	else if (e == -1) {

// Could not use type 1 header for some reason.
// e.g. More than 2 addresses, extended (mod 128) sequence numbers, etc.

	    e = il2p_type_0_header (pp, max_fec, hdr);
	    if (e > 0) {

	        il2p_scramble_block (hdr, iout, IL2P_HEADER_SIZE);
	        il2p_encode_rs (iout, IL2P_HEADER_SIZE, IL2P_HEADER_PARITY, iout+IL2P_HEADER_SIZE);
	        out_len = IL2P_HEADER_SIZE + IL2P_HEADER_PARITY;

	        // Payload is entire AX.25 frame.
	        unsigned char *frame_data_ptr = ax25_get_frame_data_ptr (pp);
	        int frame_len = ax25_get_frame_len (pp);
	        int k = il2p_encode_payload (frame_data_ptr, frame_len, max_fec, iout+out_len);
	        if (k > 0) {
	            out_len += k;
	            // Success. Entire AX.25 frame <= 1023 bytes.
                crc_len = il2p_generate_crc(crc, frame_data_ptr, frame_len);
				memcpy(iout + out_len, crc, crc_len);
				out_len += crc_len;
	            return (out_len);
	        }
	        // Something went wrong with the payload encoding.
	        return (-1);
	    }
	    else if (e == 0) {
	        // Impossible condition.  Type 0 header must have payload.
	        return (-1);
	    }
	    else {
	        // AX.25 frame is too large.
	        return (-1);
	    }
	}

	// AX.25 Information part is too large.
	return (-1);
}



/*-------------------------------------------------------------
 *
 * Name:	il2p_decode_frame
 *
 * Purpose:	Convert IL2P encoding to AX.25 frame.
 *		This is only used during testing, with a whole encoded frame.
 *		During reception, the header would have FEC and descrambling
 *		applied first so we would know how much to collect for the payload.
 *
 * Inputs:	irec	- Received IL2P frame excluding the 3 byte sync word.
 *          use_crc - 1 to chec CRC for the payload.
 *
 * Future Out:	Number of symbols corrected.
 *
 * Returns:	Packet pointer or NULL for error.
 *
 *--------------------------------------------------------------*/

packet_t il2p_decode_frame (unsigned char *irec, int use_crc)
{
	unsigned char uhdr[IL2P_HEADER_SIZE];		// After FEC and descrambling.
	int e = il2p_clarify_header (irec, uhdr);

	// TODO?: for symmetry we might want to clarify the payload before combining.

	return (il2p_decode_header_payload(uhdr, irec + IL2P_HEADER_SIZE + IL2P_HEADER_PARITY, &e, use_crc));
}


/*-------------------------------------------------------------
 *
 * Name:	il2p_decode_header_payload
 *
 * Purpose:	Convert IL2P encoding to AX.25 frame
 *
 * Inputs:	uhdr 		- Received header after FEC and descrambling.
 *		epayload	- Encoded payload.
 *		use_crc	- 1 to check CRC for the payload.
 *
 * In/Out:	symbols_corrected - Symbols (bytes) corrected in the header.
 *				  Should be 0 or 1 because it has 2 parity symbols.
 *				  Here we add number of corrections for the payload.
 *
 * Returns:	Packet pointer or NULL for error.
 *
 *--------------------------------------------------------------*/

packet_t il2p_decode_header_payload (unsigned char* uhdr, unsigned char *epayload, int *symbols_corrected, int use_crc)
{
	int hdr_type;
	int max_fec;
	int payload_len = il2p_get_header_attributes (uhdr, &hdr_type, &max_fec);
    unsigned char *crc_hdr = epayload; // In case ther's no payload, this is where the CRC is stored.
    int ret;
	int frame_len;
	unsigned char *frame_data;

	packet_t pp = NULL;

	if (hdr_type == 1) {

// Header type 1.  Any payload is the AX.25 Information part.

	    pp = il2p_decode_header_type_1 (uhdr, *symbols_corrected);
	    if (pp == NULL) {
	        // Failed for some reason.
	        return (NULL);
	    }

	    if (payload_len > 0) {
	        // This is the AX.25 Information part.

	        unsigned char extracted[IL2P_MAX_PAYLOAD_SIZE];
		      int e = il2p_decode_payload (epayload, payload_len, max_fec, extracted, symbols_corrected, &crc_hdr);

		// It would be possible to have a good header but too many errors in the payload.

	        if (e <= 0) {
	            ax25_delete (pp);
	            pp = NULL;
	            return (pp);
	        }
		      if (e != payload_len) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("IL2P Internal Error: %s(): hdr_type=%d, max_fec=%d, payload_len=%d, e=%d.\n", __func__, hdr_type, max_fec, payload_len, e);
	        }
	        ax25_set_info (pp, extracted, payload_len);
	    }
	    // Check CRC if requested.
		if (use_crc == IL2P_USECRC) {
			frame_len = ax25_get_frame_len(pp);
			frame_data = ax25_get_frame_data_ptr(pp);
			ret = il2p_check_crc(frame_data, frame_len, crc_hdr);
			if (ret < 0) {
	            ax25_delete (pp);
	            pp = NULL;
	    	}
		}
	    return (pp);
	}
	else {

// Header type 0.  The payload is the entire AX.25 frame.

	    unsigned char extracted[IL2P_MAX_PAYLOAD_SIZE];
	    int e = il2p_decode_payload (epayload, payload_len, max_fec, extracted, symbols_corrected, &crc_hdr);

	    if (e <= 0) {	// Payload was not received correctly.
	        return (NULL);
	    }
	    if (e != payload_len) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("IL2P Internal Error: %s(): hdr_type=%d, e=%d, payload_len=%d\n", __func__, hdr_type, e, payload_len);
	        return (NULL);
	    }

	    alevel_t alevel;
	    memset (&alevel, 0, sizeof(alevel));
	    //alevel = demod_get_audio_level (chan, subchan); 	// What TODO? We don't know channel here.
						// I think alevel gets filled in somewhere later making
						// this redundant.

	    pp = ax25_from_frame (extracted, payload_len, alevel);
		if (use_crc) {
			frame_len = payload_len;
			frame_data = extracted;
			ret = il2p_check_crc(frame_data, frame_len, crc_hdr);
			if (ret < 0) {
    	        ax25_delete (pp);
	            pp = NULL;
	        }
		}
	    return (pp);
	}

} // end il2p_decode_header_payload

/*-------------------------------------------------------------
 *
 * Name:	il2p_generate_crc
 *
 * Purpose:	Generate IL2P encoded CRC
 *
 * Inputs:	payload 		- Decoded payload.
 *      		payload_length	- payload length.
 *
 * In/Out:	coded_crc - Coded CRC
 *
 * Returns:	The length of the CRC, which is 4 bytes.
 *
 *--------------------------------------------------------------*/

static int il2p_generate_crc(unsigned char *coded_crc, unsigned char *payload, int payload_length)
{
	int out_len = 0;
	// Calculate CRC-16 for the payload.
	unsigned short crc = fcs_calc (payload, payload_length);
	uint8_t crc_nibles[IL2P_CODED_CRC_LENGTH];

	if (il2p_get_debug() >= 1) {
        dw_printf ("IL2P TX CRC data:\n");
        fx_hex_dump (payload, payload_length);
		text_color_set (DW_COLOR_DEBUG);
		dw_printf ("IL2P TX CRC: %04x.\n", crc);
	}

	crc_nibles[0] = crc & IL2P_NIBLE_MASK; // low nibble
	crc_nibles[1] = (crc >>  4) & IL2P_NIBLE_MASK; // next nibble
	crc_nibles[2] = (crc >>  8) & IL2P_NIBLE_MASK; // next nibble
	crc_nibles[3] = (crc >> 12) & IL2P_NIBLE_MASK; // high nibble

	for (int i = 0; i < IL2P_CODED_CRC_LENGTH; i++) {
		*(coded_crc + i) = encode_table[crc_nibles[i]];
		out_len++;
	}

	return out_len;
} // end il2p_generate_crc


/*-------------------------------------------------------------
 *
 * Name:	il2p_check_crc
 *
 * Purpose:	Check IL2P CRC against decoded payload.
 *
 * Inputs:	payload 		- Decoded payload.
 *      		payload_length	- payload length.
 *
 * In/Out:	coded_crc - pointer to the coded CRC
 *          The length is assumed to be IL2P_CODED_CRC_LENGTH
 *
 * Returns:	The result of the check.
 *           0 for success, -1 for failure.
 *
 *--------------------------------------------------------------*/

static int il2p_check_crc(unsigned char *payload, int payload_length, unsigned char *coded_crc)
{

  int ret;
  ret = 0;

	uint8_t crc_nibles[IL2P_CODED_CRC_LENGTH];
	// Check the CRC-16 for the payload.
	unsigned short crc = fcs_calc (payload, payload_length);

    if (il2p_get_debug() >= 1) {
        dw_printf ("IL2P RX CRC data:\n");
        fx_hex_dump (payload, payload_length);
		text_color_set (DW_COLOR_DEBUG);
		dw_printf ("IL2P RX CRC: %04x.\n", crc);
	}

	for (int i = 0; i < IL2P_CODED_CRC_LENGTH; i++) {
			uint8_t coded_crc_byte = IL2P_DECODE_MASK & *(coded_crc+i);
			crc_nibles[i] = decode_table[coded_crc_byte];
	}

	unsigned short decoded_crc = (crc_nibles[0] & IL2P_NIBLE_MASK) |
																((crc_nibles[1] & IL2P_NIBLE_MASK)<< 4) |
																((crc_nibles[2] & IL2P_NIBLE_MASK)<< 8) |
																((crc_nibles[3] & IL2P_NIBLE_MASK)<< 12);
	if (decoded_crc != crc) {
    if (il2p_get_debug() >= 1) {
      text_color_set (DW_COLOR_ERROR);
      dw_printf ("IL2P RX CRC error: expected %04x, got %04x.\n", crc, decoded_crc);
    }
    ret = -1;
	} else if (il2p_get_debug() >= 1) {
		text_color_set (DW_COLOR_DEBUG);
		dw_printf ("IL2P RX CRC OK: %04x.\n", decoded_crc);
	}

  return ret;
} // end il2p_check_crc

// end il2p_codec.c
