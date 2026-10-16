# LoRa APRS with Dire Wolf — RTL-SDR Receive Path

This document describes the optional SDR receive path for LoRa APRS using
an RTL-SDR dongle and GNU Radio instead of a dedicated LoRa hardware module.

> **Note:** This is an extension of the native SPI driver described in
> [LoRa-APRS.md](LoRa-APRS.md). Read that document first.

## When to use SDR vs the native SPI driver

|                        | Native SPI driver (`LCHANNEL`) | SDR bridge (`lora_sdr_bridge.py`) |
|------------------------|----------------------------------|--------------------------------------|
| **RX**                 | yes                              | yes                                  |
| **TX**                 | yes                              | **no** (RTL-SDR is receive-only)     |
| **Hardware cost**      | $15–40 (LoRa hat)                | $25 (RTL-SDR dongle)                 |
| **CPU on Pi 4**        | ~0.5%                            | ~20–35%                              |
| **CPU on Pi 5**        | ~0.2%                            | ~8–15%                               |
| **Decode quality**     | chip-native                      | good for SF7–SF12, BW125             |
| **TX beacons**         | yes (via PBEACON)                | dropped (logged only)                |
| **Use case**           | iGate + digipeater               | RX-only iGate / monitoring           |

Use the SDR bridge when:
- You only need to receive and gate LoRa APRS packets (no TX)
- You already have an RTL-SDR dongle for other purposes
- You do not have a LoRa hardware hat

Use the native SPI driver (`LCHANNEL`) when:
- You need to transmit (beacons, digipeating)
- CPU budget is tight (Pi 3 / Pi Zero)

> **Note:** The SDR path is not supported on Pi 3 due to CPU constraints
> (~20–35% CPU usage). Use the native SPI driver (`LCHANNEL`) on Pi 3.

## Architecture

```
RTL-SDR dongle
|
| USB (IQ samples, ~1 Msps)
v
GNU Radio (gr-lora_sdr blocks)
| demodulate Chirp Spread Spectrum
| decode LoRa frames + SNR from PDU metadata
v
lora_sdr_bridge.py <-- TCP (KISS) -- Dire Wolf (NCHANNEL)
| strip non-printable preamble bytes
| validate TNC2 header
| encode as AX.25 UI frame, wrap in KISS
^
waits for Dire Wolf to connect (bridge is the KISS server)
v
iGate / decoder
```

