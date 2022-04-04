#ifndef __BCH_H
#define __BCH_H
#include <stdlib.h>

struct bch {
	int m;		// 2^m - 1 is max length, n
	int length;	// Actual packet size
	int n;		// 2^m - 1
	int k;		// Length of data portion
	int t;		// Number of correctable bits

	int *g;		// Calculated polynomial of length n - k
	int *alpha_to;
	int *index_of;
};

typedef struct bch bch_t;

int init_bch(bch_t *bch, int m, int length, int t);

void generate_bch(bch_t *bch, const int *data, int *bb);

int apply_bch(const bch_t *bch, int *recd);

void bytes_to_bits(const uint8_t *bytes, int *bit_dest, int num_bits);

void bits_to_bytes(const int *bits, uint8_t *byte_dest, int num_bits);

void swap_format(const int *bits, int *dest, int cutoff, int num_bits);

uint8_t rotate_byte(uint8_t x);

void rotate_bits(const int *in, int *out, int num_bits);

void print_bytes(const char *msg, const uint8_t *bytes, int num_bytes);

void print_bits(const char *msg, const int *bits, int num_bits);

void invert_bits(const int *bits, int *dest, int num_bits);

#endif
