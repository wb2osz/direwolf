#!/usr/bin/env python3
import sys
import struct

# Read IQ samples and FM demodulate using same algorithm as our C code
import math

prev_phase = 0.0
for i in range(12000):
    data = sys.stdin.buffer.read(8)
    if len(data) < 8:
        break
    I, Q = struct.unpack('ff', data)
    
    phase = math.atan2(Q, I)
    dphase = phase - prev_phase
    
    # Unwrap
    while dphase < -math.pi:
        dphase += 2 * math.pi
    while dphase > math.pi:
        dphase -= 2 * math.pi
    
    # Output without /pi normalization
    sys.stdout.buffer.write(struct.pack('f', dphase))
    prev_phase = phase
