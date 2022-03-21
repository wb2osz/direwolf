#ifndef __BCH_H
#define __BCH_H

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

int bch_init(bch_t *bch, int m, int length, int t);

void generate_bch(bch_t *bch, int *data, int *bb);

#endif