The bridge acts as a **KISS TCP server**. Dire Wolf connects to it using the
`NCHANNEL` directive (available in WB2OSZ's dev branch and Dire Wolf 1.9+):

```
NCHANNEL 10 127.0.0.1 8002
```

This is the same protocol used by TTGO/Heltec serial LoRa devices (`SCHANNEL`)
and other external KISS TNCs (`NCHANNEL`). The native SPI driver
(`loraspi.c`, `LCHANNEL`) is used for LoRa hats wired directly to the Pi.

## Requirements

**Hardware:**
- Raspberry Pi 4 or 5 (Pi 3 is not supported — too CPU-intensive)
- RTL-SDR dongle (RTL2832U-based — e.g. RTL-SDR Blog V3, NooElec NESDR)
- Antenna for 433 MHz

**Software:**

```bash
# GNU Radio + gr-osmosdr (package manager — easier than building from source)
sudo apt install gnuradio gr-osmosdr

# gr-lora_sdr (must build from source — not available as a Debian package)
sudo apt install cmake git libboost-all-dev pybind11-dev python3-pybind11
git clone https://github.com/tapparelj/gr-lora_sdr.git
cd gr-lora_sdr
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
sudo ldconfig

# Verify the Python bindings loaded correctly:
python3 -c "from gnuradio import lora_sdr; print('lora_sdr OK')"

# RTL-SDR driver
sudo apt install rtl-sdr

# Blacklist the kernel DVB driver so rtl-sdr can claim the device.
echo "blacklist dvb_usb_rtl28xxu" | sudo tee /etc/modprobe.d/rtlsdr.conf
sudo modprobe -r dvb_usb_rtl28xxu 2>/dev/null || true

# Python packages
# On Raspberry Pi OS Bookworm / Debian Trixie (2023+) add --break-system-packages:
pip3 install --break-system-packages pyyaml
```

Verify the RTL-SDR is detected:

```bash
rtl_test -t  # Should show: Found 1 device(s)
```

## Configuration

### lora.conf

```
# RF parameters — must match your local LoRa APRS network
LORAFREQ 433.775    # MHz (standard worldwide)
LORABW   125        # kHz
LORASF   12         # spreading factor
LORACR   5          # coding rate (5 = 4/5)
LORASW   0x12       # LoRa APRS sync word

# SDR receive settings
SDRDEVICE    0      # RTL-SDR device index (rtl_test to find yours)
SDRGAIN      40     # tuner gain in dB (0 = automatic)
SDRSAMPLERATE 1000000  # IQ sample rate (>= 2 x BW in Hz)

# TCP port this bridge listens on — Dire Wolf connects here via NCHANNEL
KISSPORT 8002
```

### direwolf.conf

The bridge is a **KISS TCP server** — Dire Wolf is the KISS client and connects
to it with `NCHANNEL`. If you have no physical audio TNC add `ADEVICE null null`
so Dire Wolf does not exit when it finds no sound card:

```
# Suppress audio device requirement (LoRa/SDR-only setup)
ADEVICE null null

MYCALL N0CALL-10

# LoRa SDR bridge — Dire Wolf connects to the bridge on port 8002
# Channel 10 is arbitrary; use any virtual channel >= MAX_RADIO_CHANS
NCHANNEL 10 127.0.0.1 8002

# iGate (optional)
IGSERVER noam.aprs2.net
IGLOGIN N0CALL-10 <passcode>

# Position beacon — sendto=IG sends directly to APRS-IS (no RF transmit needed)
PBEACON delay=1 every=30 sendto=IG overlay=L symbol="igate" lat=0.0000 long=0.0000 comment="LoRa APRS SDR iGate"
```

> **Note:** The SDR path cannot transmit. Use `sendto=IG` on PBEACON so the
> beacon goes directly to APRS-IS rather than over RF. Any TX frames Dire Wolf
> sends to the bridge (digipeater output, etc.) will be logged and dropped.

If you also want to receive **VHF APRS** (144.390 MHz) alongside LoRa, pipe
`rtl_fm` into Dire Wolf instead of using `ADEVICE null null`:

```bash
rtl_fm -f 144.390M -o 4 -s 24000 - | direwolf -c ~/direwolf.conf -r 24000 -D 1 -
```

This uses channel 0 for 1200-baud APRS and channel 6 for LoRa simultaneously.

## Starting the bridge

Start the **bridge first** — it is the KISS TCP server and must be listening
before Dire Wolf tries to connect:

```bash
# Terminal 1 — bridge starts listening on KISSPORT
python3 ~/direwolf/scripts/lora_sdr_bridge.py

# Terminal 2 — Dire Wolf connects to the bridge via NCHANNEL
direwolf -c ~/direwolf.conf

# With debug logging
python3 ~/direwolf/scripts/lora_sdr_bridge.py --log-level DEBUG

# With explicit config path
python3 ~/direwolf/scripts/lora_sdr_bridge.py -c ~/lora.conf
```

> **Startup order:** If Dire Wolf starts before the bridge, NCHANNEL will
> retry the connection automatically — you do not have to restart Dire Wolf.

## systemd service

Service files are provided in `systemd/`. The SDR bridge depends on Dire Wolf,
so enable both:

```bash
sudo cp systemd/direwolf.service /etc/systemd/system/
sudo cp systemd/lora-sdr-bridge.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable direwolf lora-sdr-bridge
sudo systemctl start direwolf lora-sdr-bridge
```

## Running SDR bridge and native SPI driver simultaneously

You can receive LoRa APRS on the SDR bridge and also have a LoRa hat for TX
(beacons, digipeating) at the same time. The SDR bridge uses `NCHANNEL`; the
LoRa hat uses `LCHANNEL` (native SPI driver, no bridge script needed):

```
# direwolf.conf — SDR bridge on channel 10, LoRa hat on channel 11
NCHANNEL 10 127.0.0.1 8002    # lora_sdr_bridge.py (RX-only, SDR)

LCHANNEL 11
MYCALL N0CALL-10
LORAHW lorapi_rfm95w           # adjust to your hardware
LORAFREQ 433.775
LORASF 12
LORABW 125
LORACR 5
LORASW 0x12
LORATXPOWER 17
```

Packets received on either channel are decoded and gated independently.
TX (beacons, digipeating) is routed to channel 11 (the hat) only, since the
SDR is receive-only.

## Running the tests

The test suite mocks GNU Radio and RTL-SDR so it runs on any machine:

```bash
python3 scripts/test_sdr_bridge.py
```

Expected output:
```
=== LoRa SDR bridge integration test ===
[PASS] Bridge connects to mock Dire Wolf
--- Test 1: LoRa SDR RX -> Dire Wolf ---
[PASS] Dire Wolf received one TNC2 line
[PASS] TNC2 content matches injected packet
...
=== 19/19 tests passed ===
```

## Log output and colors

The bridge uses a Dire Wolf-compatible ANSI color scheme (black background):

| Color | Meaning |
|-------|---------|
| Green | Packet received and forwarded to Dire Wolf |
| Magenta | TX frame received from Dire Wolf (logged and dropped — SDR is RX-only) |
| Red | Error or dropped/invalid packet |
| Dark green | Debug messages (`--log-level DEBUG`) |

Colors are suppressed automatically when output is redirected to a file or
systemd journal (when stderr is not a TTY).

## Troubleshooting

**`ImportError: No module named 'gnuradio'`**
- GNU Radio is not installed or not on PYTHONPATH
- Try: `python3 -c "import gnuradio; print(gnuradio.__version__)"`
- If missing: `sudo apt install gnuradio`

**`ImportError: No module named 'gnuradio.lora_sdr'`**
- gr-lora_sdr not installed; build from source (see Requirements above)

**`No RTL-SDR device found`**
- Check USB connection: `lsusb | grep RTL`
- Check kernel driver conflict: `sudo modprobe -r dvb_usb_rtl28xxu`
- Check permissions: `sudo usermod -a -G plugdev $USER` then log out/in

**No packets received (but hardware seems OK)**
- Verify frequency: 433.775 MHz (standard worldwide)
- Try higher gain: set `SDRGAIN 50` in lora.conf
- Check spreading factor: SF12 is standard; some networks use SF9 or SF11
- Run `rtl_power -f 433.7M:433.9M:1k -g 40 30s /tmp/scan.csv` to confirm RF activity

**High CPU usage**
- Expected ~20–35% on Pi 4; ~8–15% on Pi 5
- SDR path is not supported on Pi 3 — use the native SPI driver (`LCHANNEL`) instead

**Garbled decodes — `WARNING: Dropping packet with invalid TNC2 header`**
- The bridge validates every decoded frame before forwarding it to Dire Wolf. Packets
  with bit errors in the address field are silently dropped and logged at WARNING level.
- Digipeated LoRa packets are most susceptible because the digipeater re-transmits with
  slightly different timing, causing the SDR receiver to lose phase lock momentarily.
- Options: improve antenna gain, add an external LNA before the RTL-SDR, or increase
  `SDRGAIN` in `lora.conf` (up to ~50 dB for R820T2 dongles).
- The native SPI driver (SX1276/SX1262) handles marginal signals better because it
  performs LoRa demodulation in dedicated silicon.

## gr-lora_sdr vs gr-lora

Two GNU Radio LoRa implementations exist:

| Project | Repo | Status | Notes |
|---------|------|--------|-------|
| gr-lora_sdr | tapparelj/gr-lora_sdr | active (2024) | Recommended; GR 3.10 compatible |
| gr-lora | BastilleResearch/gr-lora | unmaintained | GR 3.7/3.8 only |

This bridge uses **gr-lora_sdr**. If you have gr-lora installed instead, the flowgraph
block names in `lora_sdr_flowgraph.py` will need adjustment.

## Relationship to the native SPI driver

The SDR bridge connects to Dire Wolf via the standard KISS TCP (`NCHANNEL`) interface —
the same protocol used by external KISS TNCs. The native SPI driver (`LCHANNEL` /
`loraspi.c`) is compiled directly into Dire Wolf and requires no separate process.

To remove the SDR bridge entirely, delete:
```
scripts/lora_sdr_bridge.py
scripts/lora_sdr_flowgraph.py
scripts/test_sdr_bridge.py
doc/LoRa-SDR.md
systemd/lora-sdr-bridge.service
```

All Dire Wolf C changes and the native SPI driver remain intact.
