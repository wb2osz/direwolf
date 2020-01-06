//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2019  John Langner, WB2OSZ
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// -----------------------------------------------------------------------
//
//
// Most of this is based on:
//
// FX.25 Encoder
//	Author: Jim McGuire KB3MPL
//	Date: 	23 October 2007
//
// This program is a single-file implementation of the FX.25 encapsulation 
// structure for use with AX.25 data packets.  Details of the FX.25 
// specification are available at:
//     http://www.stensat.org/Docs/Docs.htm
//
// This program implements a single RS(255,239) FEC structure.  Future
// releases will incorporate more capabilities as accommodated in the FX.25
// spec.  
//
// The Reed Solomon encoding routines are based on work performed by
// Phil Karn.  Phil was kind enough to release his code under the GPL, as
// noted below.  Consequently, this FX.25 implementation is also released
// under the terms of the GPL.  
//
// Phil Karn's original copyright notice:
  /* Test the Reed-Solomon codecs
   * for various block sizes and with random data and random error patterns
   *
   * Copyright 2002 Phil Karn, KA9Q
   * May be used under the terms of the GNU General Public License (GPL)
   *
   */
                

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fx25.h"



void ENCODE_RS(struct rs * restrict rs, DTYPE * restrict data, DTYPE * restrict bb)
{

  int i, j;
  DTYPE feedback;

  memset(bb,0,NROOTS*sizeof(DTYPE)); // clear out the FEC data area

  for(i=0;i<NN-NROOTS;i++){
    feedback = INDEX_OF[data[i] ^ bb[0]];
    if(feedback != A0){      /* feedback term is non-zero */
      for(j=1;j<NROOTS;j++)
	    bb[j] ^= ALPHA_TO[MODNN(feedback + GENPOLY[NROOTS-j])];
    }
    /* Shift */
    memmove(&bb[0],&bb[1],sizeof(DTYPE)*(NROOTS-1));
    if(feedback != A0)
      bb[NROOTS-1] = ALPHA_TO[MODNN(feedback + GENPOLY[0])];
    else
      bb[NROOTS-1] = 0;
  }
}

// end fx25_encode.c