#!/bin/bash
# Test script for IQ FM demodulation
# Generates a simple IQ test signal and pipes it to direwolf

# This script will generate a complex sine wave (IQ signal) representing
# a 1200 Hz AFSK tone modulated on FM

echo "Testing IQ FM demodulation..."
echo "This is a basic test that will generate 1 second of IQ data"
echo "representing a 1200 Hz tone on FM at 48 kHz sample rate"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Generate 1 second of test data: 
# - 48000 samples per second
# - Carrier offset of 0 Hz (baseband)
# - 1200 Hz audio tone
# - FM deviation of 3000 Hz

python3 << 'EOF'
import sys
import math
import struct

sample_rate = 48000
duration = 1.0  # seconds
audio_freq = 1200.0  # Hz - APRS mark tone
fm_deviation = 3000.0  # Hz

num_samples = int(sample_rate * duration)

for i in range(num_samples):
    t = i / sample_rate
    
    # Generate audio signal (1200 Hz sine wave)
    audio = math.sin(2.0 * math.pi * audio_freq * t)
    
    # FM modulate: phase = integral of (2*pi*f_deviation*audio)
    # For simplicity, we approximate the integral as a sum
    phase = 2.0 * math.pi * fm_deviation * audio * (1.0 / sample_rate)
    phase_accum = phase * i  # Simple phase accumulator
    
    # Generate I and Q components
    I = math.cos(phase_accum)
    Q = math.sin(phase_accum)
    
    # Write as float32 little-endian
    sys.stdout.buffer.write(struct.pack('<ff', I, Q))

EOF
