/*
 * File:    bch3.c
 * Title:   Encoder/decoder for binary BCH codes in C (Version 3.1)
 * Author:  Robert Morelos-Zaragoza
 * Date:    August 1994
 * Revised: June 13, 1997
 *
 * ===============  Encoder/Decoder for binary BCH codes in C =================
 *
 * Version 1:   Original program. The user provides the generator polynomial
 *              of the code (cumbersome!).
 * Version 2:   Computes the generator polynomial of the code.
 * Version 3:   No need to input the coefficients of a primitive polynomial of
 *              degree m, used to construct the Galois Field GF(2**m). The
 *              program now works for any binary BCH code of length such that:
 *              2**(m-1) - 1 < length <= 2**m - 1
 *
 * Note:        You may have to change the size of the arrays to make it work.
 *
 * The encoding and decoding methods used in this program are based on the
 * book "Error Control Coding: Fundamentals and Applications", by Lin and
 * Costello, Prentice Hall, 1983.
 *
 * Thanks to Patrick Boyle (pboyle@era.com) for his observation that 'bch2.c'
 * did not work for lengths other than 2**m-1 which led to this new version.
 * Portions of this program are from 'rs.c', a Reed-Solomon encoder/decoder
 * in C, written by Simon Rockliff (simon@augean.ua.oz.au) on 21/9/89. The
 * previous version of the BCH encoder/decoder in C, 'bch2.c', was written by
 * Robert Morelos-Zaragoza (robert@spectra.eng.hawaii.edu) on 5/19/92.
 *
 * NOTE:    
 *          The author is not responsible for any malfunctioning of
 *          this program, nor for any damage caused by it. Please include the
 *          original program along with these comments in any redistribution.
 *
 *  For more information, suggestions, or other ideas on implementing error
 *  correcting codes, please contact me at:
 *
 *                           Robert Morelos-Zaragoza
 *                           5120 Woodway, Suite 7036
 *                           Houston, Texas 77056
 *
 *                    email: r.morelos-zaragoza@ieee.org
 *
 * COPYRIGHT NOTICE: This computer program is free for non-commercial purposes.
 * You may implement this program for any non-commercial application. You may 
 * also implement this program for commercial purposes, provided that you
 * obtain my written permission. Any modification of this program is covered
 * by this copyright.
 *
 * == Copyright (c) 1994-7,  Robert Morelos-Zaragoza. All rights reserved.  ==
 *
 * m = order of the Galois field GF(2**m) 
 * n = 2**m - 1 = size of the multiplicative group of GF(2**m)
 * length = length of the BCH code
 * t = error correcting capability (max. no. of errors the code corrects)
 * d = 2*t + 1 = designed min. distance = no. of consecutive roots of g(x) + 1
 * k = n - deg(g(x)) = dimension (no. of information bits/codeword) of the code
 * p[] = coefficients of a primitive polynomial used to generate GF(2**m)
 * g[] = coefficients of the generator polynomial, g(x)
 * alpha_to [] = log table of GF(2**m) 
 * index_of[] = antilog table of GF(2**m)
 * data[] = information bits = coefficients of data polynomial, i(x)
 * bb[] = coefficients of redundancy polynomial x^(length-k) i(x) modulo g(x)
 * numerr = number of errors 
 * errpos[] = error positions 
 * recd[] = coefficients of the received polynomial 
 * decerror = number of decoding errors (in _message_ positions) 
 *
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bch.h"

#undef DEBUG

int init_bch(bch_t *bch, int m, int length, int t) {

	int p[21], n;

	if (bch == NULL) {
		return -1;
	}

	if (m < 2 || m > 20) {
		return -2;
	}

	bch->m = m;
	bch->length = length;
	bch->t = t;

	for (int i=1; i<m; i++) {
		p[i] = 0;
	}

	p[0] = p[m] = 1;
	if (m == 2)			p[1] = 1;
	else if (m == 3)	p[1] = 1;
	else if (m == 4)	p[1] = 1;
	else if (m == 5)	p[2] = 1;
	else if (m == 6)	p[1] = 1;
	else if (m == 7)	p[1] = 1;
	else if (m == 8)	p[4] = p[5] = p[6] = 1;
	else if (m == 9)	p[4] = 1;
	else if (m == 10)	p[3] = 1;
	else if (m == 11)	p[2] = 1;
	else if (m == 12)	p[3] = p[4] = p[7] = 1;
	else if (m == 13)	p[1] = p[3] = p[4] = 1;
	else if (m == 14)	p[1] = p[11] = p[12] = 1;
	else if (m == 15)	p[1] = 1;
	else if (m == 16)	p[2] = p[3] = p[5] = 1;
	else if (m == 17)	p[3] = 1;
	else if (m == 18)	p[7] = 1;
	else if (m == 19)	p[1] = p[5] = p[6] = 1;
	else if (m == 20)	p[3] = 1;
    
	n = 1;
#ifdef DEBUG
	printf("p(x) = ");
#endif
	for (int i = 0; i <= m; i++) {
        	n *= 2;
#ifdef DEBUG
		printf("%1d", p[i]);
#endif
        }
#ifdef DEBUG
	printf("\n");
#endif
	n = n / 2 - 1;
	bch->n = n;
	int ninf = (n + 1) / 2 - 1;

	if (length < ninf || length > n) {
		return -3;
	}

/*
 * Generate field GF(2**m) from the irreducible polynomial p(X) with
 * coefficients in p[0]..p[m].
 *
 * Lookup tables:
 *   index->polynomial form: alpha_to[] contains j=alpha^i;
 *   polynomial form -> index form:	index_of[j=alpha^i] = i
 *
 * alpha=2 is the primitive element of GF(2**m) 
 */
	register int    i, mask;

	bch->alpha_to = malloc(n * sizeof(int));
	bch->index_of = malloc(n * sizeof(int));

	mask = 1;
	bch->alpha_to[m] = 0;
	for (int i = 0; i < m; i++) {
		bch->alpha_to[i] = mask;
		bch->index_of[bch->alpha_to[i]] = i;
		if (p[i] != 0)
			bch->alpha_to[m] ^= mask;
		mask <<= 1;
	}
	bch->index_of[bch->alpha_to[m]] = m;
	mask >>= 1;
	for (int i = m + 1; i < n; i++) {
		if (bch->alpha_to[i - 1] >= mask)
		  bch->alpha_to[i] = bch->alpha_to[m] ^ ((bch->alpha_to[i - 1] ^ mask) << 1);
		else
		  bch->alpha_to[i] = bch->alpha_to[i - 1] << 1;
		bch->index_of[bch->alpha_to[i]] = i;
	}
	bch->index_of[0] = -1;
	
