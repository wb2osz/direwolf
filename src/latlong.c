//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015  John Langner, WB2OSZ
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

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <assert.h>

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
 *				  Must always be exactly 8 characters + NUL.
 *				  Put in leading zeros if necessary.
 *				  We must have exactly ddmm.mm and hemisphere because
 *				  the APRS position report has fixed width fields.
 *				  Trailing digits can be blanked for position ambiguity.
 *
 * Returns:     None
 *
 * Idea for future:
 *		Non zero ambiguity removes least significant digits without rounding.
 *		Maybe we could use -1 and -2 to add extra digits using !DAO! as
 *		documented in http://www.aprs.org/datum.txt
 *
 *		For example, -1 adds one more human readable digit.
 *			lat minutes 12.345 would produce "12.34" and !W5 !
 *
 *		-2 would encode almost 2 digits in base 91.
 *			lat minutes 10.0027 would produce "10.00" and !w: !
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

	// dmin is known to be in range of 0 <= dmin < 60.

	// Minutes must be exactly like 99.99 with leading zeros,
	// if needed, to make it fixed width.
	// Two digits, decimal point, two digits, nul terminator.

	snprintf (smin, sizeof(smin), "%05.2f", dmin);
	/* Due to roundoff, 59.9999 could come out as "60.00" */
	if (smin[0] == '6') {
	  smin[0] = '0';
	  ideg++;
	}

	// Assumes slat can hold 8 characters + nul.
	// Degrees must be exactly 2 digits, with leading zero, if needed.

	// FIXME: Should pass in sizeof slat and use snprintf
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
 * Outputs:	slong		- String in format dddmm.mm[NS]
 *				  Must always be exactly 9 characters + NUL.
 *				  Put in leading zeros if necessary.
 *				  We must have exactly dddmm.mm and hemisphere because
 *				  the APRS position report has fixed width fields.
 *				  Trailing digits can be blanked for position ambiguity.
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

	snprintf (smin, sizeof(smin), "%05.2f", dmin);
	/* Due to roundoff, 59.9999 could come out as "60.00" */
	if (smin[0] == '6') {
	  smin[0] = '0';
	  ideg++;
	}

	// Assumes slong can hold 9 characters + nul.
	// Degrees must be exactly 3 digits, with leading zero, if needed.

	// FIXME: Should pass in sizeof slong and use snprintf
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
 *				  Exactly 4 bytes, no nul terminator.
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
 *				  Exactly 4 bytes, no nul terminator.
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


/*------------------------------------------------------------------
 *
 * Name:        latitude_to_nmea
 *
 * Purpose:     Convert numeric latitude to strings for NMEA sentence.
 *
 * Inputs:      dlat		- Floating point degrees.
 *
 * Outputs:	slat		- String in format ddmm.mmmm
 *		hemi		- Hemisphere or empty string.
 *
 * Returns:     None
 *
 *----------------------------------------------------------------*/

void latitude_to_nmea (double dlat, char *slat, char *hemi)
{
	int ideg;	/* whole number of degrees. */
	double dmin;	/* Minutes after removing degrees. */
	char smin[10];	/* Minutes in format mm.mmmm */
	
	if (dlat == G_UNKNOWN) {
	  strcpy (slat, "");
	  strcpy (hemi, "");
	  return;
	}

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
	  strcpy (hemi, "S");
	}
	else {
	  strcpy (hemi, "N");
	}

	ideg = (int)dlat;
	dmin = (dlat - ideg) * 60.;

	snprintf (smin, sizeof(smin), "%07.4f", dmin);
	/* Due to roundoff, 59.99999 could come out as "60.0000" */
	if (smin[0] == '6') {
	  smin[0] = '0';
	  ideg++;
	}

	// FIXME: Should pass in sizeof slat and use snprintf
	sprintf (slat, "%02d%s", ideg, smin);

} /* end latitude_to_str */


