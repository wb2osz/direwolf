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
#include "ax25_pad.h"
#include "ax25_pad2.h"
#include "multi_modem.h"


static void test_scramble(void);
static void test_rs(void);
static void test_payload(void);
static void test_example_headers(void);
static void all_frame_types(void);
static void test_serdes(void);
static void decode_bitstream(void);

/*-------------------------------------------------------------
 *
 * Name:	il2p_test.c
 *
 * Purpose:	Unit tests for IL2P protocol functions.
 *
 * Errors:	Die if anything goes wrong.
 *
 *--------------------------------------------------------------*/


int main ()
{
	int enable_color = 1;
	text_color_init (enable_color);

	int enable_debug_out = 0;
	il2p_init(enable_debug_out);

	text_color_set(DW_COLOR_INFO);
	dw_printf ("Begin IL2P unit tests.\n");

// These start simple and later complex cases build upon earlier successes.

// Test scramble and descramble.

	test_scramble();

// Test Reed Solomon error correction.

	test_rs();

// Test payload functions.

	test_payload();

// Try encoding the example headers in the protocol spec.

	test_example_headers();

// Convert all of the AX.25 frame types to IL2P and back again.

	all_frame_types();

// Use same serialize / deserialize functions used on the air.

	test_serdes ();

// Decode bitstream from demodulator if data file is available.
// TODO:  Very large info parts.  Appropriate error if too long.
// TODO:  More than 2 addresses.

	decode_bitstream();

	text_color_set(DW_COLOR_REC);
	dw_printf ("\n----------\n\n");
	dw_printf ("\nSUCCESS!\n");

	return (EXIT_SUCCESS);
}



/////////////////////////////////////////////////////////////////////////////////////////////
//
//	Test scrambling and descrambling.
//
/////////////////////////////////////////////////////////////////////////////////////////////

static void test_scramble(void)
{
	text_color_set(DW_COLOR_INFO);
	dw_printf ("Test scrambling...\n");

// First an example from the protocol specification to make sure I'm compatible.

	static unsigned char scramin1[] = { 0x63, 0xf1, 0x40, 0x40, 0x40, 0x00, 0x6b, 0x2b, 0x54, 0x28, 0x25, 0x2a, 0x0f };
	static unsigned char scramout1[] = { 0x6a, 0xea, 0x9c, 0xc2, 0x01, 0x11, 0xfc, 0x14, 0x1f, 0xda, 0x6e, 0xf2, 0x53 };
	unsigned char scramout[sizeof(scramin1)];

	il2p_scramble_block (scramin1, scramout, sizeof(scramin1));
	assert (memcmp(scramout, scramout, sizeof(scramout1)) == 0);

}  // end test_scramble.




/////////////////////////////////////////////////////////////////////////////////////////////
//
//	Test Reed Solomon encode/decode examples found in the protocol spec.
//	The data part is scrambled but that does not matter here because.
//	We are only concerned abound adding the parity and verifying.
//
/////////////////////////////////////////////////////////////////////////////////////////////


static void test_rs()
{
	text_color_set(DW_COLOR_INFO);
	dw_printf ("Test Reed Solomon functions...\n");

	static unsigned char example_s[] = { 0x26, 0x57, 0x4d, 0x57, 0xf1, 0x96, 0xcc, 0x85, 0x42, 0xe7, 0x24, 0xf7, 0x2e,
						0x8a, 0x97 };
	unsigned char parity_out[2];
	il2p_encode_rs (example_s, 13, 2, parity_out);
	//dw_printf ("DEBUG RS encode %02x %02x\n", parity_out[0], parity_out[1]);
	assert (memcmp(parity_out, example_s + 13, 2) == 0);


	static unsigned char example_u[] = { 0x6a, 0xea, 0x9c, 0xc2, 0x01, 0x11, 0xfc, 0x14, 0x1f, 0xda, 0x6e, 0xf2, 0x53,
						 0x91, 0xbd };
	il2p_encode_rs (example_u, 13, 2, parity_out);
	//dw_printf ("DEBUG RS encode %02x %02x\n", parity_out[0], parity_out[1]);
	assert (memcmp(parity_out, example_u + 13, 2) == 0);

	// See if we can go the other way.

	unsigned char received[15];
	unsigned char corrected[15];
	int e;

	e = il2p_decode_rs (example_s, 13, 2, corrected);
	assert (e == 0);
	assert (memcmp(example_s, corrected, 13) == 0);

	memcpy (received, example_s, 15);
	received[0] = '?';
	e = il2p_decode_rs (received, 13, 2, corrected);
	assert (e == 1);
	assert (memcmp(example_s, corrected, 13) == 0);

	e = il2p_decode_rs (example_u, 13, 2, corrected);
	assert (e == 0);
	assert (memcmp(example_u, corrected, 13) == 0);

	memcpy (received, example_u, 15);
	received[12] = '?';
	e = il2p_decode_rs (received, 13, 2, corrected);
	assert (e == 1);
	assert (memcmp(example_u, corrected, 13) == 0);

	received[1] = '?';
	received[2] = '?';
	e = il2p_decode_rs (received, 13, 2, corrected);
	assert (e == -1);
}