/*
 * Compute the generator polynomial of a binary BCH code. Fist generate the
 * cycle sets modulo 2**m - 1, cycle[][] =  (i, 2*i, 4*i, ..., 2^l*i). Then
 * determine those cycle sets that contain integers in the set of (d-1)
 * consecutive integers {1..(d-1)}. The generator polynomial is calculated
 * as the product of linear factors of the form (x+alpha^i), for every i in
 * the above cycle sets.
 */
	register int	ii, jj, ll, kaux;
	register int	test, aux, nocycles, root, noterms, rdncy;
	int             cycle[1024][21], size[1024], min[1024], zeros[1024];

	/* Generate cycle sets modulo n, n = 2**m - 1 */
	cycle[0][0] = 0;
	size[0] = 1;
	cycle[1][0] = 1;
	size[1] = 1;
	jj = 1;			/* cycle set index */
#ifdef DEBUG
	if (bch->m > 9)  {
		printf("Computing cycle sets modulo %d\n", bch->n);
		printf("(This may take some time)...\n");
	}
#endif
	do {
		/* Generate the jj-th cycle set */
		ii = 0;
		do {
			ii++;
			cycle[jj][ii] = (cycle[jj][ii - 1] * 2) % bch->n;
			size[jj]++;
			aux = (cycle[jj][ii] * 2) % bch->n;
		} while (aux != cycle[jj][0]);
		/* Next cycle set representative */
		ll = 0;
		do {
			ll++;
			test = 0;
			for (ii = 1; ((ii <= jj) && (!test)); ii++)	
			/* Examine previous cycle sets */
			  for (kaux = 0; ((kaux < size[ii]) && (!test)); kaux++)
			     if (ll == cycle[ii][kaux])
			        test = 1;
		} while ((test) && (ll < (bch->n - 1)));
		if (!(test)) {
			jj++;	/* next cycle set index */
			cycle[jj][0] = ll;
			size[jj] = 1;
		}
	} while (ll < (bch->n - 1));
	nocycles = jj;		/* number of cycle sets modulo n */

	int d = 2 * t + 1;

	/* Search for roots 1, 2, ..., d-1 in cycle sets */
	kaux = 0;
	rdncy = 0;
	for (ii = 1; ii <= nocycles; ii++) {
		min[kaux] = 0;
		test = 0;
		for (jj = 0; ((jj < size[ii]) && (!test)); jj++)
			for (root = 1; ((root < d) && (!test)); root++)
				if (root == cycle[ii][jj])  {
					test = 1;
					min[kaux] = ii;
				}
		if (min[kaux]) {
			rdncy += size[min[kaux]];
			kaux++;
		}
	}
	noterms = kaux;
	kaux = 1;
	for (ii = 0; ii < noterms; ii++)
		for (jj = 0; jj < size[min[ii]]; jj++) {
			zeros[kaux] = cycle[min[ii]][jj];
			kaux++;
		}

	bch-> k = length - rdncy;

    if (bch->k<0)
      {
         return -4;
      }

