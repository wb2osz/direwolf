#!/usr/bin/env python3
"""
Stream IQ samples from SDRplay RSP1 to direwolf via stdout.
Requires: python3-soapysdr
Install: sudo apt install python3-soapysdr

Usage: python3 sdrplay_to_direwolf.py [--freq MHz] [--ifgr dB] [--rfgr step] [--agc]
Example: python3 sdrplay_to_direwolf.py --freq 144.8 --ifgr 23 --rfgr 0
         python3 sdrplay_to_direwolf.py --freq 144.8 --agc

RSP1 Gain Settings:
  --ifgr: IF Gain Reduction, 20-59 dB (default: 23, optimal for APRS)
  --rfgr: RF Gain Reduction, 0-3 steps (default: 0, no reduction)
  --agc:  Enable hardware AGC (automatic gain control)
"""

import sys
import argparse
import numpy as np
import SoapySDR
from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_CF32

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='Stream IQ from SDRplay to direwolf')
    parser.add_argument('--freq', type=float, default=144.8, help='Frequency in MHz (default: 144.8)')
    parser.add_argument('--ifgr', type=int, default=23, help='IF Gain Reduction 20-59 for RSP1 (default: 23)')
    parser.add_argument('--rfgr', type=int, default=0, help='RF Gain Reduction 0-3 for RSP1 (default: 0)')
    parser.add_argument('--agc', action='store_true', help='Enable hardware AGC (default: disabled)')
    args = parser.parse_args()
    
    # Configuration
    frequency = args.freq * 1e6  # Convert MHz to Hz
    sample_rate = 192000  # 192 kHz (supported by RSP1). Downsample later in pipeline.
    ifgr = args.ifgr
    rfgr = args.rfgr
    use_agc = args.agc
    
    # Print to stderr so it doesn't interfere with IQ data on stdout
    def log(msg):
        print(msg, file=sys.stderr)
    
    log("SDRplay to Direwolf IQ streamer")
    log(f"Frequency: {frequency/1e6} MHz")
    log(f"Sample rate: {sample_rate} Hz")
    log(f"Hardware AGC: {'ON' if use_agc else 'OFF'}")
    if not use_agc:
        log(f"IF Gain Reduction (IFGR): {ifgr}")
        log(f"RF Gain Reduction (RFGR): {rfgr}")
    
    # Open SDRplay device
    args = dict(driver="sdrplay")
    sdr = SoapySDR.Device(args)
    
    # Configure device
    sdr.setSampleRate(SOAPY_SDR_RX, 0, sample_rate)
    sdr.setFrequency(SOAPY_SDR_RX, 0, frequency)
    
    # Set gain mode
    if sdr.hasGainMode(SOAPY_SDR_RX, 0):
        sdr.setGainMode(SOAPY_SDR_RX, 0, use_agc)  # True for AGC, False for manual
    
    # Set IF and RF gain reduction for RSP1 (only if AGC is off)
    if not use_agc:
        try:
            sdr.setGain(SOAPY_SDR_RX, 0, "IFGR", ifgr)
            sdr.setGain(SOAPY_SDR_RX, 0, "RFGR", rfgr)
        except:
            # Fallback to combined gain if individual settings fail
            log("Warning: Could not set IFGR/RFGR individually, using combined gain")
            sdr.setGain(SOAPY_SDR_RX, 0, ifgr)
    
    log(f"Actual sample rate: {sdr.getSampleRate(SOAPY_SDR_RX, 0)} Hz")
    log(f"Actual frequency: {sdr.getFrequency(SOAPY_SDR_RX, 0)/1e6} MHz")
    try:
        actual_ifgr = sdr.getGain(SOAPY_SDR_RX, 0, "IFGR")
        actual_rfgr = sdr.getGain(SOAPY_SDR_RX, 0, "RFGR")
        log(f"Actual IFGR: {actual_ifgr}, RFGR: {actual_rfgr}")
    except:
        log(f"Actual gain: {sdr.getGain(SOAPY_SDR_RX, 0)} dB")
    
    # Setup stream
    rx_stream = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32)
    sdr.activateStream(rx_stream)
    
    log("Streaming IQ samples to stdout (CF32 @ 192 kHz)...")
    log("Press Ctrl+C to stop")
    
    # Buffer for receiving samples
    buff = np.zeros(4096, dtype=np.complex64)
    
    try:
        while True:
            # Read samples
            sr = sdr.readStream(rx_stream, [buff], len(buff))
            num_samples = sr.ret
            
            if num_samples > 0:
                # Write to stdout as interleaved float32 (I,Q,I,Q,...) at 192 kHz
                iq_interleaved = np.empty(num_samples * 2, dtype=np.float32)
                iq_interleaved[0::2] = buff[:num_samples].real
                iq_interleaved[1::2] = buff[:num_samples].imag
                sys.stdout.buffer.write(iq_interleaved.tobytes())
                sys.stdout.buffer.flush()
    
    except KeyboardInterrupt:
        log("\nStopping...")
    
    finally:
        sdr.deactivateStream(rx_stream)
        sdr.closeStream(rx_stream)
        log("Stream closed")

if __name__ == "__main__":
    main()
