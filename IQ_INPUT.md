# IQ Input with FM Demodulation for Direwolf

This feature allows Direwolf to accept IQ (In-phase/Quadrature) samples from Software Defined Radios (SDR) and perform FM demodulation internally before AFSK demodulation.

## Configuration

To use IQ input, set your audio device to `iq:<sample_rate>` in your configuration file:

```
ADEVICE iq:48000 null
```

Where `48000` is the IQ sample rate in Hz. Common values are:
- 48000 (default if not specified)
- 96000
- 192000

## Input Format

Direwolf expects IQ samples from stdin in the following format:
- **Data type**: 32-bit floating point (float32)
- **Layout**: Interleaved I/Q pairs (I₀, Q₀, I₁, Q₁, I₂, Q₂, ...)
- **Byte order**: Little-endian
- **Value range**: Typically -1.0 to +1.0 (but can be auto-scaled)

## Usage with rx_sdr

The original use case with `rx_sdr` and `csdr`:

```bash
rx_sdr -d "driver=sdrplay,serial=0000000001" \
       -f 144.800M -s 192000 \
       -t AGC=off,IFGR=50,RFGR=0,BW=120000 \
       -g 30 -F CS16 - | \
csdr convert_s16_f | \
csdr fir_decimate_cc 4 | \
direwolf -c direwolf.conf
```

This pipeline:
1. `rx_sdr`: Captures IQ at 192 kHz as 16-bit signed integers
2. `csdr convert_s16_f`: Converts to float32
3. `csdr fir_decimate_cc 4`: Decimates by 4 → 48 kHz output
4. `direwolf`: Receives 48 kHz IQ, demodulates FM, then decodes APRS

## Configuration File Example

```
# IQ input at 48 kHz
ADEVICE iq:48000 null

# Channel 0 - Standard 1200 baud AFSK APRS
CHANNEL 0
MYCALL YOUR-CALL
MODEM 1200

# No transmit in receive-only mode
PTT NONE
```

## How It Works

1. **IQ Input**: Direwolf reads complex float samples from stdin
2. **FM Demodulation**: Uses quadrature demodulation (phase difference method)
   - Calculates phase difference between consecutive IQ samples
   - Phase difference is proportional to instantaneous frequency
   - Applies DC blocking and de-emphasis filters
3. **Audio Output**: Produces real audio samples (mono, 16-bit)
4. **AFSK Demodulation**: Standard Direwolf AFSK/packet demodulation

## FM Demodulator Parameters

The FM demodulator uses these internal parameters:
- **Maximum deviation**: 5000 Hz (suitable for narrow FM APRS)
- **DC blocking**: High-pass filter to remove DC offset
- **De-emphasis**: Low-pass filter (standard FM de-emphasis)

## Testing

A test script is provided to generate a simple IQ test signal:

```bash
./test-iq-signal.sh | direwolf -c test-iq.conf
```

## Troubleshooting

**No packets decoded:**
- Verify IQ sample rate matches configuration
- Check FM deviation (5 kHz works for standard narrow FM)
- Ensure proper frequency tuning (should be centered on APRS frequency)
- Verify gain settings on SDR aren't clipping or too low

**"End of IQ stream on stdin" message:**
- Input pipeline terminated
- Check rx_sdr or csdr commands for errors

**Build errors:**
- Ensure `fm_demod.c` is in CMakeLists.txt
- Math library should be automatically linked

## Technical Details

### FM Demodulation Algorithm

The demodulator uses the phase difference method:

```
φ_diff = atan2(I[n]·Q[n-1] - Q[n]·I[n-1], I[n]·I[n-1] + Q[n]·Q[n-1])
audio = φ_diff · (sample_rate / (2π · max_deviation))
```

This is mathematically equivalent to:
```
φ_diff = atan2(Q[n], I[n]) - atan2(Q[n-1], I[n-1])
```

But more numerically stable and avoids phase wrapping issues.

### Sample Rate Considerations

The IQ sample rate should be at least 2× the FM bandwidth. For APRS:
- FM bandwidth ≈ 12.5 kHz (narrow FM)
- Minimum IQ rate: ~25 kHz
- Recommended: 48 kHz or higher for good audio quality

## Future Enhancements

Possible improvements:
- UDP input for IQ samples (in addition to stdin)
- Configurable FM deviation
- Configurable de-emphasis time constant
- Support for wider FM modes
- AGC on IQ input

## Credits

- FM demodulation implementation: 2025
- Based on standard quadrature demodulation techniques
- Inspired by existing SDR FM demodulators (gqrx, rtl_fm, etc.)