#ifdef DEBUG
	printf("This is a (%d, %d, %d) binary BCH code\n", bch->length, bch->k, d);
#endif

	/* Compute the generator polynomial */
	bch->g = malloc((rdncy + 1) * sizeof(int));
	bch->g[0] = bch->alpha_to[zeros[1]];
	bch->g[1] = 1;		/* g(x) = (X + zeros[1]) initially */
	for (ii = 2; ii <= rdncy; ii++) {
	  bch->g[ii] = 1;
	  for (jj = ii - 1; jj > 0; jj--)
	    if (bch->g[jj] != 0)
	      bch->g[jj] = bch->g[jj - 1] ^ bch->alpha_to[(bch->index_of[bch->g[jj]] + zeros[ii]) % bch->n];
	    else
	      bch->g[jj] = bch->g[jj - 1];
	  bch->g[0] = bch->alpha_to[(bch->index_of[bch->g[0]] + zeros[ii]) % bch->n];
	}
#ifdef DEBUG
	printf("Generator polynomial:\ng(x) = ");
	for (ii = 0; ii <= rdncy; ii++) {
	  printf("%d", bch->g[ii]);
	}
	printf("\n");
#endif
	return 0;
}

void generate_bch(bch_t *bch, int *data, int *bb) {
/*
 * Compute redundacy bb[], the coefficients of b(x). The redundancy
 * polynomial b(x) is the remainder after dividing x^(length-k)*data(x)
 * by the generator polynomial g(x).
 */
	register int    feedback;

	for (int i = 0; i < bch->length - bch->k; i++)
		bb[i] = 0;
	for (int i = bch->k - 1; i >= 0; i--) {
		feedback = data[i] ^ bb[bch->length - bch->k - 1];
		if (feedback != 0) {
			for (int j = bch->length - bch->k - 1; j > 0; j--)
				if (bch->g[j] != 0)
					bb[j] = bb[j - 1] ^ feedback;
				else
					bb[j] = bb[j - 1];
			bb[0] = bch->g[0] && feedback;
		} else {
			for (int j = bch->length - bch->k - 1; j > 0; j--)
				bb[j] = bb[j - 1];
			bb[0] = 0;
		}
	}
}


