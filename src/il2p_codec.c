//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2021, 2026  John Langner, WB2OSZ
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


/*-------------------------------------------------------------
 *
 * File:	il2p_codec.c
 *
 * Purpose:	Convert IL2P encoded format from and to direwolf internal packet format.
 *
 *--------------------------------------------------------------*/


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
 *			  0 for weak FEC was removed from IL2P spec version 0.6.
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

// New in IL2P 0.6 - Compute FCS of the original frame.
// Same technique as used in AX.25 HDLC frame.
// Encode it into 4 bytes using Hamming code so minor bit corruption is tolerated.

// Without the new CRC at the end, and minimum FEC, we saw results like this:
//	WB2OSZ-15>TEST:,The quick brown fox jumps over the lazy dog!  0063 of 0100
//	WB2OSZ-15>TEST:,The quick brown fox jumps over the lazy dog!  0064 of 0100
//	WB2OSZ-15>TEST:,The quick brown fox jumps over the lazy dog!  0071 of 0100
//	WB2OSZ-15>TEST:,Thg<0x01>quick1jrogváfox!z⌡mps over the laxX dog!  0074 of 0100
//	WB2OSZ-15>TEST:,The u7ick brown╬<0x16>ox ju)Pwbover the lazy dMw!  0076 of ╕q00
//	WB2OSZ-15>TEST:,The qwHck brown fox jumps o0dr the lazy dog!  0077 +F 0100

	unsigned char ham_crc[IL2P_CRC_SIZE];
	il2p_create_crc (pp, ham_crc);

	// text_color_set(DW_COLOR_DEBUG);
	// dw_printf ("--------\n");
	// dw_printf ("IL2P Hamming = %02x %02x %02x %02x \n", ham_crc[0], ham_crc[1], ham_crc[2], ham_crc[3]);
	// dw_printf ("--------\n");

// Can a type 1 header be used?

	unsigned char hdr[IL2P_HEADER_SIZE + IL2P_HEADER_PARITY];
	int e;
	int out_len = 0;

	e = il2p_type_1_header (pp, max_fec, hdr);
	if (e >= 0) {
	    il2p_scramble_block (hdr, iout, IL2P_HEADER_SIZE);
	    il2p_encode_rs (iout, IL2P_HEADER_SIZE, IL2P_HEADER_PARITY, iout+IL2P_HEADER_SIZE);
	    out_len = IL2P_HEADER_SIZE + IL2P_HEADER_PARITY;

	    if (e == 0) {
	        // Success. No info part.
	        memcpy (iout+out_len, ham_crc, sizeof(ham_crc));
	        out_len += sizeof(ham_crc);
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
	        memcpy (iout+out_len, ham_crc, sizeof(ham_crc));
	        out_len += sizeof(ham_crc);
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
	            memcpy (iout+out_len, ham_crc, sizeof(ham_crc));
	            out_len += sizeof(ham_crc);
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
 *		This is only used from il2p_test.c, with a whole encoded frame.
 *		During reception, the header would have FEC and descrambling
 *		applied first so we would know how much to collect for the payload.
 *
 * Inputs:	irec	- Received IL2P frame excluding the 3 byte sync word.
 *		rec_len	- Length for above.
 *
 * Future Out:	Number of symbols corrected.
 *
 * Returns:	Packet pointer or NULL for error.
 *
 *--------------------------------------------------------------*/

packet_t il2p_decode_frame (unsigned char *irec, int rec_len)
{
	unsigned char uhdr[IL2P_HEADER_SIZE];		// After FEC and descrambling.
	int e = il2p_clarify_header (irec, uhdr);

	unsigned char crc[IL2P_CRC_SIZE];
	memcpy (crc, irec + rec_len - IL2P_CRC_SIZE, IL2P_CRC_SIZE);

	// This function is used only from il2p_test.c for automated unit tests.
	// The CRC should always be checked in this situation.
	int check_crc = 1;
	return (il2p_decode_header_payload(uhdr, irec + IL2P_HEADER_SIZE + IL2P_HEADER_PARITY, crc, check_crc, &e));
}


/*-------------------------------------------------------------
 *
 * Name:	il2p_decode_header_payload
 *
 * Purpose:	Convert IL2P encoding to AX.25 frame
 *
 * Inputs:	uhdr 		- Received header after FEC and descrambling.
 *		epayload	- Encoded payload.
 *		crc		- 4 byte Hamming encoded CRC.
 *		check_crc	- Should CRC be checked (1) or ignored (0)?
 *
 * In/Out:	symbols_corrected - Symbols (bytes) corrected in the header.
 *				  Should be 0 or 1 because it has 2 parity symbols.
 *				  Here we add number of corrections for the payload.
 *
 * Returns:	Packet pointer or NULL for error.
 *
 *--------------------------------------------------------------*/

packet_t il2p_decode_header_payload (unsigned char* uhdr, unsigned char *epayload, unsigned char *crc, int check_crc, int *symbols_corrected)
{
	int e;
	int hdr_type;
	int max_fec;
	int payload_len = il2p_get_header_attributes (uhdr, &hdr_type, &max_fec);

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
		e = il2p_decode_payload (epayload, payload_len, max_fec, extracted, symbols_corrected);

		// It would be possible to have a good header but too many errors in the payload.

	        if (e <= 0) {
	            ax25_delete (pp);
	            return (NULL);
	        }
	        
		if (e != payload_len) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("IL2P Internal Error: %s(): hdr_type=%d, max_fec=%d, payload_len=%d, e=%d.\n", __func__, hdr_type, max_fec, payload_len, e);
	        }

	        ax25_set_info (pp, extracted, payload_len);
	    }
	}
	else {

// Header type 0.  The payload is the entire encapsulated AX.25 frame.

	    unsigned char extracted[IL2P_MAX_PAYLOAD_SIZE];
	    e = il2p_decode_payload (epayload, payload_len, max_fec, extracted, symbols_corrected);

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
	    if (pp == NULL) {
	        // Failed for some reason.
	        return (NULL);
	    }
	}

