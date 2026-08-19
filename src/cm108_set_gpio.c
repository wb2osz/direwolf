//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014, 2016, 2017  John Langner, WB2OSZ
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



/*------------------------------------------------------------------
 *
 * Module:      cm108_set_gpio.c
 *
 * Purpose:   	Utility to set GPIO pins on a CM108.
 *
 * Description:	Test utility to set the GPIO pins on a CM108 USB sound device.
 *
 *---------------------------------------------------------------*/

#include <stdlib.h>

#include "cm108.h"
#include "textcolor.h"

void usage() {
	text_color_set(DW_COLOR_INFO);
	dw_printf("\n");
	dw_printf("cm108_set_gpio - Utility to set a CM108 GPIO pin.\n");
	dw_printf("\n");
	dw_printf("Usage:	cm108_set_gpio /dev/hidrawN PIN_NUMBER <0|1>\n");
	dw_printf("\n");
}

int main (int argc, char **argv) {
	if (argc != 4) {
		usage();
		return 1;
	}
	char* hidraw_filename = argv[1];
	int pin_num = atoi(argv[2]);
	int state = atoi(argv[3]);
	return cm108_set_gpio_pin(hidraw_filename, pin_num, state);
}