int apply_bch(bch_t *bch, int *recd)
/*
 * Simon Rockliff's implementation of Berlekamp's algorithm.
 *
 * Assume we have received bits in recd[i], i=0..(n-1).
 *
 * Compute the 2*t syndromes by substituting alpha^i into rec(X) and
 * evaluating, storing the syndromes in s[i], i=1..2t (leave s[0] zero) .
 * Then we use the Berlekamp algorithm to find the error location polynomial
 * elp[i].
 *
 * If the degree of the elp is >t, then we cannot correct all the errors, and
 * we have detected an uncorrectable error pattern. We output the information
 * bits uncorrected.
 *
 * If the degree of elp is <=t, we substitute alpha^i , i=1..n into the elp
 * to get the roots, hence the inverse roots, the error location numbers.
 * This step is usually called "Chien's search".
 *
 * If the number of errors located is not equal the degree of the elp, then
 * the decoder assumes that there are more than t errors and cannot correct
 * them, only detect them. We output the information bits uncorrected.
 */
{
	register int    i, j, u, q, t2, count = 0, syn_error = 0;
	int             elp[1026][1024], d[1026], l[1026], u_lu[1026], s[1025];
	int             root[200], loc[200], err[1024], reg[201];

	t2 = 2 * bch->t;

	/* first form the syndromes */
#ifdef DEBUG
	printf("S(x) = ");
#endif
	for (i = 1; i <= t2; i++) {
		s[i] = 0;
		for (j = 0; j < bch->length; j++)
			if (recd[j] != 0)
				s[i] ^= bch->alpha_to[(i * j) % bch->n];
		if (s[i] != 0)
			syn_error = 1; /* set error flag if non-zero syndrome */
/*
 * Note:    If the code is used only for ERROR DETECTION, then
 *          exit program here indicating the presence of errors.
 */
		/* convert syndrome from polynomial form to index form  */
		s[i] = bch->index_of[s[i]];
#ifdef DEBUG
		printf("%3d ", s[i]);
#endif
	}
#ifdef DEBUG
	printf("\n");
#endif

	if (syn_error) {	/* if there are errors, try to correct them */
		/*
		 * Compute the error location polynomial via the Berlekamp
		 * iterative algorithm. Following the terminology of Lin and
		 * Costello's book :   d[u] is the 'mu'th discrepancy, where
		 * u='mu'+1 and 'mu' (the Greek letter!) is the step number
		 * ranging from -1 to 2*t (see L&C),  l[u] is the degree of
		 * the elp at that step, and u_l[u] is the difference between
		 * the step number and the degree of the elp. 
		 */
		/* initialise table entries */
		d[0] = 0;			/* index form */
		d[1] = s[1];		/* index form */
		elp[0][0] = 0;		/* index form */
		elp[1][0] = 1;		/* polynomial form */
		for (i = 1; i < t2; i++) {
			elp[0][i] = -1;	/* index form */
			elp[1][i] = 0;	/* polynomial form */
		}
		l[0] = 0;
		l[1] = 0;
		u_lu[0] = -1;
		u_lu[1] = 0;
		u = 0;
 
		do {
			u++;
			if (d[u] == -1) {
				l[u + 1] = l[u];
				for (i = 0; i <= l[u]; i++) {
					elp[u + 1][i] = elp[u][i];
					elp[u][i] = bch->index_of[elp[u][i]];
				}
			} else
				/*
				 * search for words with greatest u_lu[q] for
				 * which d[q]!=0 
				 */
			{
				q = u - 1;
				while ((d[q] == -1) && (q > 0))
					q--;
				/* have found first non-zero d[q]  */
				if (q > 0) {
				  j = q;
				  do {
				    j--;
				    if ((d[j] != -1) && (u_lu[q] < u_lu[j]))
				      q = j;
				  } while (j > 0);
				}
 
				/*
				 * have now found q such that d[u]!=0 and
				 * u_lu[q] is maximum 
				 */
				/* store degree of new elp polynomial */
				if (l[u] > l[q] + u - q)
					l[u + 1] = l[u];
				else
					l[u + 1] = l[q] + u - q;
 
				/* form new elp(x) */
				for (i = 0; i < t2; i++)
					elp[u + 1][i] = 0;
				for (i = 0; i <= l[q]; i++)
					if (elp[q][i] != -1)
						elp[u + 1][i + u - q] = 
                                   bch->alpha_to[(d[u] + bch->n - d[q] + elp[q][i]) % bch->n];
				for (i = 0; i <= l[u]; i++) {
					elp[u + 1][i] ^= elp[u][i];
					elp[u][i] = bch->index_of[elp[u][i]];
				}
			}
			u_lu[u + 1] = u - l[u + 1];
 
			/* form (u+1)th discrepancy */
			if (u < t2) {	
			/* no discrepancy computed on last iteration */
			  if (s[u + 1] != -1)
			    d[u + 1] = bch->alpha_to[s[u + 1]];
			  else
			    d[u + 1] = 0;
			    for (i = 1; i <= l[u + 1]; i++)
			      if ((s[u + 1 - i] != -1) && (elp[u + 1][i] != 0))
			        d[u + 1] ^= bch->alpha_to[(s[u + 1 - i] 
			                      + bch->index_of[elp[u + 1][i]]) % bch->n];
			  /* put d[u+1] into index form */
			  d[u + 1] = bch->index_of[d[u + 1]];	
			}
		} while ((u < t2) && (l[u + 1] <= bch->t));
 
		u++;
		if (l[u] <= bch->t) {/* Can correct errors */
			/* put elp into index form */
			for (i = 0; i <= l[u]; i++)
				elp[u][i] = bch->index_of[elp[u][i]];

#ifdef DEBUG
			printf("sigma(x) = ");
			for (i = 0; i <= l[u]; i++)
				printf("%3d ", elp[u][i]);
			printf("\n");
			printf("Roots: ");
#endif
			/* Chien search: find roots of the error location polynomial */
			for (i = 1; i <= l[u]; i++)
				reg[i] = elp[u][i];
			count = 0;
			for (i = 1; i <= bch->n; i++) {
				q = 1;
				for (j = 1; j <= l[u]; j++)
					if (reg[j] != -1) {
						reg[j] = (reg[j] + j) % bch->n;
						q ^= bch->alpha_to[reg[j]];
					}
				if (!q) {	/* store root and error
						 * location number indices */
					root[count] = i;
					loc[count] = bch->n - i;
					count++;
#ifdef DEBUG
					printf("%3d ", bch->n - i);
#endif
				}
			}
#ifdef DEBUG
			printf("\n");
#endif
			if (count == l[u]) {
			/* no. roots = degree of elp hence <= t errors */
				for (i = 0; i < l[u]; i++)
					recd[loc[i]] ^= 1;
				return l[u];
			}
			else {	/* elp has degree >t hence cannot solve */
#ifdef DEBUG
				printf("Incomplete decoding: errors detected\n");
#endif
				return -1;
			}
		} else {
			return -1;
		}
	} else {
		return 0; // No errors
	}
}

