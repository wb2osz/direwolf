

#ifndef IL2P_H
#define IL2P_H 1


#define IL2P_PREAMBLE 0x55

#define IL2P_SYNC_WORD 0xF15E48

#define IL2P_SYNC_WORD_SIZE 3
#define IL2P_HEADER_SIZE 13	// Does not include 2 parity.
#define IL2P_HEADER_PARITY 2

#define IL2P_MAX_PAYLOAD_SIZE 1023
#define IL2P_MAX_PAYLOAD_BLOCKS 5
#define IL2P_MAX_PARITY_SYMBOLS 16		// For payload only.
#define IL2P_MAX_ENCODED_PAYLOAD_SIZE (IL2P_MAX_PAYLOAD_SIZE + IL2P_MAX_PAYLOAD_BLOCKS * IL2P_MAX_PARITY_SYMBOLS)

#define IL2P_MAX_PACKET_SIZE (IL2P_SYNC_WORD_SIZE + IL2P_HEADER_SIZE + IL2P_HEADER_PARITY + IL2P_MAX_ENCODED_PAYLOAD_SIZE)


///////////////////////////////////////////////////////////////////////////////
//
// 	il2p_init.c
//
///////////////////////////////////////////////////////////////////////////////


// Init must be called at start of application.

extern void il2p_init (int debug);

#include "fx25.h"	// For Reed Solomon stuff.  e.g. struct rs
			// Maybe rearrange someday because RS now used another place.

extern struct rs *il2p_find_rs(int nparity);	// Internal later?

extern void il2p_encode_rs (unsigned char *tx_data, int data_size, int num_parity, unsigned char *parity_out);

extern int il2p_decode_rs (unsigned char *rec_block, int data_size, int num_parity, unsigned char *out);

extern int il2p_get_debug(void);
extern void il2p_set_debug(int debug);


///////////////////////////////////////////////////////////////////////////////
//
// 	il2p_rec.c
//
///////////////////////////////////////////////////////////////////////////////

// Receives a bit stream from demodulator.

extern void il2p_rec_bit (int chan, int subchan, int slice, int dbit);




///////////////////////////////////////////////////////////////////////////////
//
// 	il2p_send.c
//
///////////////////////////////////////////////////////////////////////////////

#include "ax25_pad.h"	// For packet object.

// Send bit stream to modulator.

int il2p_send_frame (int chan, packet_t pp, int max_fec, int polarity);



///////////////////////////////////////////////////////////////////////////////
//
// 	il2p_codec.c
//
///////////////////////////////////////////////////////////////////////////////

#include "ax25_pad.h"

extern int il2p_encode_frame (packet_t pp, int max_fec, unsigned char *iout);

packet_t il2p_decode_frame (unsigned char *irec);

packet_t il2p_decode_header_payload (unsigned char* uhdr, unsigned char *epayload, int *symbols_corrected);




///////////////////////////////////////////////////////////////////////////////
//
// 	il2p_header.c
//
///////////////////////////////////////////////////////////////////////////////


extern int il2p_type_1_header (packet_t pp, int max_fec, unsigned char *hdr);

extern packet_t il2p_decode_header_type_1 (unsigned char *hdr, int num_sym_changed);


extern int il2p_type_0_header (packet_t pp, int max_fec, unsigned char *hdr);

extern int il2p_clarify_header(unsigned char *rec_hdr, unsigned char *corrected_descrambled_hdr);



///////////////////////////////////////////////////////////////////////////////
//
// 	il2p_scramble.c
//
///////////////////////////////////////////////////////////////////////////////

extern void il2p_scramble_block (unsigned char *in, unsigned char *out, int len);

extern void il2p_descramble_block (unsigned char *in, unsigned char *out, int len);


///////////////////////////////////////////////////////////////////////////////
//
// 	il2p_payload.c
//
///////////////////////////////////////////////////////////////////////////////


typedef struct {
	int payload_byte_count;		// Total size, 0 thru 1023
	int payload_block_count;
	int small_block_size;
	int large_block_size;
	int large_block_count;
	int small_block_count;
	int parity_symbols_per_block;	// 2, 4, 6, 8, 16
} il2p_payload_properties_t;

extern int il2p_payload_compute (il2p_payload_properties_t *p, int payload_size, int max_fec);

extern int il2p_encode_payload (unsigned char *payload, int payload_size, int max_fec, unsigned char *enc);

extern int il2p_decode_payload (unsigned char *received, int payload_size, int max_fec, unsigned char *payload_out, int *symbols_corrected);

extern int il2p_get_header_attributes (unsigned char *hdr, int *hdr_type, int *max_fec);

#endif