/////////////////////////////////////////////////////////////////////////////////////////////
//
//	Test payload functions.
//
/////////////////////////////////////////////////////////////////////////////////////////////

static void test_payload(void) 
{
	text_color_set(DW_COLOR_INFO);
	dw_printf ("Test payload functions...\n");

	il2p_payload_properties_t ipp;
	int e;

// Examples in specification.

	e = il2p_payload_compute (&ipp, 100, 0);
	assert (ipp.small_block_size == 100);
	assert (ipp.large_block_size == 101);
	assert (ipp.large_block_count == 0);
	assert (ipp.small_block_count == 1);
	assert (ipp.parity_symbols_per_block == 4);
	
	e = il2p_payload_compute (&ipp, 236, 0);
	assert (ipp.small_block_size == 236);
	assert (ipp.large_block_size == 237);
	assert (ipp.large_block_count == 0);
	assert (ipp.small_block_count == 1);
	assert (ipp.parity_symbols_per_block == 8);
	
	e = il2p_payload_compute (&ipp, 512, 0);
	assert (ipp.small_block_size == 170);
	assert (ipp.large_block_size == 171);
	assert (ipp.large_block_count == 2);
	assert (ipp.small_block_count == 1);
	assert (ipp.parity_symbols_per_block == 6);
	
	e = il2p_payload_compute (&ipp, 1023, 0);
	assert (ipp.small_block_size == 204);
	assert (ipp.large_block_size == 205);
	assert (ipp.large_block_count == 3);
	assert (ipp.small_block_count == 2);
	assert (ipp.parity_symbols_per_block == 8);

// Now try all possible sizes for Baseline FEC Parity.

	for (int n = 1; n <= IL2P_MAX_PAYLOAD_SIZE; n++) {
	    e = il2p_payload_compute (&ipp, n, 0);
	    //dw_printf ("bytecount=%d, smallsize=%d, largesize=%d, largecount=%d, smallcount=%d\n", n,
	    //		ipp.small_block_size, ipp.large_block_size,
	    //		ipp.large_block_count, ipp.small_block_count);
	    //fflush (stdout);

	    assert (ipp.payload_block_count >= 1 && ipp.payload_block_count <= IL2P_MAX_PAYLOAD_BLOCKS);
	    assert (ipp.payload_block_count == ipp.small_block_count + ipp.large_block_count);
	    assert (ipp.small_block_count * ipp.small_block_size + 
			ipp.large_block_count * ipp.large_block_size == n);
	    assert (ipp.parity_symbols_per_block == 2 ||
			ipp.parity_symbols_per_block == 4 ||
			ipp.parity_symbols_per_block == 6 ||
			ipp.parity_symbols_per_block == 8);

	    // Data and parity must fit in RS block size of 255.
	    // Size test does not apply if block count is 0.
	    assert (ipp.small_block_count == 0 || ipp.small_block_size + ipp.parity_symbols_per_block <= 255);
	    assert (ipp.large_block_count == 0 || ipp.large_block_size + ipp.parity_symbols_per_block <= 255);
	}

// All sizes for MAX FEC.

	for (int n = 1; n <= IL2P_MAX_PAYLOAD_SIZE; n++) {
	    e = il2p_payload_compute (&ipp, n, 1);	// 1 for max fec.
	    //dw_printf ("bytecount=%d, smallsize=%d, largesize=%d, largecount=%d, smallcount=%d\n", n,
	    //		ipp.small_block_size, ipp.large_block_size,
	    //		ipp.large_block_count, ipp.small_block_count);
	    //fflush (stdout);

	    assert (ipp.payload_block_count >= 1 && ipp.payload_block_count <= IL2P_MAX_PAYLOAD_BLOCKS);
	    assert (ipp.payload_block_count == ipp.small_block_count + ipp.large_block_count);
	    assert (ipp.small_block_count * ipp.small_block_size + 
			ipp.large_block_count * ipp.large_block_size == n);
	    assert (ipp.parity_symbols_per_block == 16);

	    // Data and parity must fit in RS block size of 255.
	    // Size test does not apply if block count is 0.
	    assert (ipp.small_block_count == 0 || ipp.small_block_size + ipp.parity_symbols_per_block <= 255);
	    assert (ipp.large_block_count == 0 || ipp.large_block_size + ipp.parity_symbols_per_block <= 255);
	}

// Now let's try encoding payloads and extracting original again.
// This will also provide exercise for scrambling and Reed Solomon under more conditions.

	unsigned char original_payload[IL2P_MAX_PAYLOAD_SIZE];
	for (int n = 0; n < IL2P_MAX_PAYLOAD_SIZE; n++) {
	    original_payload[n] = n & 0xff;
	}
	for (int max_fec = 0; max_fec <= 1; max_fec++) {
	    for (int payload_length = 1; payload_length <= IL2P_MAX_PAYLOAD_SIZE; payload_length++) {
	        //dw_printf ("\n--------- max_fec = %d, payload_length = %d\n", max_fec, payload_length);
	        unsigned char encoded[IL2P_MAX_ENCODED_PAYLOAD_SIZE];
	        int k = il2p_encode_payload (original_payload, payload_length, max_fec, encoded);

	        //dw_printf ("payload length %d %s -> %d\n", payload_length, max_fec ? "M" : "", k);
	        assert (k > payload_length && k <= IL2P_MAX_ENCODED_PAYLOAD_SIZE);

	        // Now extract.

	        unsigned char extracted[IL2P_MAX_PAYLOAD_SIZE];
	        int symbols_corrected = 0;
		int e = il2p_decode_payload (encoded, payload_length, max_fec, extracted, &symbols_corrected);
	        //dw_printf ("e = %d, payload_length = %d\n", e, payload_length);
		assert (e == payload_length);

	        // if (memcmp (original_payload, extracted, payload_length) != 0) {
	        //  dw_printf ("********** Received message not as expected. **********\n");
	        //  fx_hex_dump(extracted, payload_length);
	        // }
	        assert (memcmp (original_payload, extracted, payload_length) == 0);
	    }
	}
	(void)e;
} // end test_payload



