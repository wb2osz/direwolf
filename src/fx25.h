#ifndef FX25_H
#define FX25_H

#include <stdint.h>	// for uint64_t


/* Reed-Solomon codec control block */
struct rs {
  unsigned int mm;              /* Bits per symbol */
  unsigned int nn;              /* Symbols per block (= (1<<mm)-1) */
  unsigned char *alpha_to;      /* log lookup table */
  unsigned char *index_of;      /* Antilog lookup table */
  unsigned char *genpoly;       /* Generator polynomial */
  unsigned int nroots;     /* Number of generator roots = number of parity symbols */
  unsigned char fcr;        /* First consecutive root, index form */
  unsigned char prim;       /* Primitive element, index form */
  unsigned char iprim;      /* prim-th root of 1, index form */
};

#define MM (rs->mm)
#define NN (rs->nn)
#define ALPHA_TO (rs->alpha_to) 
#define INDEX_OF (rs->index_of)
#define GENPOLY (rs->genpoly)
#define NROOTS (rs->nroots)
#define FCR (rs->fcr)
#define PRIM (rs->prim)
#define IPRIM (rs->iprim)
#define A0 (NN)



__attribute__((always_inline))
static inline int modnn(struct rs *rs, int x){
  while (x >= rs->nn) {
    x -= rs->nn;
    x = (x >> rs->mm) + (x & rs->nn);
  }
  return x;
}

#define MODNN(x) modnn(rs,x)


#define ENCODE_RS encode_rs_char
#define DECODE_RS decode_rs_char
#define INIT_RS init_rs_char
#define FREE_RS free_rs_char

#define DTYPE unsigned char

void ENCODE_RS(struct rs *rs, DTYPE *data, DTYPE *bb);

int DECODE_RS(struct rs *rs, DTYPE *data, int *eras_pos, int no_eras);

struct rs *INIT_RS(unsigned int symsize, unsigned int gfpoly,
		   unsigned int fcr, unsigned int prim, unsigned int nroots);

void FREE_RS(struct rs *rs);



// These 3 are the external interface.
// Maybe these should be in a different file, separated from the internal stuff.

void fx25_init ( int debug_level );
int fx25_send_frame (int chan, unsigned char *fbuf, int flen, int fx_mode);
void fx25_rec_bit (int chan, int subchan, int slice, int dbit);
int fx25_rec_busy (int chan);


// Other functions in fx25_init.c.

struct rs *fx25_get_rs (int ctag_num);
uint64_t fx25_get_ctag_value (int ctag_num);
int fx25_get_k_data_radio (int ctag_num);
int fx25_get_k_data_rs (int ctag_num);
int fx25_get_nroots (int ctag_num);
int fx25_get_debug (void);
int fx25_tag_find_match (uint64_t t);
int fx25_pick_mode (int fx_mode, int dlen);

void fx_hex_dump(unsigned char *x, int len);



#define CTAG_MIN 0x01
#define CTAG_MAX 0x0B

// Maximum sizes of "data" and "check" parts.

#define FX25_MAX_DATA 239	// i.e. RS(255,239)
#define FX25_MAX_CHECK 64	// e.g. RS(255, 191)
#define FX25_BLOCK_SIZE 255	// Block size always 255 for 8 bit symbols.

#endif // FX25_H