/*------------------------------------------------------------------
 *
 * Name:        longitude_to_nmea
 *
 * Purpose:     Convert numeric longitude to strings for NMEA sentence.
 *
 * Inputs:      dlong		- Floating point degrees.
 *
 * Outputs:	slong		- String in format dddmm.mmmm
 *		hemi		- Hemisphere or empty string.
 *
 * Returns:     None
 *
 *----------------------------------------------------------------*/

void longitude_to_nmea (double dlong, char *slong, char *hemi)
{
	int ideg;	/* whole number of degrees. */
	double dmin;	/* Minutes after removing degrees. */
	char smin[10];	/* Minutes in format mm.mmmm */
	
	if (dlong == G_UNKNOWN) {
	  strcpy (slong, "");
	  strcpy (hemi, "");
	  return;
	}

	if (dlong < -180.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("longitude is less than -180.  Changing to -180.n");
	  dlong = -180.;
	}
	if (dlong > 180.) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("longitude is greater than 180.  Changing to 180.n");
	  dlong = 180.;
	}

	if (dlong < 0) {
	  dlong = (- dlong);
	  strcpy (hemi, "W");
	}
	else {
	  strcpy (hemi, "E");
	}

	ideg = (int)dlong;
	dmin = (dlong - ideg) * 60.;

	snprintf (smin, sizeof(smin), "%07.4f", dmin);
	/* Due to roundoff, 59.99999 could come out as "60.0000" */
	if (smin[0] == '6') {
	  smin[0] = '0';
	  ideg++;
	}

	// FIXME: Should pass in sizeof slong and use snprintf
	sprintf (slong, "%03d%s", ideg, smin);

} /* end longitude_to_nmea */



/*------------------------------------------------------------------
 *
 * Function:	latitude_from_nmea
 *
 * Purpose:	Convert NMEA latitude encoding to degrees.
 *
 * Inputs:	pstr 	- Pointer to numeric string.
 *		phemi	- Pointer to following field.  Should be N or S.
 *
 * Returns:	Double precision value in degrees.  Negative for South.
 *
 * Description:	Latitude field has
 *			2 digits for degrees
 *			2 digits for minutes
 *			period
 *			Variable number of fractional digits for minutes.
 *			I've seen 2, 3, and 4 fractional digits.
 *
 *
 * Bugs:	Very little validation of data.
 *
 * Errors:	Return constant G_UNKNOWN for any type of error.
 *
 *------------------------------------------------------------------*/


double latitude_from_nmea (char *pstr, char *phemi)
{

	double lat;

	if ( ! isdigit((unsigned char)(pstr[0]))) return (G_UNKNOWN);

	if (pstr[4] != '.') return (G_UNKNOWN);


	lat = (pstr[0] - '0') * 10 + (pstr[1] - '0') + atof(pstr+2) / 60.0;

	if (lat < 0 || lat > 90) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: Latitude not in range of 0 to 90.\n");	  
	}

	// Saw this one time:
	//	$GPRMC,000000,V,0000.0000,0,00000.0000,0,000,000,000000,,*01

	// If location is unknown, I think the hemisphere should be
	// an empty string.  TODO: Check on this.
	// 'V' means void, so sentence should be discarded rather than
	// trying to extract any data from it.

	if (*phemi != 'N' && *phemi != 'S' && *phemi != '\0') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: Latitude hemisphere should be N or S.\n");	  
	}

	if (*phemi == 'S') lat = ( - lat);

	return (lat);
}




/*------------------------------------------------------------------
 *
 * Function:	longitude_from_nmea
 *
 * Purpose:	Convert NMEA longitude encoding to degrees.
 *
 * Inputs:	pstr 	- Pointer to numeric string.
 *		phemi	- Pointer to following field.  Should be E or W.
 *
 * Returns:	Double precision value in degrees.  Negative for West.
 *
 * Description:	Longitude field has
 *			3 digits for degrees
 *			2 digits for minutes
 *			period
 *			Variable number of fractional digits for minutes
 *
 *
 * Bugs:	Very little validation of data.
 *
 * Errors:	Return constant G_UNKNOWN for any type of error.
 *
 *------------------------------------------------------------------*/


