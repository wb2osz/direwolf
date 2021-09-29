#!/usr/bin/python3

# Part of Dire Wolf APRS Telemetry Toolkit, WB2OSZ, 2015

# Derived from
# https://github.com/adafruit/Adafruit-Raspberry-Pi-Python-Code/blob/master/Adafruit_ADS1x15/ads1x15_ex_singleended.py

import time, signal, sys
from Adafruit_ADS1x15 import ADS1x15

ADS1015 = 0x00  # 12-bit ADC
ADS1115 = 0x01  # 16-bit ADC

# Set ADC full scale to 2048 mV.
# Can't use original 4096 with 3.3V supply.
gain = 2048

# Select the sample time, 1/sps second.
# Longer is better to average out noise.
sps = 64

# Set this to ADS1015 or ADS1115 depending on the ADC you are using!
adc = ADS1x15(ic=ADS1115)

# Values for voltage divider on ADC input.
r1 = 1000000.
r2 = 100000.

# Read channel 0 in single-ended mode using the settings above
volts = adc.readADCSingleEnded(0, gain, sps) * 0.001 * (r1+r2) / r2

# Calibration correction specific to my hardware.
# (multiply by expected value, divide by uncalibrated result.)
#volts = volts * 4.98 / 4.889

print("%.3f" % (volts))