/* LEFT justified in hex */
void bytes_to_bits(int *bytes, int *bit_dest, int num_bits) {
	for (int i = 0; i < num_bits; i++) {
		int index = i / 8;
		int bit_pos = 7 - (i % 8);
		int bit_mask = 1 << bit_pos;
		bit_dest[i] = (bytes[index] & bit_mask) != 0;
	}
}

void bits_to_bytes(int *bits, int *byte_dest, int num_bits) {

	int index;

	for (int i = 0; i < num_bits; i++) {
		index = i / 8;
		if (i % 8 == 0) {
			byte_dest[index] = 0;
		}

		byte_dest[index] <<= 1;
		byte_dest[index] |= bits[i] & 0x01;
	}

	byte_dest[index] <<= 8 - (num_bits % 8);
}

void swap_format(int *bits, int cutoff, int num_bits) {
	// Do it the easy way
	int *temp = malloc(num_bits * sizeof(int));
	for (int i = 0; i < num_bits; i++) {
		if (i < cutoff) {
			temp[num_bits - cutoff + i] = bits[i];
		} else {
			temp[i - cutoff] = bits[i];
		}
	}

	memcpy(bits, temp, num_bits * sizeof(int));
}

void dump_bch(bch_t *bch) {
	printf("m: %d length: %d t: %d n: %d k: %d\n", bch->m, bch->length, bch->t, bch->n, bch->k);
}

