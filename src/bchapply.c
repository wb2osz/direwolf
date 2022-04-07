#include <stdio.h>
#include <string.h>
#include "bch.h"

#define SHOW_BYTES

int test(bch_t *bch, char *msg, int *bits, int length) {
	int corrected = 0;
	int temp_bits[length];
	uint8_t bytes[8];

	memcpy(temp_bits, bits, length * sizeof(int));
#ifdef SHOW_BYTES
		bits_to_bytes(temp_bits, bytes, length);
		print_bytes(msg, bytes, 8);
#else
		print_bits(msg, temp_bits, length);
#endif
		corrected = apply_bch(bch, temp_bits);

		if (corrected >= 0) {
			printf("corrected %d ", corrected);
#ifdef SHOW_BYTES
			bits_to_bytes(temp_bits, bytes, length);
			printf("CORR ");
			print_bytes(msg, bytes, 8);
#else
			print_bits(msg, temp_bits, length);
#endif
		printf("\n");
		} else {
			printf("invalid.\n");
		}

	return corrected >= 0;
}

int main(int argc, char **argv) {
	bch_t bch;
	uint8_t bytes[8];
	int m, length, t;
	int data_len, crc_len;


	if (argc != 4) {
		fprintf(stderr, "Expecting 3 arguments.\n");
		return -1;
	}

	sscanf(argv[1], "%d", &m);
	sscanf(argv[2], "%d", &length);
	sscanf(argv[3], "%d", &t);

	int orig_bits[length+1];

	init_bch(&bch, m, length, t);
	data_len = bch.k;
	crc_len = bch.length - bch.k;

printf("m=%d, length=%d, n=%d, k=%d, t=%d\n", bch.m, bch.length, bch.n, bch.k, bch.t);
printf("data_len=%d, crc_len=%d\n", data_len, crc_len);

//
//	THIS IS THE LSB-FIRST VERSION
//
fprintf(stderr, "Enter HCB+ATAD _WITH_ the parity bit intact and XOR already applied.\n");
	while (1) {
		for (int i = 0; i < 8; i++) {
			int temp;
			int status = scanf("%x ", &temp);
			bytes[i] = temp;
			if (status == EOF) {
				return 0;
			}

			if (status != 1) {
				fprintf(stderr, "Error: %d", status);
			}
printf("%0x ", bytes[i]);
		}
printf("\n");

		int temp[length];

		// HCB + ATAD
		bytes_to_bits(bytes, orig_bits, length+1);
		memcpy(temp, orig_bits+1, length * sizeof(int));
		print_bits("atad: ", temp + crc_len, data_len);
		printf("\n");
		print_bits("hcb: ", temp, crc_len);
		printf("\n");

		test(&bch, "HCB+ATAD: ", temp, length);

		// ATAD+HCB
		bytes_to_bits(bytes, orig_bits, length+1);
		swap_format(orig_bits+1, temp, crc_len, length);
		print_bits("atad: ", temp, data_len);
		printf("\n");
		print_bits("hcb: ", temp+data_len, crc_len);
		printf("\n");
		test(&bch, "ATAD+HCB: ", temp, length);

		// DATA + BCH
		bytes_to_bits(bytes, orig_bits, length+1);
		rotate_bits(orig_bits+1, temp, length);
		print_bits("data: ", temp, data_len);
		printf("\n");
		print_bits("bch: ", temp+data_len, crc_len);
		printf("\n");
		test(&bch, "DATA+BCH: ", temp, length);

		// BCH+DATA
		int swap[length];
		bytes_to_bits(bytes, orig_bits, length+1);
		rotate_bits(orig_bits+1, temp, length);
		// now DATA+BCH
		swap_format(temp, swap, data_len, length);
		// now BCH + DATA
		print_bits("data: ", swap + crc_len, data_len);
		printf("\n");
		print_bits("bch: ", swap, crc_len);
		printf("\n");
		test(&bch, "BCH+DATA: ", swap, length);

		int rot[length];
		// DATA + HCB
		bytes_to_bits(bytes, orig_bits, length+1);
		memcpy(rot+data_len, orig_bits + 1, crc_len * sizeof(int));
		rotate_bits(orig_bits+1+crc_len, temp, data_len);
		memcpy(rot, temp, data_len * sizeof(int));
		print_bits("data: ", rot, data_len);
		printf("\n");
		print_bits("hcb: ", rot+data_len, crc_len);
		printf("\n");
		// Now DATA+HCB
		test(&bch, "DATA+HCB: ", rot, length);
		
		// ATAD+BCH
		bytes_to_bits(bytes, orig_bits, length+1);
		// h+a
		memcpy(rot, orig_bits+1+crc_len, data_len * sizeof(int));
		rotate_bits(orig_bits+1, temp, crc_len);
		memcpy(rot+data_len, temp, crc_len * sizeof(int));
		// Now ATAD+BCH
		print_bits("atad: ", rot, data_len);
		printf("\n");
		print_bits("bch: ", rot+data_len, crc_len);
		printf("\n");
		test(&bch, "ATAD+BCH: ", rot, length);
		
		// HCB+DATA
		bytes_to_bits(bytes, orig_bits, length+1);
		memcpy(rot, orig_bits+1, crc_len * sizeof(int));
		rotate_bits(orig_bits+1+crc_len, temp, data_len);
		memcpy(rot+crc_len, temp, data_len * sizeof(int));
		print_bits("data: ", rot+crc_len, data_len);
		printf("\n");
		print_bits("hcb: ", rot, crc_len);
		printf("\n");
		// Now HCB+DATA
		test(&bch, "HCB+DATA: ", rot, length);
		
		// BCH+ATAD
		bytes_to_bits(bytes, orig_bits, length+1);
		memcpy(rot+crc_len, orig_bits+1+crc_len, data_len * sizeof(int));
		rotate_bits(orig_bits+1, temp, crc_len);
		memcpy(rot, temp, crc_len * sizeof(int));
		print_bits("atad: ", rot + crc_len, data_len);
		printf("\n");
		print_bits("bch: ", rot, crc_len);
		printf("\n");
		// Now BCH+ATAD
		test(&bch, "BCH+ATAD: ", rot, length);
	}
}