// Should we require a correct Hamming CRC?  (Config file IL2PRXCRC option, default 1)
// "atest" does not have an option to ignore.
// Always check when run from il2p_test.c.

	if (check_crc) {
	    e = il2p_check_crc (pp, crc);
	    if (e < 0) {
	        ax25_delete (pp);
	        return (NULL);
	    }
	}
	return (pp);

} // end il2p_decode_header_payload


/*-------------------------------------------------------------
 *
 * Name:	il2p_create_crc
 *
 * Purpose:	Create 4 byte hamming encoded CRC for given AX.25 frame.
 *
 * Inputs:	pp 		- Packet object pointer.
 *
 * Outputs:	crc		- 4 byte Hamming encoded CRC.
 *
 * Returns:	None
 *
 *--------------------------------------------------------------*/

static const unsigned char ham_it_up[16] = {
		0x0, 0x71, 0x62, 0x13, 0x54, 0x25, 0x36, 0x47,
		0x38, 0x49, 0x5a, 0x2b, 0x6c, 0x1d, 0x0e, 0x7f };

void il2p_create_crc (packet_t pp, unsigned char *crc)
{
	assert (pp != NULL);
	// ax25_hex_dump (pp);
	unsigned char *fbuf = ax25_get_frame_data_ptr(pp);
	int flen = ax25_get_frame_len (pp);
	unsigned int fcs = fcs_calc (fbuf, flen);

	crc[0] = ham_it_up[(fcs >> 12) & 0xf];	// CRC3 in spec, transmitted first.
	crc[1] = ham_it_up[(fcs >>  8) & 0xf];
	crc[2] = ham_it_up[(fcs >>  4) & 0xf];
	crc[3] = ham_it_up[(fcs      ) & 0xf];

	// dw_printf ("%s:%d: originalfcs=%04x  hamcrc=%02x %02x %02x %02x\n", __FILE__, __LINE__,  fcs, crc[0], crc[1], crc[2], crc[3]);

} // end il2p_create_crc


/*-------------------------------------------------------------
 *
 * Name:	il2p_check_crc
 *
 * Purpose:	Convert 4 byte hamming encoded CRC to 16 bit AX.15 frame FCS.
 *		Compare extracted 16 bit FCS with what is expected for given AX.25 frame.
 *
 * Inputs:	pp 		- Packet object pointer.
 *		crc		- 4 byte Hamming encoded CRC received over radio.
 *
 * Returns:	-1 for no match of 16 bit FCS values.
 *		0  for match and no errors in received hamming encoded CRC.
 *		>0 for match and number of bits that were wrong in the hamming encoded CRC.
 *
 *--------------------------------------------------------------*/

int il2p_check_crc (packet_t pp, unsigned char *crc)
{
	// Convert received 7 bits to closest original 4.
	// This allows single bit correction and detection.
	// If two bits are changed, error is undetected and some other 4 bits come out.

	static const unsigned char sliced_ham[128] = {
		0x0, 0x0, 0x0, 0x3, 0x0, 0x5, 0xe, 0x7,
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

	assert (pp != NULL);
	// ax25_hex_dump (pp);
	unsigned char *fbuf = ax25_get_frame_data_ptr(pp);
	int flen = ax25_get_frame_len (pp);
	unsigned int expected_fcs = fcs_calc (fbuf, flen);

	int recv_fcs = 0;
	int bad_bits = 0;
	for (int j = 0; j < IL2P_CRC_SIZE; j++) {
	  if (crc[j] & 0x80) bad_bits++;
	  int nybble = sliced_ham[crc[j] & 0x7f];
	  if (ham_it_up[nybble] != (crc[j] & 0x7f)) bad_bits++;
	  recv_fcs = (recv_fcs << 4) | nybble;
	  // dw_printf ("%s:%d: j=%d crcj=%02x nybble=%x recv_fcs=%04x\n", __FILE__, __LINE__,  j, crc[j], nybble, recv_fcs);
	}

	// dw_printf ("%s:%d: recv_fcs %04x  expected_fcs %04x\n", __FILE__, __LINE__,  recv_fcs, expected_fcs);
	return ((recv_fcs == expected_fcs) ? bad_bits : -1);

} // end il2p_check_crc

// end il2p_codec.c