#undef MAIN
#undef TEST_BYTES_TO_BITS
#define TEST_SWAP
#ifdef MAIN
int main()
{
	int test[][8] = {
/* 0 errors */	{ 0xb2, 0x17, 0xa2, 0xb9, 0x53, 0xdd, 0xc5, 0x52 }, /* perfect random test */
                { 0xf0, 0x5a, 0x6a, 0x6a, 0x01, 0x63, 0x33, 0xd0 }, /* g001-cut-lenthened_457.938M.wav */
                { 0xf0, 0x81, 0x52, 0x6b, 0x71, 0xa5, 0x63, 0x08 }, /* 1st in eotd_received_data */
/* 3 errors */	{ 0xf0, 0x85, 0x50, 0x6a, 0x01, 0xe5, 0x6e, 0x84 }, /* 2nd in eotd_received_data - 3 bad bits */
/* 0 errors */	{ 0xf0, 0x85, 0x50, 0x6a, 0x01, 0xe5, 0x06, 0x84 }, /* 2nd, but with the bits fixed */
/* 3 errors */	{ 0xf0, 0x85, 0x59, 0x5a, 0x01, 0xe5, 0x6e, 0x84 }, /* 3rd - 3 bad bits */
/* 0 errors */	{ 0xb0, 0x85, 0x59, 0x5a, 0x11, 0xe5, 0x6f, 0x84 }, /* 3rd fixed */
                { 0xf1, 0x34, 0x50, 0x1a, 0x01, 0xe5, 0x66, 0xfe }, /* 4th */
                { 0xf0, 0xeb, 0x10, 0xea, 0x01, 0x6e, 0x54, 0x1c }, /* 5th */
                { 0xf0, 0xea, 0x5c, 0xea, 0x01, 0x6e, 0x55, 0x0e }, /* 6th */
                { 0xe0, 0x21, 0x10, 0x1a, 0x01, 0x32, 0xbc, 0xe4 }, /* Sun Mar 20 05:41:00 2022 */
                { 0xf0, 0x42, 0x50, 0x5b, 0xcf, 0xd5, 0x64, 0xe4 }, /* Sun Mar 20 12:58:43 2022 */
                { 0xf0, 0x8c, 0x10, 0xaa, 0x01, 0x73, 0x7b, 0x1a }, /* Sun Mar 20 13:35:48 2022 */
                { 0xf0, 0x8c, 0x10, 0xb1, 0xc0, 0xe0, 0x90, 0x64 }, /* Sun Mar 20 13:37:05 2022 */
/* 3 errors */	{ 0xf0, 0x8c, 0x10, 0x6a, 0x01, 0x64, 0x7a, 0xe8 }, /* Sun Mar 20 13:37:48 2022  - 3 bad bits */
/* 0 errors */	{ 0x50, 0x8c, 0x12, 0x6a, 0x01, 0x64, 0x7a, 0xe8 }, /* Sun Mar 20 13:37:48 2022 with bits fixed */
	};

	int bits[63];
	int temp[8];
	bch_t bch;

	init_bch(&bch, 6, 63, 3);

	for (int count = 0; count < sizeof(test) / sizeof(*test); count++) {
		bytes_to_bits(test[count], bits, 63);

		printf("--------------------------\nORIG pkt [%d] ", count);
		for (int i = 0; i < 8; i++) {
			printf("%02x ", test[count][i]);
		}
		printf("\n");

#ifdef TEST_BYTES_TO_BITS
	
		printf("ORIG pkt[%d] bits\n", count);
		for (int i = 0; i < 63; i++) {
			printf("%d ", bits[i]);
		}
		printf("\n");

		bits_to_bytes(bits, temp, 63);
		printf("bits_to_bytes pkt [%d]\n", count);
		for (int i = 0; i < 8; i++) {
			printf("%02x ", temp[i]);
		}
		printf("\n");

#endif

#ifdef TEST_GENERATE	
		int bch_code[18];
		generate_bch(&bch, bits, bch_code);
		printf("generated BCH\n");
		for (int i = 0; i < 18; i++) {
			printf("%d ", bch_code[i]);
		}
		printf("\n");
#endif	

#ifdef TEST_SWAP
		printf("orig: ");
		for (int i = 0; i < 63; i++) {
			printf("%d ", bits[i]);
		}
		printf("\n");

		swap_format(bits, 45, 63);

		printf("rev:  ");
		for (int i = 0; i < 63; i++) {
			printf("%d ", bits[i]);
		}
		printf("\n");
#endif
#ifdef TEST_APPLY
		int recv[63];

		for (int i = 0; i < 63; i++) {
			recv[i] = bits[i];
		}
/*	
		printf("rearranged packet [%d]: ", count);
		for (int i = 0; i < 63; i++) {
			printf("%d ", recv[i]);
		}
		printf("\n");

		bits_to_bytes(recv, temp, 63);

		printf("original [%d] bytes: ", count);
		for (int i = 0; i < 8; i++) {
			printf("%02x ", temp[i]);
		}
		printf("\n");
*/	
		int corrected = apply_bch(&bch, recv);

		if (corrected >= 0) {
/*
			printf("corrected [%d] packet: ", corrected);
			for (int i = 0; i < 63; i++) {
				printf("%d ", recv[i]);
			}
			printf("\n");
*/
			bits_to_bytes(recv, temp, 63);

			printf("corrected [%d] bytes: ", corrected);
			for (int i = 0; i < 8; i++) {
				printf("%02x ", temp[i]);
			}
			printf("\n");
		}
#endif
	}
}
#endif
