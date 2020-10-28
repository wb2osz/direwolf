
/*------------------------------------------------------------------
 *
 * File:        rpack.h
 *
 * Purpose:   	Definition of Garmin Rino message format.
 *		
 * References:	http://www.radio-active.net.au/web3/APRS/Resources/RINO
 *
 *		http://www.radio-active.net.au/web3/APRS/Resources/RINO/OnAir
 *
 *---------------------------------------------------------------*/


#ifndef RPACK_H
#define RPACK_H 1


#define RPACK_FRAME_LEN 168


#ifdef RPACK_C		/* Expose private details */


 
// Transmission order is LSB first. 

struct __attribute__((__packed__)) rpack_s {

	int lat;		// Latitude.
				// Signed integer.  Scaled by 2**30/90.

	int lon;		// Longitude.  Same encoding.

	char unknown1;		// Unproven theory: altitude.	
	char unknown2;		

	unsigned name0:6;	// 10 character name.
	unsigned name1:6;	// Bit packing is implementation dependent.
	unsigned name2:6;	// Should rewrite to be more portable.
	unsigned name3:6;
	unsigned name4:6;
	unsigned name5:6;
	unsigned name6:6;
	unsigned name7:6;
	unsigned name8:6;
	unsigned name9:6;

	unsigned symbol:5;	

	unsigned unknown3:7;		
				
	
//	unsigned crc:16;	// Safe bet this is CRC for error checking.

	unsigned char crc1;
	unsigned char crc2;

	char dummy[3];		// Total size should be 24 bytes if no gaps.

};

#else			/* Show only public interface.  */


struct rpack_s {
	char stuff[24];
};


#endif



void rpack_set_bit (struct rpack_s *rp, int position, int value);

int rpack_is_valid (struct rpack_s *rp);

int rpack_get_bit (struct rpack_s *rp, int position);

double rpack_get_lat (struct rpack_s *rp);

double rpack_get_lon (struct rpack_s *rp);

int rpack_get_symbol (struct rpack_s *rp);

void rpack_get_name (struct rpack_s *rp, char *str);



#endif

/* end rpack.h */
	
