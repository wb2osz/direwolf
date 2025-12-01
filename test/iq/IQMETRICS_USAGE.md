# IQ Metrics Configuration

## Overview
The IQMETRICS configuration option controls whether RSSI/SNR metrics are displayed with decoded packets when using IQ input mode.

## Default Behavior
By default, IQ metrics are **disabled** (not displayed).

## Configuration

### Enable IQ Metrics
Add this line to your configuration file to enable IQ metrics display:
```
IQMETRICS on
```

Alternatively, you can use:
```
IQMETRICS yes
```
or
```
IQMETRICS 1
```

### Disable IQ Metrics
To explicitly disable IQ metrics display:
```
IQMETRICS off
```

Alternatively:
```
IQMETRICS no
```
or
```
IQMETRICS 0
```

### No Parameter
Using `IQMETRICS` without a parameter defaults to enabled:
```
IQMETRICS
```

## Output Format
When enabled, decoded packets will include RSSI and SNR information:
```
[0.2] I0KTE-1>APDW16,IR6AQZ,WIDE1,IR0EF-10,IR5AE,WIDE2*: [RSSI=-28.1 dBFS (S9+19), SNR=32.0 dB]!4201.01ND01317.14E#...
```

The metrics show:
- **RSSI**: Received Signal Strength Indicator in dBFS (decibels relative to full scale)
- **S-meter**: Equivalent S-meter reading (S1-S9, or S9+dB for strong signals)
- **SNR**: Signal-to-Noise Ratio in dB

When disabled, the same packet appears without metrics:
```
[0.2] I0KTE-1>APDW16,IR6AQZ,WIDE1,IR0EF-10,IR5AE,WIDE2*:!4201.01ND01317.14E#...
```

## Example Configuration File
```
# IQ input mode with metrics enabled
ADEVICE iq:48000 null
ACHANNELS 1

# Enable IQ metrics display
IQMETRICS on
```

## Notes
- IQ metrics are only available when using IQ input mode (ADEVICE iq:samplerate)
- Metrics are calculated before FM demodulation for accurate signal strength measurement
- If metrics are not available (e.g., non-IQ input), no metrics text will be appended even if enabled
