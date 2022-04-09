#include <stdio.h>
#include <strings.h>
#include "bch.h"
#include "eotd.h"
#include "eotd_defs.h"

void dump(uint8_t *bytes, char type) {
			unsigned char eotd[9];
			for (int i = 0; i < 8; i++) {
			  eotd[i] = bytes[i] & 0xff;
			}
			eotd[8] = type;
			// Slice packet
			char buffer[512];
			eotd_to_text(eotd, 9, buffer, sizeof(buffer));
			printf("%s\n", buffer);
};

void rotate_bytes(uint8_t *src, uint8_t *dest, int count) {
	for (int i = 0; i < count; i++) {
		dest[count - i - 1] = rotate_byte(src[i]);
	}
}

int main(int argc, char **argv) {
	bch_t bch;
	uint8_t bytes[8];
	int bits[63];
	int m, length, t;
	int count = 0;
	int rev = 0;
	char type = EOTD_TYPE_R2F;


	if (argc < 5) {
		fprintf(stderr, "Expecting 4+ arguments - m, length, t, type (F or R) and optionally rev to reverse the input bytes.\n");
		fprintf(stderr, "THE BCH CODE IS NOT VERIFIED!\n");
		return -1;
	}

	sscanf(argv[1], "%d", &m);
	sscanf(argv[2], "%d", &length);
	sscanf(argv[3], "%d", &t);
	sscanf(argv[4], "%c", &type);

	if (argc > 5) {
		if (strcasecmp(argv[5], "rev") == 0) {
			rev = 1;
		}
	}

	init_bch(&bch, m, length, t);

	while (1) {
		for (int i = 0; i < 8; i++) {
			int t;
			int status = scanf("%x ", &t);
			bytes[i] = t;
			if (status == EOF) {
				return 0;
			}

			if (status != 1) {
				fprintf(stderr, "Error: %d", status);
			}
		}
		if (rev) {
			uint8_t temp[8];
			rotate_bytes(bytes, temp, 8);
			memcpy(bytes, temp, 8);
		}

	printf("%04d,", count++);
	dump(bytes, type);
	}

	return 0;
}
