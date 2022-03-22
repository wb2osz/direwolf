#include <stdio.h>
#include "bch.h"

int main(int argc, char **argv) {
	bch_t bch;
	int bytes[8];
	int bits[63];

	init_bch(&bch, 6, 63, 3);
#ifdef ARGS
	if (argc != 9) {
		fprintf(stderr, "Expecting 8 arguments.\n");
		return -1;
	}

	for (int i = 0; i < 8; i++) {
		sscanf(argv[i + 1], "%x", bytes + i);
	}
#else
	while (1) {
		for (int i = 0; i < 8; i++) {
			int status = scanf("%x ", bytes + i);
			if (status == EOF) {
				return 0;
			}

			if (status != 1) {
				fprintf(stderr, "Error: %d", status);
			}
		}
#endif
	// UNEEDED
	// swap_format(bits, 45, 63);
	bytes_to_bits(bytes, bits, 63);
	int corrected = apply_bch(&bch, bits);
	if (corrected >= 0) {
#ifdef DEBUG
		printf("%d corrected\n", corrected);
		for (int i = 0; i < 8; i++) {
			printf("%02x ", bytes[i]);
		}
		printf("\n");
#endif
		bits_to_bytes(bits, bytes, 63);
		for (int i = 0; i < 8; i++) {
			printf("%02x ", bytes[i]);
		}
		// Slice packet

		printf("chain=%1x,",(bytes[0] >> 6) & 0x03);
		printf("devst=%1x,",(bytes[0] >> 4) & 0x03);
		printf("msgid=%1x,",(bytes[0] >> 1) & 0x07);
		printf("uaddr=%03x,",((bytes[0] & 0x01) << 16) | (bytes[1] << 8) | (bytes[2]));
		printf("bpres=%d,",(bytes[3] >> 1) & 0x07f);
		printf("dbit1=%02x,",((bytes[3] & 0x01) << 7) | ((bytes[4] >> 1) & 0x7f));
		printf("confm=%x,",(bytes[5] >> 7) & 0x01);
		printf("dbit2=%x,",(bytes[5] >> 6) & 0x01);
		printf("motdt=%x,",(bytes[5] >> 5) & 0x01);
		printf("ltbat=%x,",(bytes[5] >> 4) & 0x01);
		printf("ltsta=%x",(bytes[5] >> 3) & 0x01);
 		printf("\n");
	}
#ifndef ARGS
	}
#endif

	return 0;
}
