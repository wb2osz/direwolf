# IQ Input with FM Demodulation for Direwolf

This feature allows Direwolf to accept IQ (In-phase/Quadrature) samples from Software Defined Radios (SDR) and perform FM demodulation internally before AFSK demodulation.

## Quick Start

```bash
# Example: Receive APRS from SDR (RSP1) at 144.800 MHz
python3 scripts/sdrplay_to_direwolf.py --agc  2>/dev/null |  csdr fir_decimate_cc 4 2>/dev/null | ./build/src/direwolf -M -t 0 -r 48000 -n 1 iq:48000 2>&1 -
```

Or pipe from a file:
```bash
cat iq48k_cfloat.raw | direwolf -t 0 -r 48000 -n 1 iq:48000
```

## Configuration

### Command Line Usage (Simplest)

For quick testing without a configuration file:

```bash
direwolf -t 0 -r 48000 -n 1 iq:48000
```

Parameters:
- `-t 0`: No PTT (receive only)
- `-r 48000`: Sample rate (must match IQ input rate)
- `-n 1`: Channel colors off (cleaner output)
- `iq:48000`: IQ input device at 48000 Hz

### Configuration File Usage

To use IQ input with a configuration file, set your audio device to `iq:<sample_rate>`:

```
ADEVICE iq:48000 null
```

Where `48000` is the IQ sample rate in Hz. Common values are:
- 48000 (48 kHz - typical after decimation)
- 96000 (96 kHz)
- 192000 (192 kHz - common raw SDR rate)

Full configuration example:
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

## Technical Details

### IQ Sample Format
- **Data type**: 32-bit IEEE 754 floating point (float32)
- **Byte order**: Little-endian
- **Layout**: Interleaved I/Q pairs [I₀, Q₀, I₁, Q₁, I₂, Q₂, ...]
- **Value range**: Typically -1.0 to +1.0 (auto-scaled internally)
- **Sample rate**: Must match configured rate (e.g., 48000 Hz)

### FM Demodulation Algorithm

Quadrature discriminator implementation:
```
ΔI = I[n] - I[n-1]
ΔQ = Q[n] - Q[n-1]
mag² = I[n]² + Q[n]²

if (mag² > threshold):
    output = K × (I[n] × ΔQ - Q[n] × ΔI) / mag²
```

Where:
- **K**: 0.340447... (scaling constant derived from π and sample rate)
- **threshold**: 1e-5 (prevents division by near-zero values)
- **Output clamping**: ±100 range before int16 conversion

### Compatibility

This implementation produces identical output to csdr's `fmdemod` tool, ensuring compatibility with existing SDR workflows.

### Performance

- Processes 48 kHz IQ input in real-time on Raspberry Pi and higher
- Successfully decodes APRS packets at same performance as csdr pipeline
- Validated with test files containing multiple APRS packets

## References

- [csdr - DSP for Software Defined Radio](https://github.com/ha7ilm/csdr)
- [APRS Specification](http://www.aprs.org/doc/APRS101.PDF)
- [Bell 202 Modem Standard](https://en.wikipedia.org/wiki/Bell_202_modem)

## Implementation Files

- `src/fm_demod.c` - FM demodulator implementation
- `src/fm_demod.h` - Public API header
- `src/audio.c` - IQ input integration (AUDIO_IN_TYPE_SDR_IQ case)
- `test/iq/` - Test IQ samples and validation scripts

## Usage Examples

### Example 1: Direct SDR Reception with rx_sdr

Receive APRS on 144.800 MHz using an SDRplay device:

```bash
python3 scripts/sdrplay_to_direwolf.py --agc  2>/dev/null |  csdr fir_decimate_cc 4 2>/dev/null | ./build/src/direwolf -M -t 0 -r 48000 -n 1 iq:48000 2>&1 -
python3 scripts/sdrplay_to_direwolf.py --agc  2>/dev/null |  csdr fir_decimate_cc 4 2>/dev/null | ./build/src/direwolf -M -t 0 -r 48000 -n 1 iq:48000 2>&1 -
```

This pipeline:
1. `rx_sdr`: Captures IQ at 192 kHz as 16-bit signed integers (CS16)
2. `csdr convert_s16_f`: Converts to float32
3. `csdr fir_decimate_cc 4`: Decimates by 4 → 48 kHz output
4. `direwolf`: Receives 48 kHz IQ, demodulates FM, decodes APRS packets

### Example 2: File Playback

Test with pre-recorded IQ samples:

```bash 
cat iq48k_cfloat.raw | direwolf -t 0 -r 48000 -n 1 iq:48000
```

Where `iq48k_cfloat.raw` contains complex float32 IQ samples at 48 kHz.

### Example 3: With Configuration File

Create `aprs_sdr.conf`:
```
ADEVICE iq:48000 null
CHANNEL 0
MYCALL N0CALL
MODEM 1200
PTT NONE
```

Then run:
```bash
sdrplay_to_direwolf.py ... | csdr ... | direwolf -c aprs_sdr.conf
```

## How It Works

1. **IQ Input**: Direwolf reads complex float32 samples from stdin
2. **FM Demodulation**: Uses csdr-compatible quadrature discriminator algorithm
   - Formula: `output = K × (I[n] × ΔQ - Q[n] × ΔI) / (I² + Q²)`
   - Where ΔI and ΔQ are differences from previous sample
   - K = 0.340447... (scaling constant for proper deviation)
3. **Numerical Stability**: 
   - Magnitude threshold to prevent division by near-zero
   - Infinite/NaN detection and clamping
4. **Audio Output**: Produces mono 16-bit audio samples
5. **AFSK Demodulation**: Standard Direwolf 1200 baud Bell 202 demodulation

## FM Demodulator Details

The FM demodulator is designed to match csdr's fmdemod behavior:
- **Algorithm**: Quadrature discriminator (phase difference method)
- **Output range**: Approximately ±30 (before scaling to int16)
- **Numerical protections**: 
  - Magnitude² threshold: 1e-5
  - Output clamping: ±100
  - isfinite() checks to prevent inf/nan propagation

## Troubleshooting

### No packets decoded
- **Verify sample rate**: IQ rate must match `-r` parameter or ADEVICE rate
- **Check frequency**: SDR must be tuned to APRS frequency (e.g., 144.800 MHz)
- **Adjust gain**: Too low = weak signal, too high = clipping/distortion
- **Test with known good file**: Use provided test files to verify direwolf is working

### "End of IQ stream on stdin" message
- Normal when input pipeline terminates
- Check `csdr` commands for errors
- Verify SDR device is connected and accessible

### Build errors
- Ensure `fm_demod.c` is added to `src/CMakeLists.txt`
- Math library (`-lm`) should be automatically linked
- Rebuild with: `cd build && cmake .. && make`

### Saturated or distorted audio
- Check IQ input levels (should be in ±1.0 range typically)
- Reduce SDR gain if clipping occurs
- Verify decimation is correct (output rate should match direwolf input rate)

### Performance issues
- IQ processing is CPU-intensive
- Consider decimating to lower sample rates (48 kHz is usually sufficient for APRS)
- Use proper decimation filters (csdr's `fir_decimate_cc` recommended)

## Testing

Test the implementation with the included sample file:

```bash
cd test/iq
cat iq48k_cfloat.raw | ../../build/src/direwolf -t 0 -r 48000 -n 1 iq:48000
```

Expected output: 3 decoded APRS packets from Italian stations (I0KTE-1, IU5ICR).

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