double longitude_from_nmea (char *pstr, char *phemi)
{
	double lon;

	if ( ! isdigit((unsigned char)(pstr[0]))) return (G_UNKNOWN);

	if (pstr[5] != '.') return (G_UNKNOWN);

	lon = (pstr[0] - '0') * 100 + (pstr[1] - '0') * 10 + (pstr[2] - '0') + atof(pstr+3) / 60.0;

	if (lon < 0 || lon > 180) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: Longitude not in range of 0 to 180.\n");	  
	}
	
	if (*phemi != 'E' && *phemi != 'W' && *phemi != '\0') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Error: Longitude hemisphere should be E or W.\n");	  
	}

	if (*phemi == 'W') lon = ( - lon);

	return (lon);
}


/*------------------------------------------------------------------
 *
 * Function:	ll_distance_km
 *
 * Purpose:	Calculate distance between two locations.
 *
 * Inputs:	lat1, lon1	- One location, in degrees.
 *		lat2, lon2	- other location
 *
 * Returns:	Distance in km.
 *
 * Description:	The Ubiquitous Haversine formula.
 *
 *------------------------------------------------------------------*/

#define R 6371

double ll_distance_km (double lat1, double lon1, double lat2, double lon2)
{
	double a;

	lat1 *= M_PI / 180;
	lon1 *= M_PI / 180;	
	lat2 *= M_PI / 180;	
	lon2 *= M_PI / 180;	
	
	a = pow(sin((lat2-lat1)/2),2) + cos(lat1) * cos(lat2) * pow(sin((lon2-lon1)/2),2);

	return (R * 2 *atan2(sqrt(a), sqrt(1-a)));
}


/*------------------------------------------------------------------
 *
 * Function:	ll_bearing_deg
 *
 * Purpose:	Calculate bearing between two locations.
 *
 * Inputs:	lat1, lon1	- starting location, in degrees.
 *		lat2, lon2	- destination location
 *
 * Returns:	Initial Bearing, in degrees.
 *		The calculation produces Range +- 180 degrees.
 *		But I think that 0 - 360 would be more customary?
 *
 *------------------------------------------------------------------*/

double ll_bearing_deg (double lat1, double lon1, double lat2, double lon2)
{
	double b;

	lat1 *= M_PI / 180;
	lon1 *= M_PI / 180;
	lat2 *= M_PI / 180;
	lon2 *= M_PI / 180;

	b = atan2 (sin(lon2-lon1) * cos(lat2),
		cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(lon2-lon1));

	b *= 180 / M_PI;
	if (b < 0) b += 360;

	return (b);
}


/*------------------------------------------------------------------
 *
 * Function:	ll_dest_lat
 *		ll_dest_lon
 *
 * Purpose:	Calculate the destination location given a starting point,
 *		distance, and bearing,
 *
 * Inputs:	lat1, lon1	- starting location, in degrees.
 *		dist		- distance in km.
 *		bearing		- direction in degrees.  Shouldn't matter
 *				  if it is in +- 180 or 0 to 360 range.
 *
 * Returns:	New latitude or longitude.
 *
 *------------------------------------------------------------------*/

double ll_dest_lat (double lat1, double lon1, double dist, double bearing)
{
	double lat2;

	lat1 *= M_PI / 180;	// Everything to radians.
	lon1 *= M_PI / 180;
	bearing *= M_PI / 180;

	lat2 = asin(sin(lat1) * cos(dist/R) + cos(lat1) * sin(dist/R) * cos(bearing));

	lat2 *= 180 / M_PI;	// Back to degrees.

	return (lat2);
}

