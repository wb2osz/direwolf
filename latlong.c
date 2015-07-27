//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013  John Langner, WB2OSZ
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


/*------------------------------------------------------------------
 *
 * Module:      latlong.c
 *
 * Purpose:   	Various functions for dealing with latitude and longitude.
 *		
 * Description: Originally, these were scattered around in many places.
 *		Over time they might all be gathered into one place
 *		for consistency, reuse, and easier maintenance.
 *
 *---------------------------------------------------------------*/


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <assert.h>

#include "direwolf.h"
#include "latlong.h"
#include "textcolor.h"


/*------------------------------------------------------------------
 *
 * Name:        latitude_to_str
 *
 * Purpose:     Convert numeric latitude to string for transmission.
 *
 * Inputs:      dlat		- Floating point degrees.
 * 		ambiguity	- If 1, 2, 3, or 4, blank out that many trailing digits.
 *
 * Outputs:	slat		- String in format ddmm.mm[NS]
 *
 * Returns:     None
 *
 *----------------------------------------------------------------*/

void latitude_to_str (double dlat, int ambiguity, char *slat)
{
	char hemi;	/* Hemisphere: N or S */
	int ideg;	/* whole number of degrees. */
	double dmin;	/* Minutes after removing degrees. */
	char smin[8];	/* Minutes in format mm.mm */
	
	if (dlat < -90.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Latitude is less than -90.  Changing to -90.n");
	  dlat = -90.;
	}
	if (dlat > 90.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Latitude is greater than 90.  Changing to 90.n");
	  dlat = 90.;
	}

	if (dlat < 0) {
	  dlat = (- dlat);
	  hemi = 'S';
	}
	else {
	  hemi = 'N';
	}

	ideg = (int)dlat;
	dmin = (dlat - ideg) * 60.;

	sprintf (smin, "%05.2f", dmin);
	/* Due to roundoff, 59.9999 could come out as "60.00" */
	if (smin[0] == '6') {
	  smin[0] = '0';
	  ideg++;
	}

	sprintf (slat, "%02d%s%c", ideg, smin, hemi);

	if (ambiguity >= 1) {
	  slat[6] = ' ';
	  if (ambiguity >= 2) {
	    slat[5] = ' ';
	    if (ambiguity >= 3) {
	      slat[3] = ' ';
	      if (ambiguity >= 4) {
	        slat[2] = ' ';
	      }
	    }
	  }
	}

} /* end latitude_to_str */


/*------------------------------------------------------------------
 *
 * Name:        longitude_to_str
 *
 * Purpose:     Convert numeric longitude to string for transmission.
 *
 * Inputs:      dlong		- Floating point degrees.
 * 		ambiguity	- If 1, 2, 3, or 4, blank out that many trailing digits.
 *
 * Outputs:	slat		- String in format dddmm.mm[NS]
 *
 * Returns:     None
 *
 *----------------------------------------------------------------*/

void longitude_to_str (double dlong, int ambiguity, char *slong)
{
	char hemi;	/* Hemisphere: N or S */
	int ideg;	/* whole number of degrees. */
	double dmin;	/* Minutes after removing degrees. */
	char smin[8];	/* Minutes in format mm.mm */
	
	if (dlong < -180.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Longitude is less than -180.  Changing to -180.n");
	  dlong = -180.;
	}
	if (dlong > 180.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Longitude is greater than 180.  Changing to 180.n");
	  dlong = 180.;
	}

	if (dlong < 0) {
	  dlong = (- dlong);
	  hemi = 'W';
	}
	else {
	  hemi = 'E';
	}

	ideg = (int)dlong;
	dmin = (dlong - ideg) * 60.;

	sprintf (smin, "%05.2f", dmin);
	/* Due to roundoff, 59.9999 could come out as "60.00" */
	if (smin[0] == '6') {
	  smin[0] = '0';
	  ideg++;
	}

	sprintf (slong, "%03d%s%c", ideg, smin, hemi);
/*
 * The spec says position ambiguity in latitude also
 * applies to longitude automatically.  
 * Blanking longitude digits is not necessary but I do it
 * because it makes things clearer.
 */
	if (ambiguity >= 1) {
	  slong[7] = ' ';
	  if (ambiguity >= 2) {
	    slong[6] = ' ';
	    if (ambiguity >= 3) {
	      slong[4] = ' ';
	      if (ambiguity >= 4) {
	        slong[3] = ' ';
	      }
	    }
	  }
	}

} /* end longitude_to_str */


/*------------------------------------------------------------------
 *
 * Name:        latitude_to_comp_str
 *
 * Purpose:     Convert numeric latitude to compressed string for transmission.
 *
 * Inputs:      dlat		- Floating point degrees.
 *
 * Outputs:	slat		- String in format yyyy.
 *
 *----------------------------------------------------------------*/

void latitude_to_comp_str (double dlat, char *clat)
{
	int y, y0, y1, y2, y3;

	if (dlat < -90.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Latitude is less than -90.  Changing to -90.n");
	  dlat = -90.;
	}
	if (dlat > 90.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Latitude is greater than 90.  Changing to 90.n");
	  dlat = 90.;
	}

	y = (int)round(380926. * (90. - dlat));
	
	y0 = y / (91*91*91);
	y -= y0 * (91*91*91);

	y1 = y / (91*91);
	y -= y1 * (91*91);

	y2 = y / (91);
	y -= y2 * (91);

	y3 = y;

	clat[0] = y0 + 33;
	clat[1] = y1 + 33;
	clat[2] = y2 + 33;
	clat[3] = y3 + 33;
}

/*------------------------------------------------------------------
 *
 * Name:        longitude_to_comp_str
 *
 * Purpose:     Convert numeric longitude to compressed string for transmission.
 *
 * Inputs:      dlong		- Floating point degrees.
 *
 * Outputs:	slat		- String in format xxxx.
 *
 *----------------------------------------------------------------*/

void longitude_to_comp_str (double dlong, char *clon)
{
	int x, x0, x1, x2, x3;

	if (dlong < -180.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Longitude is less than -180.  Changing to -180.n");
	  dlong = -180.;
	}
	if (dlong > 180.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Longitude is greater than 180.  Changing to 180.n");
	  dlong = 180.;
	}

	x = (int)round(190463. * (180. + dlong));
	
	x0 = x / (91*91*91);
	x -= x0 * (91*91*91);

	x1 = x / (91*91);
	x -= x1 * (91*91);

	x2 = x / (91);
	x -= x2 * (91);

	x3 = x;

	clon[0] = x0 + 33;
	clon[1] = x1 + 33;
	clon[2] = x2 + 33;
	clon[3] = x3 + 33;
}
