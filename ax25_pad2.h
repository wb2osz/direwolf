/*-------------------------------------------------------------------
 *
 * Name:	ax25_pad2.h
 *
 * Purpose:	Header file for using ax25_pad2.c
 *		ax25_pad dealt only with UI frames.
 *		This adds a facility for the other types: U, s, I.
 *
 *------------------------------------------------------------------*/

#ifndef AX25_PAD2_H
#define AX25_PAD2_H 1

#include "ax25_pad.h"




#if AX25MEMDEBUG	// to investigate a memory leak problem



packet_t ax25_u_frame_debug (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, ax25_frame_type_t ftype, int pf, int pid, unsigned char *pinfo, int info_len, char *src_file, int src_line);

packet_t ax25_s_frame_debug (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, ax25_frame_type_t ftype, int modulo, int nr, int pf, unsigned char *pinfo, int info_len, char *src_file, int src_line);

packet_t ax25_i_frame_debug (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, int modulo, int nr, int ns, int pf, int pid, unsigned char *pinfo, int info_len, char *src_file, int src_line);


#define ax25_u_frame(a,n,c,f,p,q,i,l) ax25_u_frame_debug(a,n,c,f,p,q,i,l,__FILE__,__LINE__)

#define ax25_s_frame(a,n,c,f,m,r,p,i,l) ax25_s_frame_debug(a,n,c,f,m,r,p,i,l,__FILE__,__LINE__)

#define ax25_i_frame(a,n,c,m,r,s,p,q,i,l) ax25_i_frame_debug(a,n,c,m,r,s,p,q,i,l,__FILE__,__LINE__)


#else

packet_t ax25_u_frame (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, ax25_frame_type_t ftype, int pf, int pid, unsigned char *pinfo, int info_len);

packet_t ax25_s_frame (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, ax25_frame_type_t ftype, int modulo, int nr, int pf, unsigned char *pinfo, int info_len);

packet_t ax25_i_frame (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, int modulo, int nr, int ns, int pf, int pid, unsigned char *pinfo, int info_len);


#endif




#endif /* AX25_PAD2_H */

/* end ax25_pad2.h */