double ll_dest_lon (double lat1, double lon1, double dist, double bearing)
{
	double lon2;
	double lat2;

	lat1 *= M_PI / 180;	// Everything to radians.
	lon1 *= M_PI / 180;
	bearing *= M_PI / 180;

	lat2 = asin(sin(lat1) * cos(dist/R) + cos(lat1) * sin(dist/R) * cos(bearing));

	lon2 = lon1 + atan2(sin(bearing) * sin(dist/R) * cos(lat1), cos(dist/R) - sin(lat1) * sin(lat2));

	lon2 *= 180 / M_PI;	// Back to degrees.

	return (lon2);
}



/*------------------------------------------------------------------
 *
 * Function:	ll_from_grid_square
 *
 * Purpose:	Convert Maidenhead locator to latitude and longitude.
 *
 * Inputs:	maidenhead	- 2, 4, 6, 8, 10, or 12 character grid square locator.
 *
 * Outputs:	dlat, dlon	- Latitude and longitude.  
 *				  Original values unchanged if error.
 *
 * Returns:	1 for success, 0 if error.
 *
 * Reference:	A good converter for spot checking.  Only handles 4 or 6 characters :-(
 *		http://home.arcor.de/waldemar.kebsch/The_Makrothen_Contest/fmaidenhead.html
 *
 * Rambling:	What sort of resolution does this provide?
 *		For 8 character form, each latitude unit is 0.25 minute.
 *		(Longitude can be up to twice that around the equator.)
 *		6371 km * 2 * pi * 0.25 / 60 / 360 = 0.463 km.  Is that right?
 *
 *		Using this calculator, http://www.earthpoint.us/Convert.aspx
 *		It gives lower left corner of square rather than the middle.  :-(
 *
 *		FN42MA00  -->  19T 334361mE 4651711mN
 *		FN42MA11  -->  19T 335062mE 4652157mN
 *				   ------   -------
 *				      701       446    meters difference.
 *
 *		With another two pairs, we are down around 2 meters for latitude.
 *
 *------------------------------------------------------------------*/

#define MH_MIN_PAIR 1
#define MH_MAX_PAIR 6
#define MH_UNITS ( 18 * 10 * 24 * 10 * 24 * 10 * 2 )

static const struct {
	char *position;
	char min_ch;
	char max_ch;
	int value;
} mh_pair[MH_MAX_PAIR] = {
	{ "first",    'A', 'R',  10 * 24 * 10 * 24 * 10 * 2 },
	{ "second",   '0', '9',       24 * 10 * 24 * 10 * 2 },
	{ "third",    'A', 'X',            10 * 24 * 10 * 2 },
	{ "fourth",   '0', '9',                 24 * 10 * 2 },
	{ "fifth",    'A', 'X',                      10 * 2 },
	{ "sixth",    '0', '9',                           2 } };  // Even so we can get center of square.



#if 1

int ll_from_grid_square (char *maidenhead, double *dlat, double *dlon)
{
	char mh[16];			/* Local copy, changed to upper case. */
	int ilat = 0, ilon = 0;		/* In units in table above. */
	char *p;
	int n;

	int np = strlen(maidenhead) / 2;	/* Number of pairs of characters. */

	if (strlen(maidenhead) %2 != 0 || np < MH_MIN_PAIR || np > MH_MAX_PAIR) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Maidenhead locator \"%s\" must from 1 to %d pairs of characters.\n", maidenhead, MH_MAX_PAIR);
	  return (0);
	}

	strlcpy (mh, maidenhead, sizeof(mh));
	for (p = mh; *p != '\0'; p++) {
	  if (islower(*p)) *p = toupper(*p);
	}

	for (n = 0; n < np; n++) {

	  if (mh[2*n]   < mh_pair[n].min_ch || mh[2*n]   > mh_pair[n].max_ch || 
	      mh[2*n+1] < mh_pair[n].min_ch || mh[2*n+1] > mh_pair[n].max_ch) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("The %s pair of characters in Maidenhead locator \"%s\" must be in range of %c thru %c.\n", 
			mh_pair[n].position, maidenhead, mh_pair[n].min_ch, mh_pair[n].max_ch);
	    return (0);
	  }

	  ilon +=  ( mh[2*n]   - mh_pair[n].min_ch ) * mh_pair[n].value;
	  ilat +=  ( mh[2*n+1] - mh_pair[n].min_ch ) * mh_pair[n].value;

	  if (n == np-1) {	// If last pair, take center of square.
	    ilon += mh_pair[n].value / 2;
	    ilat += mh_pair[n].value / 2;
	  }
	}

	*dlat = (double)ilat / MH_UNITS * 180. - 90.;
	*dlon = (double)ilon / MH_UNITS * 360. - 180.;

	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf("DEBUG: Maidenhead conversion \"%s\" -> %.6f %.6f\n", maidenhead, *dlat, *dlon);

	return (1);
}
#else