/////////////////////////////////////////////////////////////////////////////////////////////
//
//	Test header examples found in protocol specification.
//
/////////////////////////////////////////////////////////////////////////////////////////////

static void test_example_headers()
{

//----------- Example 1:  AX.25 S-Frame   --------------
 
//	This frame sample only includes a 15 byte header, without PID field.
//	Destination Callsign: ?KA2DEW-2
//	Source Callsign: ?KK4HEJ-7
//	N(R): 5
//	P/F: 1
//	C: 1
//	Control Opcode: 00 (Receive Ready)
//	
//	AX.25 data:
//	96 82 64 88 8a ae e4 96 96 68 90 8a 94 6f b1
//	
//	IL2P Data Prior to Scrambling and RS Encoding:
//	2b a1 12 24 25 77 6b 2b 54 68 25 2a 27
//	
//	IL2P Data After Scrambling and RS Encoding:
//	26 57 4d 57 f1 96 cc 85 42 e7 24 f7 2e 8a 97

	text_color_set(DW_COLOR_INFO);
	dw_printf ("Example 1: AX.25 S-Frame...\n");

	static unsigned char example1[] = {0x96, 0x82, 0x64, 0x88, 0x8a, 0xae, 0xe4, 0x96, 0x96, 0x68, 0x90, 0x8a, 0x94, 0x6f, 0xb1};
	static unsigned char header1[]  = {0x2b, 0xa1, 0x12, 0x24, 0x25, 0x77, 0x6b, 0x2b, 0x54, 0x68, 0x25, 0x2a, 0x27 };
	unsigned char header[IL2P_HEADER_SIZE];
	unsigned char sresult[32];
	memset (header, 0, sizeof(header));
	memset (sresult, 0, sizeof(sresult));
	unsigned char check[2];
	alevel_t alevel;
	memset(&alevel, 0, sizeof(alevel));

	packet_t pp = ax25_from_frame (example1, sizeof(example1), alevel);
	assert (pp != NULL);
	int e;
	e = il2p_type_1_header (pp, 0, header);
	assert (e == 0);
	ax25_delete(pp);

	//dw_printf ("Example 1 header:\n");
	//for (int i = 0 ; i < sizeof(header); i++) {
	//    dw_printf (" %02x", header[i]);
	//}
	///dw_printf ("\n");

	assert (memcmp(header, header1, sizeof(header)) == 0);

	il2p_scramble_block (header, sresult, 13);
	//dw_printf ("Expect scrambled  26 57 4d 57 f1 96 cc 85 42 e7 24 f7 2e\n");
	//for (int i = 0 ; i < sizeof(sresult); i++) {
	//   dw_printf (" %02x", sresult[i]);
	//}
	//dw_printf ("\n");

	il2p_encode_rs (sresult, 13, 2, check);

	//dw_printf ("expect checksum = 8a 97\n");
	//dw_printf ("check = ");
	//for (int i = 0 ; i < sizeof(check); i++) {
	//    dw_printf (" %02x", check[i]);
	//}
	//dw_printf ("\n");
	assert (check[0] == 0x8a);
	assert (check[1] == 0x97);

// Can we go from IL2P back to AX.25?

	pp = il2p_decode_header_type_1 (header, 0);
	assert (pp != NULL);

	char dst_addr[AX25_MAX_ADDR_LEN];
	char src_addr[AX25_MAX_ADDR_LEN];

	ax25_get_addr_with_ssid (pp, AX25_DESTINATION, dst_addr);
	ax25_get_addr_with_ssid (pp, AX25_SOURCE, src_addr);

	ax25_frame_type_t frame_type;
	cmdres_t cr;			// command or response.
	char description[64];
	int pf;				// Poll/Final.
	int nr, ns;			// Sequence numbers.

	frame_type = ax25_frame_type (pp, &cr, description, &pf, &nr, &ns);
	(void)frame_type;
#if 1
	dw_printf ("%s(): %s>%s: %s\n", __func__, src_addr, dst_addr, description);
#endif
// TODO: compare binary.
	ax25_delete (pp);

	dw_printf ("Example 1 header OK\n");


// -------------- Example 2 - UI frame, no info part  ------------------

//	This is an AX.25 Unnumbered Information frame, such as APRS.
//	Destination Callsign: ?CQ    -0
//	Source Callsign: ?KK4HEJ-15
//	P/F: 0
//	C: 0
//	Control Opcode:  3 Unnumbered Information
//	PID: 0xF0 No L3
//
//	AX.25 Data:
//	86 a2 40 40 40 40 60 96 96 68 90 8a 94 7f 03 f0
//
//	IL2P Data Prior to Scrambling and RS Encoding:
//	63 f1 40 40 40 00 6b 2b 54 28 25 2a 0f
//
//	IL2P Data After Scrambling and RS Encoding:
//	6a ea 9c c2 01 11 fc 14 1f da 6e f2 53 91 bd


	//dw_printf ("---------- example 2 ------------\n");
	static unsigned char example2[] = { 0x86, 0xa2, 0x40, 0x40, 0x40, 0x40, 0x60, 0x96, 0x96, 0x68, 0x90, 0x8a, 0x94, 0x7f, 0x03, 0xf0 };
	static unsigned char header2[]  = { 0x63, 0xf1, 0x40, 0x40, 0x40, 0x00, 0x6b, 0x2b, 0x54, 0x28, 0x25, 0x2a, 0x0f };
	memset (header, 0, sizeof(header));
	memset (sresult, 0, sizeof(sresult));
	memset(&alevel, 0, sizeof(alevel));

	pp = ax25_from_frame (example2, sizeof(example2), alevel);
	assert (pp != NULL);
	e = il2p_type_1_header (pp, 0, header);
	assert (e == 0);
	ax25_delete(pp);

	//dw_printf ("Example 2 header:\n");
	//for (int i = 0 ; i < sizeof(header); i++) {
	//    dw_printf (" %02x", header[i]);
	//}
	//dw_printf ("\n");

	assert (memcmp(header, header2, sizeof(header2)) == 0);

	il2p_scramble_block (header, sresult, 13);
	//dw_printf ("Expect scrambled  6a ea 9c c2 01 11 fc 14 1f da 6e f2 53\n");
	//for (int i = 0 ; i < sizeof(sresult); i++) {
	//   dw_printf (" %02x", sresult[i]);
	//}
	//dw_printf ("\n");

	il2p_encode_rs (sresult, 13, 2, check);

	//dw_printf ("expect checksum = 91 bd\n");
	//dw_printf ("check = ");
	//for (int i = 0 ; i < sizeof(check); i++) {
	//    dw_printf (" %02x", check[i]);
	//}
	//dw_printf ("\n");
	assert (check[0] == 0x91);
	assert (check[1] == 0xbd);

// Can we go from IL2P back to AX.25?

	pp = il2p_decode_header_type_1 (header, 0);
	assert (pp != NULL);

	ax25_get_addr_with_ssid (pp, AX25_DESTINATION, dst_addr);
	ax25_get_addr_with_ssid (pp, AX25_SOURCE, src_addr);

	frame_type = ax25_frame_type (pp, &cr, description, &pf, &nr, &ns);
	(void)frame_type;
#if 1
	dw_printf ("%s(): %s>%s: %s\n", __func__, src_addr, dst_addr, description);
#endif
// TODO: compare binary.

	ax25_delete (pp);
// TODO: more examples

	dw_printf ("Example 2 header OK\n");


// -------------- Example 3 - I Frame  ------------------

//	This is an AX.25 I-Frame with 9 bytes of information after the 16 byte header.
//
//	Destination Callsign: ?KA2DEW-2
//	Source Callsign: ?KK4HEJ-2
//	P/F: 1
//	C: 1
//	N(R): 5
//	N(S): 4
//	AX.25 PID: 0xCF TheNET
//	IL2P Payload Byte Count: 9
//
//	AX.25 Data:
//	96 82 64 88 8a ae e4 96 96 68 90 8a 94 65 b8 cf 30 31 32 33 34 35 36 37 38
//
//	IL2P Scrambled and Encoded Data:
//	26 13 6d 02 8c fe fb e8 aa 94 2d 6a 34 43 35 3c 69 9f 0c 75 5a 38 a1 7f f3 fc


	//dw_printf ("---------- example 3 ------------\n");
	static unsigned char example3[]  = { 0x96, 0x82, 0x64, 0x88, 0x8a, 0xae, 0xe4, 0x96, 0x96, 0x68, 0x90, 0x8a, 0x94, 0x65, 0xb8, 0xcf, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
	static unsigned char header3[]   = { 0x2b, 0xe1, 0x52, 0x64, 0x25, 0x77, 0x6b, 0x2b, 0xd4, 0x68, 0x25, 0xaa, 0x22 };
	static unsigned char complete3[] = { 0x26, 0x13, 0x6d, 0x02, 0x8c, 0xfe, 0xfb, 0xe8, 0xaa, 0x94, 0x2d, 0x6a, 0x34, 0x43, 0x35, 0x3c, 0x69, 0x9f, 0x0c, 0x75, 0x5a, 0x38, 0xa1, 0x7f, 0xf3, 0xfc };
	memset (header, 0, sizeof(header));
	memset (sresult, 0, sizeof(sresult));
	memset(&alevel, 0, sizeof(alevel));

	pp = ax25_from_frame (example3, sizeof(example3), alevel);
	assert (pp != NULL);
	e = il2p_type_1_header (pp, 0, header);
	assert (e == 9);
	ax25_delete(pp);

	//dw_printf ("Example 3 header:\n");
	//for (int i = 0 ; i < sizeof(header); i++) {
	//    dw_printf (" %02x", header[i]);
	//}
	//dw_printf ("\n");

	assert (memcmp(header, header3, sizeof(header)) == 0);

	il2p_scramble_block (header, sresult, 13);
	//dw_printf ("Expect scrambled  26 13 6d 02 8c fe fb e8 aa 94 2d 6a 34\n");
	//for (int i = 0 ; i < sizeof(sresult); i++) {
	//   dw_printf (" %02x", sresult[i]);
	//}
	//dw_printf ("\n");

	il2p_encode_rs (sresult, 13, 2, check);

	//dw_printf ("expect checksum = 43 35\n");
	//dw_printf ("check = ");
	//for (int i = 0 ; i < sizeof(check); i++) {
	//    dw_printf (" %02x", check[i]);
	//}
	//dw_printf ("\n");

	assert (check[0] == 0x43);
	assert (check[1] == 0x35);

	// That was only the header.  We will get to the info part in a later test.

// Can we go from IL2P back to AX.25?

	pp = il2p_decode_header_type_1 (header, 0);
	assert (pp != NULL);

	ax25_get_addr_with_ssid (pp, AX25_DESTINATION, dst_addr);
	ax25_get_addr_with_ssid (pp, AX25_SOURCE, src_addr);

	frame_type = ax25_frame_type (pp, &cr, description, &pf, &nr, &ns);
	(void)frame_type;
#if 1
	dw_printf ("%s(): %s>%s: %s\n", __func__, src_addr, dst_addr, description);
#endif
// TODO: compare binary.

	ax25_delete (pp);
	dw_printf ("Example 3 header OK\n");

// Example 3 again, this time the Information part is included.

	pp = ax25_from_frame (example3, sizeof(example3), alevel);
	assert (pp != NULL);

	int max_fec = 0;
	unsigned char iout[IL2P_MAX_PACKET_SIZE];
	e = il2p_encode_frame (pp, max_fec, iout);

	//dw_printf ("expected for example 3:\n");
	//fx_hex_dump(complete3, sizeof(complete3));
	//dw_printf ("actual result for example 3:\n");
	//fx_hex_dump(iout, e);
	// Does it match the example in the protocol spec?
	assert (e == sizeof(complete3));
	assert (memcmp(iout, complete3, sizeof(complete3)) == 0);
	ax25_delete (pp);

	dw_printf ("Example 3 with info OK\n");

} // end test_example_headers



/////////////////////////////////////////////////////////////////////////////////////////////
//
//	Test all of the frame types.
//
//	Encode to IL2P format, decode, and verify that the result is the same as the original.
//
/////////////////////////////////////////////////////////////////////////////////////////////


static void enc_dec_compare (packet_t pp1)
{
    for (int max_fec = 0; max_fec <= 1; max_fec++) {

	unsigned char encoded[IL2P_MAX_PACKET_SIZE];
	int enc_len;
	enc_len = il2p_encode_frame (pp1, max_fec, encoded);
	assert (enc_len >= 0);

	packet_t pp2;
	pp2 = il2p_decode_frame (encoded);
	assert (pp2 != NULL);

// Is it the same after encoding to IL2P and then decoding?

	int len1 = ax25_get_frame_len (pp1);
	unsigned char *data1 = ax25_get_frame_data_ptr (pp1);

	int len2 = ax25_get_frame_len (pp2);
	unsigned char *data2 = ax25_get_frame_data_ptr (pp2);

	if (len1 != len2 || memcmp(data1, data2, len1) != 0) {

	    dw_printf ("\nEncode/Decode Error.  Original:\n");
	    ax25_hex_dump (pp1);

	    dw_printf ("IL2P encoded as:\n");
	    fx_hex_dump(encoded, enc_len);

	    dw_printf ("Got turned into this:\n");
	    ax25_hex_dump (pp2);
	}

	assert (len1 == len2 && memcmp(data1, data2, len1) == 0);

	ax25_delete (pp2);
    }
}

static void all_frame_types(void)
{
	char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
	int num_addr = 2;
	cmdres_t cr;
	ax25_frame_type_t ftype;
	int pf = 0;
	int pid = 0xf0;
	int modulo;
	int nr, ns;
	unsigned char *pinfo = NULL;
	int info_len = 0;
	packet_t pp;

	strcpy (addrs[0], "W2UB");
	strcpy (addrs[1], "WB2OSZ-12");
	num_addr = 2;

	text_color_set(DW_COLOR_INFO);
	dw_printf ("Testing all frame types.\n");

/* U frame */

	dw_printf ("\nU frames...\n");

	for (ftype = frame_type_U_SABME; ftype <= frame_type_U_TEST; ftype++) {

	  for (pf = 0; pf <= 1; pf++) {

 	    int cmin = 0, cmax = 1;

	    switch (ftype) {
					// 0 = response, 1 = command
	      case frame_type_U_SABME:	cmin = 1; cmax = 1; break;
	      case frame_type_U_SABM:	cmin = 1; cmax = 1; break;
	      case frame_type_U_DISC:	cmin = 1; cmax = 1; break;
	      case frame_type_U_DM:	cmin = 0; cmax = 0; break;
	      case frame_type_U_UA:	cmin = 0; cmax = 0; break;
	      case frame_type_U_FRMR:	cmin = 0; cmax = 0; break;
	      case frame_type_U_UI:	cmin = 0; cmax = 1; break;
	      case frame_type_U_XID:	cmin = 0; cmax = 1; break;
	      case frame_type_U_TEST:	cmin = 0; cmax = 1; break;
	      default:			break;	// avoid compiler warning.		
	    }
	  
	    for (cr = cmin; cr <= cmax; cr++) {

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("\nConstruct U frame, cr=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	      pp = ax25_u_frame (addrs, num_addr, cr, ftype, pf, pid, pinfo, info_len);
	      ax25_hex_dump (pp);
	      enc_dec_compare (pp);
	      ax25_delete (pp);
	    }
	  }
	}


/* S frame */

	//strcpy (addrs[2], "DIGI1-1");
	//num_addr = 3;

	dw_printf ("\nS frames...\n");

	for (ftype = frame_type_S_RR; ftype <= frame_type_S_SREJ; ftype++) {

	  for (pf = 0; pf <= 1; pf++) {

	    modulo = 8;
	    nr = modulo / 2 + 1;

	    // SREJ can only be response.

 	    for (cr = 0; cr <= (int)(ftype!=frame_type_S_SREJ); cr++) {

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("\nConstruct S frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	      pp = ax25_s_frame (addrs, num_addr, cr, ftype, modulo, nr, pf, NULL, 0);

	      ax25_hex_dump (pp);
	      enc_dec_compare (pp);
	      ax25_delete (pp);
	    }

	    modulo = 128;
	    nr = modulo / 2 + 1;

 	    for (cr = 0; cr <= (int)(ftype!=frame_type_S_SREJ); cr++) {

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("\nConstruct S frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	      pp = ax25_s_frame (addrs, num_addr, cr, ftype, modulo, nr, pf, NULL, 0);

	      ax25_hex_dump (pp);
	      enc_dec_compare (pp);
	      ax25_delete (pp);
	    }
	  }
	}

/* SREJ is only S frame which can have information part. */

	static unsigned char srej_info[] = { 1<<1, 2<<1, 3<<1, 4<<1 };

	ftype = frame_type_S_SREJ;
	for (pf = 0; pf <= 1; pf++) {

	  modulo = 128;
	  nr = 127;
	  cr = cr_res;

	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("\nConstruct Multi-SREJ S frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	  pp = ax25_s_frame (addrs, num_addr, cr, ftype, modulo, nr, pf, srej_info, (int)(sizeof(srej_info)));

	  ax25_hex_dump (pp);
	  enc_dec_compare (pp);
	  ax25_delete (pp);
	}


/* I frame */

	dw_printf ("\nI frames...\n");

	pinfo = (unsigned char*)"The rain in Spain stays mainly on the plain.";
	info_len = strlen((char*)pinfo);

	for (pf = 0; pf <= 1; pf++) {

	  modulo = 8;
	  nr = 0x55 & (modulo - 1);
	  ns = 0xaa & (modulo - 1);

 	  for (cr = 1; cr <= 1; cr++) {		// can only be command

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("\nConstruct I frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	    pp = ax25_i_frame (addrs, num_addr, cr, modulo, nr, ns, pf, pid, pinfo, info_len);

	    ax25_hex_dump (pp);
	    enc_dec_compare (pp);
	    ax25_delete (pp);
	  }

	  modulo = 128;
	  nr = 0x55 & (modulo - 1);
	  ns = 0xaa & (modulo - 1);

 	  for (cr = 1; cr <= 1; cr++) {

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("\nConstruct I frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	    pp = ax25_i_frame (addrs, num_addr, cr, modulo, nr, ns, pf, pid, pinfo, info_len);

	    ax25_hex_dump (pp);
	    enc_dec_compare (pp);
	    ax25_delete (pp);
	  }
	}

} // end all_frame_types 


/////////////////////////////////////////////////////////////////////////////////////////////
//
//	Test bitstream tapped off from demodulator.
//
//	5 frames were sent to Nino TNC and a recording was made.
//	This was demodulated and the resulting bit stream saved to a file.
//
//	No automatic test here - must be done manually with audio recording.
//
/////////////////////////////////////////////////////////////////////////////////////////////

static int decoding_bitstream = 0;

static void decode_bitstream(void)
{
	dw_printf("-----\nReading il2p-bitstream.txt if available...\n");

	FILE *fp = fopen ("il2p-bitstream.txt", "r");
	if (fp == NULL) {
	  dw_printf ("Bitstream test file not available.\n");
	  return;
	}

	decoding_bitstream = 1;
	int save_previous = il2p_get_debug();
	il2p_set_debug (1);

	int ch;
	while ( (ch = fgetc(fp)) != EOF) {

	  if (ch == '0' || ch == '1') {
	    il2p_rec_bit (0, 0, 0, ch - '0');
	  }
	}
	fclose(fp);
	il2p_set_debug (save_previous);
	decoding_bitstream = 0;

}  // end decode_bitstream




/////////////////////////////////////////////////////////////////////////////////////////////
//
//	Test serialize / deserialize.
//
//	This uses same functions used on the air.	
//
/////////////////////////////////////////////////////////////////////////////////////////////

static char addrs2[] = "AA1AAA-1>ZZ9ZZZ-9";
static char addrs3[] = "AA1AAA-1>ZZ9ZZZ-9,DIGI*";
static char text[] = 
	"'... As I was saying, that seems to be done right - though I haven't time to look it over thoroughly just now - and that shows that there are three hundred and sixty-four days when you might get un-birthday presents -'"
	"\n"
	"'Certainly,' said Alice."
	"\n"
	"'And only one for birthday presents, you know. There's glory for you!'"
	"\n"
	"'I don't know what you mean by \"glory\",' Alice said."
	"\n"
	"Humpty Dumpty smiled contemptuously. 'Of course you don't - till I tell you. I meant \"there's a nice knock-down argument for you!\"'"
	"\n"
	"'But \"glory\" doesn't mean \"a nice knock-down argument\",' Alice objected."
	"\n"
	"'When I use a word,' Humpty Dumpty said, in rather a scornful tone, 'it means just what I choose it to mean - neither more nor less.'"
	"\n"
	"'The question is,' said Alice, 'whether you can make words mean so many different things.'"
	"\n"
	"'The question is,' said Humpty Dumpty, 'which is to be master - that's all.'"
	"\n" ;


static int rec_count = -1;	// disable deserialized packet test.
static int polarity = 0;

static void test_serdes (void)
{
	text_color_set(DW_COLOR_INFO);
	dw_printf ("\nTest serialize / deserialize...\n");
	rec_count = 0;

	int max_fec = 1;

	// try combinations of header type, max_fec, polarity, errors.

	for (int hdr_type = 0; hdr_type <= 1; hdr_type++) {
	    char packet[1024];
	    snprintf (packet, sizeof(packet), "%s:%s", hdr_type ? addrs2 : addrs3, text);
	    packet_t pp = ax25_from_text (packet, 1);
	    assert (pp != NULL);

	    int chan = 0;
	

	    for (max_fec = 0; max_fec <= 1; max_fec++) {
	        for (polarity = 0; polarity <= 2; polarity++) {	// 2 means throw in some errors.
 	            int num_bits_sent = il2p_send_frame (chan, pp, max_fec, polarity);
	            dw_printf ("%d bits sent.\n", num_bits_sent);

	            // Need extra bit at end to flush out state machine.
	            il2p_rec_bit (0, 0, 0, 0);
	        }
	    }
	    ax25_delete(pp);
	}

	dw_printf ("Serdes receive count = %d\n", rec_count);
	assert (rec_count == 12);
	rec_count = -1;		// disable deserialized packet test.
}


// Serializing calls this which then simulates the demodulator output.

void tone_gen_put_bit (int chan, int data)
{
	il2p_rec_bit (chan, 0, 0, data);
}

// This is called when a complete frame has been deserialized.

void multi_modem_process_rec_packet (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, retry_t retries, fec_type_t fec_type)
{
	if (rec_count < 0) return;	// Skip check before serdes test.

	rec_count++;

	// Does it have the the expected content?
	
	unsigned char *pinfo;
	int len = ax25_get_info(pp, &pinfo);
	assert (len == strlen(text));
	assert (strcmp(text, (char*)pinfo) == 0);

	dw_printf ("Number of symbols corrected: %d\n", retries);
	if (polarity == 2) {	// expecting errors corrected.
	    assert (retries == 10);
	}
	else {	// should be no errors.
	    assert (retries == 0);
	}

	ax25_delete (pp);
}

alevel_t demod_get_audio_level (int chan, int subchan)
{
	alevel_t alevel;
	memset (&alevel, 0, sizeof(alevel));
	return (alevel);
}

// end il2p_test.c