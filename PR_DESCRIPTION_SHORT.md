# IQ Input Support with RSSI/SNR Metrics

## Overview
Add native IQ sample input support to Direwolf, enabling direct SDR integration with real-time signal quality metrics (RSSI and SNR).

## Key Features
- **IQ Input Mode**: Accept complex float32 samples directly from stdin (`iq:RATE` syntax)
- **Built-in FM Demodulation**: Quadrature demodulator with de-emphasis
- **RSSI Measurement**: Signal strength in dBFS with S-meter conversion
- **SNR Calculation**: Real-time signal-to-noise ratio reporting
- **100% Backward Compatible**: No changes to existing audio input modes

## Usage
```bash
# File playback example (CF32 format at 48kHz)
cat samples.cf32 | direwolf -r 48000 -n 1 iq:48000

# Output includes metrics for each decoded packet
[RSSI=-10.5 dBFS (S9+35), SNR=52.3 dB]
```

## What Changed
- **Added**: `src/iq_metrics.c/h` - RSSI/SNR calculation
- **Added**: `src/fm_demod.c/h` - FM demodulation
- **Added**: `IQ_INPUT.md` - Complete documentation
- **Added**: `test/iq/` - Test files and scripts
- **Modified**: `src/audio.c`, `src/direwolf.c` - IQ input integration
- **Modified**: `src/config.c/h` - `IQMETRICS` configuration option

**Stats**: 21 files changed, 1049 insertions(+), 3 deletions(-)

## Testing
✅ Tested with SDRplay RSP1  
✅ Validated on 144.800 MHz APRS traffic  
✅ 100% packet decode success rate with test files  
✅ Cross-platform builds (Linux, macOS tested)  
✅ No external dependencies added

## Benefits
1. **Simplified SDR Integration**: No need for external FM demodulation tools
2. **Signal Quality Monitoring**: RSSI/SNR metrics for station analysis
3. **Performance**: Minimal CPU overhead (~2-5% increase)
4. **Flexibility**: Works with any SDR via stdin pipe

## Documentation
Complete guide in `IQ_INPUT.md` covering:
- Installation and setup
- SDR integration example (SDRplay)
- Configuration options
- Troubleshooting

## Compatibility
- No breaking changes
- Existing features unaffected
- Backward compatible with all configurations

See `PULL_REQUEST.md` for full technical details.