int ll_from_grid_square (char *maidenhead, double *dlat, double *dlon)
{
	double lat, lon;
	char mh[16];


	if (strlen(maidenhead) != 2 &&  strlen(maidenhead) != 4 &&  strlen(maidenhead) != 6 &&  strlen(maidenhead) != 8) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("Maidenhead locator \"%s\" must 2, 4, 6, or 8 characters.\n", maidenhead);
	  return (0);
	}

	strcpy (mh, maidenhead);
	if (islower(mh[0])) mh[0] = toupper(mh[0]);
	if (islower(mh[1])) mh[1] = toupper(mh[1]);
	
	if (mh[0] < 'A' || mh[0] > 'R' || mh[1] < 'A' || mh[1] > 'R') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("The first pair of characters in Maidenhead locator \"%s\" must be in range of A thru R.\n", maidenhead);
	  return (0);
	}


	/* Lon:  360 deg / 18 squares = 20 deg / square */
	/* Lat:  180 deg / 18 squares = 10 deg / square */

	lon = (mh[0] - 'A') * 20 - 180;
	lat = (mh[1] - 'A') * 10 - 90;

	if (strlen(mh) >= 4) {

	  if ( ! isdigit(mh[2]) || ! isdigit(mh[3]) ) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("The second pair of characters in Maidenhead locator \"%s\" must be digits.\n", maidenhead);
	    return (0);
	  }

	  /* Lon:  20 deg / 10 squares = 2 deg / square */
	  /* Lat:  10 deg / 10 squares = 1 deg / square */

	  lon += (mh[2] - '0') * 2;
	  lat += (mh[3] - '0');


	  if (strlen(mh) >=6) {

	    if (islower(mh[4])) mh[4] = toupper(mh[4]);
	    if (islower(mh[5])) mh[5] = toupper(mh[5]);

	    if (mh[4] < 'A' || mh[4] > 'X' || mh[5] < 'A' || mh[5] > 'X') {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("The third pair of characters in Maidenhead locator \"%s\" must be in range of A thru X.\n", maidenhead);
	      return (0);
	    }

	    /* Lon:  2 deg / 24 squares = 5 minutes / square */
	    /* Lat:  1 deg / 24 squares = 2.5 minutes / square */

	    lon += (mh[4] - 'A') * 5.0 / 60.0;
	    lat += (mh[5] - 'A') * 2.5 / 60.0;

	    if (strlen(mh) >= 8) {

	      if ( ! isdigit(mh[6]) || ! isdigit(mh[7]) ) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf("The fourth pair of characters in Maidenhead locator \"%s\" must be digits.\n", maidenhead);
	        return (0);
	      }

	      /* Lon:  5   min / 10 squares = 0.5 minutes / square */
	      /* Lat:  2.5 min / 10 squares = 0.25 minutes / square */

	      lon += (mh[6] - '0') * 0.50 / 60.0;
	      lat += (mh[7] - '0') * 0.25 / 60.0;

	      lon += 0.250 / 60.0;	/* Move from corner to center of square */
	      lat += 0.125 / 60.0;
	    }
	    else {
	      lon += 2.5  / 60.0;	/* Move from corner to center of square */
	      lat += 1.25 / 60.0;
	    }
	  }
	  else {
	    lon += 1.0;	/* Move from corner to center of square */
	    lat += 0.5;
	  }
	}
	else {
	  lon += 10;	/* Move from corner to center of square */
	  lat += 5;
	}

 	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf("DEBUG: Maidenhead conversion \"%s\" -> %.6f %.6f\n", maidenhead, lat, lon);

	*dlat = lat;
	*dlon = lon;

	return (1);
}

#endif

/* end ll_from_grid_square */


#if LLTEST

/* gcc -o lltest -DLLTEST latlong.c textcolor.o misc.a && lltest */


int main (int argc, char *argv[])
{
	char result[20];
	int errors = 0;
	int ok;
	double dlat, dlon;
	double d, b;

/* Latitude to APRS format. */

	latitude_to_str (45.25, 0, result);
	if (strcmp(result, "4515.00N") != 0) { errors++; dw_printf ("Error 1.1: Did not expect \"%s\"\n", result); }

	latitude_to_str (-45.25, 0, result);
	if (strcmp(result, "4515.00S") != 0) { errors++; dw_printf ("Error 1.2: Did not expect \"%s\"\n", result); }


	latitude_to_str (45.999830, 0, result);
	if (strcmp(result, "4559.99N") != 0) { errors++; dw_printf ("Error 1.3: Did not expect \"%s\"\n", result); }

	latitude_to_str (45.99999, 0, result);
	if (strcmp(result, "4600.00N") != 0) { errors++; dw_printf ("Error 1.4: Did not expect \"%s\"\n", result); }


	latitude_to_str (45.999830, 1, result);
	if (strcmp(result, "4559.9 N") != 0) { errors++; dw_printf ("Error 1.5: Did not expect \"%s\"\n", result); }

	latitude_to_str (45.999830, 2, result);
	if (strcmp(result, "4559.  N") != 0) { errors++; dw_printf ("Error 1.6: Did not expect \"%s\"\n", result); }

	latitude_to_str (45.999830, 3, result);
	if (strcmp(result, "455 .  N") != 0) { errors++; dw_printf ("Error 1.7: Did not expect \"%s\"\n", result); }

	latitude_to_str (45.999830, 4, result);
	if (strcmp(result, "45  .  N") != 0) { errors++; dw_printf ("Error 1.8: Did not expect \"%s\"\n", result); }

	// Test for leading zeros for small values.  Result must be fixed width.

	latitude_to_str (0.016666666, 0, result);
	if (strcmp(result, "0001.00N") != 0) { errors++; dw_printf ("Error 1.9: Did not expect \"%s\"\n", result); }

	latitude_to_str (-1.999999, 0, result);
	if (strcmp(result, "0200.00S") != 0) { errors++; dw_printf ("Error 1.10: Did not expect \"%s\"\n", result); }

/* Longitude to APRS format. */

	longitude_to_str (45.25, 0, result);
	if (strcmp(result, "04515.00E") != 0) { errors++; dw_printf ("Error 2.1: Did not expect \"%s\"\n", result); }

	longitude_to_str (-45.25, 0, result);
	if (strcmp(result, "04515.00W") != 0) { errors++; dw_printf ("Error 2.2: Did not expect \"%s\"\n", result); }


	longitude_to_str (45.999830, 0, result);
	if (strcmp(result, "04559.99E") != 0) { errors++; dw_printf ("Error 2.3: Did not expect \"%s\"\n", result); }

	longitude_to_str (45.99999, 0, result);
	if (strcmp(result, "04600.00E") != 0) { errors++; dw_printf ("Error 2.4: Did not expect \"%s\"\n", result); }


	longitude_to_str (45.999830, 1, result);
	if (strcmp(result, "04559.9 E") != 0) { errors++; dw_printf ("Error 2.5: Did not expect \"%s\"\n", result); }

	longitude_to_str (45.999830, 2, result);
	if (strcmp(result, "04559.  E") != 0) { errors++; dw_printf ("Error 2.6: Did not expect \"%s\"\n", result); }

	longitude_to_str (45.999830, 3, result);
	if (strcmp(result, "0455 .  E") != 0) { errors++; dw_printf ("Error 2.7: Did not expect \"%s\"\n", result); }

	longitude_to_str (45.999830, 4, result);
	if (strcmp(result, "045  .  E") != 0) { errors++; dw_printf ("Error 2.8: Did not expect \"%s\"\n", result); }

	// Test for leading zeros for small values.  Result must be fixed width.

	longitude_to_str (0.016666666, 0, result);
	if (strcmp(result, "00001.00E") != 0) { errors++; dw_printf ("Error 2.9: Did not expect \"%s\"\n", result); }

	longitude_to_str (-1.999999, 0, result);
	if (strcmp(result, "00200.00W") != 0) { errors++; dw_printf ("Error 2.10: Did not expect \"%s\"\n", result); }


/* Compressed format. */
/* Protocol spec example has <*e7 but I got <*e8 due to rounding rather than truncation to integer. */

	memset(result, 0, sizeof(result));

	latitude_to_comp_str (-90.0, result);
	if (strcmp(result, "{{!!") != 0) { errors++; dw_printf ("Error 3.1: Did not expect \"%s\"\n", result); }

	latitude_to_comp_str (49.5, result);
	if (strcmp(result, "5L!!") != 0) { errors++; dw_printf ("Error 3.2: Did not expect \"%s\"\n", result); }

	latitude_to_comp_str (90.0, result);
	if (strcmp(result, "!!!!") != 0) { errors++; dw_printf ("Error 3.3: Did not expect \"%s\"\n", result); }


	longitude_to_comp_str (-180.0, result);
	if (strcmp(result, "!!!!") != 0) { errors++; dw_printf ("Error 3.4: Did not expect \"%s\"\n", result); }

	longitude_to_comp_str (-72.75, result);
	if (strcmp(result, "<*e8") != 0) { errors++; dw_printf ("Error 3.5: Did not expect \"%s\"\n", result); }

	longitude_to_comp_str (180.0, result);
	if (strcmp(result, "{{!!") != 0) { errors++; dw_printf ("Error 3.6: Did not expect \"%s\"\n", result); }

// to be continued for others...  NMEA...


/* Distance & bearing - Take a couple examples from other places and see if we get similar results. */

	// http://www.movable-type.co.uk/scripts/latlong.html

	d = ll_distance_km (35., 45., 35., 135.);
	b = ll_bearing_deg (35., 45., 35., 135.);

	if (d < 7862 || d > 7882) { errors++; dw_printf ("Error 5.1: Did not expect distance %.1f\n", d); }

	if (b < 59.7 || b > 60.3) { errors++; dw_printf ("Error 5.2: Did not expect bearing %.1f\n", b); }

	// Sydney to Kinsale.  https://woodshole.er.usgs.gov/staffpages/cpolloni/manitou/ccal.htm

	d = ll_distance_km (-33.8688, 151.2093, 51.7059, -8.5222);
	b = ll_bearing_deg (-33.8688, 151.2093, 51.7059, -8.5222);

	if (d < 17435 || d > 17455) { errors++; dw_printf ("Error 5.3: Did not expect distance %.1f\n", d); }

	if (b < 327-1 || b > 327+1) { errors++; dw_printf ("Error 5.4: Did not expect bearing %.1f\n", b); }


/*
 * More distance and bearing.
 * Here we will start at some location1 (lat1,lon1) and go some distance (d1) at some bearing (b1).
 * This results in a new location2 (lat2, lon2).
 * We then calculate the distance and bearing from location1 to location2 and compare with the intention.
 */
	int lat1, lon1, d1 = 10, b1;
	double lat2, lon2, d2, b2;

	for (lat1 = -60; lat1 <= 60; lat1 += 30) {
	  for (lon1 = -180; lon1 <= 180; lon1 +=30) {
	    for (b1 = 0; b1 < 360; b1 += 15) {

	      lat2 = ll_dest_lat ((double)lat1, (double)lon1, (double)d1, (double)b1);
	      lon2 = ll_dest_lon ((double)lat1, (double)lon1, (double)d1, (double)b1);

	      d2 = ll_distance_km ((double)lat1, (double)lon1, lat2, lon2);
	      b2 = ll_bearing_deg ((double)lat1, (double)lon1, lat2, lon2);
	      if (b2 > 359.9 && b2 < 360.1) b2 = 0;

	      // must be within 0.1% of distance and 0.1 degree.
	      if (d2 < 0.999 * d1 || d2 > 1.001 * d1) { errors++; dw_printf ("Error 5.8: lat1=%d, lon2=%d, d1=%d, b1=%d, d2=%.2f\n", lat1, lon1, d1, b1, d2); }
	      if (b2 < b1 - 0.1 || b2 > b1 + 0.1)       { errors++; dw_printf ("Error 5.9: lat1=%d, lon2=%d, d1=%d, b1=%d, b2=%.2f\n", lat1, lon1, d1, b1, b2); }
	    }
	  }
	}


/* Maidenhead locator to lat/long. */


	ok = ll_from_grid_square ("BL11", &dlat, &dlon);
	if (!ok || dlat < 20.4999999 || dlat > 21.5000001 || dlon < -157.0000001 || dlon > -156.9999999) { errors++; dw_printf ("Error 7.1: Did not expect %.6f %.6f\n", dlat, dlon); }

	ok = ll_from_grid_square ("BL11BH", &dlat, &dlon);
	if (!ok || dlat < 21.31249 || dlat > 21.31251 || dlon < -157.87501 || dlon > -157.87499) { errors++; dw_printf ("Error 7.2: Did not expect %.6f %.6f\n", dlat, dlon); }

#if 0		// TODO: add more test cases after comparing results with other cconverters.
		// Many other converters are limited to smaller number of characters,
		// or return corner rather than center of square, or return 3 decimal places for degrees.

	ok = ll_from_grid_square ("BL11BH16", &dlat, &dlon);
	if (!ok || dlat < 21.? || dlat > 21.? || dlon < -157.? || dlon > -157.?) { errors++; dw_printf ("Error 7.3: Did not expect %.6f %.6f\n", dlat, dlon); }

	ok = ll_from_grid_square ("BL11BH16oo", &dlat, &dlon);
	if (!ok || dlat < 21.? || dlat > 21.? || dlon < -157.? || dlon > -157.?) { errors++; dw_printf ("Error 7.4: Did not expect %.6f %.6f\n", dlat, dlon); }

	ok = ll_from_grid_square ("BL11BH16oo66", &dlat, &dlon);
	if (!ok || dlat < 21.? || dlat > 21.? || dlon < -157.? || dlon > -157.?) { errors++; dw_printf ("Error 7.5: Did not expect %.6f %.6f\n", dlat, dlon); }
#endif
        if (errors > 0) {
          text_color_set (DW_COLOR_ERROR);
          dw_printf ("\nLocation Coordinate Conversion Test - FAILED!\n");
          exit (EXIT_FAILURE);
        }
        text_color_set (DW_COLOR_REC);
        dw_printf ("\nLocation Coordinate Conversion Test - SUCCESS!\n");
        exit (EXIT_SUCCESS);

}



#endif


/* end latlong.c */