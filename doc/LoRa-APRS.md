# LoRa APRS with Dire Wolf

This document describes how to use Dire Wolf as an iGate, digipeater, and decoder for LoRa APRS using a Raspberry Pi with a supported LoRa hat.

## How it works

```
LoRa hat (SX1276 / SX1262 wired to SPI bus)
|
| Linux spidev + libgpiod
v
Dire Wolf (loraspi.c) <- direwolf.conf (LCHANNEL, LORAHW, LORAFREQ ...)
|
iGate / digipeater / decoder
```

Dire Wolf talks directly to the LoRa chip over SPI. No Python packages or separate processes are required -- the LoRa channel starts automatically when Dire Wolf starts.

## Requirements

- Raspberry Pi (any model with SPI)
- Supported LoRa hat (see Hardware Profiles below)
- `libgpiod-dev` installed (included automatically by `install-lora.sh`)

---

## Installation

### Fresh install

1. Update your system and reboot:

```bash
sudo apt update && sudo apt upgrade -y
sudo reboot
```

2. Clone the repository:

```bash
git clone -b feature/lora-spi https://github.com/radiohound/direwolf.git
cd direwolf
```

3. Run the install script:

```bash
sudo bash install-lora.sh
```

The script installs dependencies (including `libgpiod-dev`), builds Dire Wolf, enables SPI, and creates a starter config at `~/direwolf.conf`. It will prompt you for your callsign, passcode, location, hardware profile, and frequency.

SPI is enabled automatically. If a reboot is required the script will say so — reboot before using the LoRa hat.

4. Add your user to the `gpio` group so Dire Wolf can access GPIO without root:

```bash
sudo usermod -a -G gpio $USER
```

Log out and back in (or reboot) for this to take effect.

---

### Upgrade (preserves existing config)

If you already have Dire Wolf installed and just want to update the binary:

```bash
cd direwolf
git pull
sudo bash install-lora.sh --upgrade
```

The `--upgrade` flag skips the config prompts and does not overwrite `~/direwolf.conf`. It rebuilds and reinstalls the binary, then restarts the Dire Wolf service automatically. The script detects whichever service name is running (`direwolf` or a custom name like `direwolf-chase`) and restarts the correct one.

---

## Configuration (direwolf.conf)

The LCHANNEL block must appear **before** any PBEACON lines that reference that channel number.

```
# VHF APRS via RTL-SDR (piped from rtl_fm on stdin)
# On Pi 3 (no RTL-SDR) use: ADEVICE null null
ADEVICE stdin null
CHANNEL 0
MYCALL W1ABC-10
MODEM 1200

# LoRa SPI hat -- native driver
# Must appear before any PBEACON lines referencing channel 10
LCHANNEL 10
MYCALL W1ABC-10
LORAHW lorapi_rfm95w        # hardware profile (see table below)
LORAFREQ 433.775            # MHz (standard worldwide)
LORASF 12                   # spreading factor
LORABW 125                  # kHz bandwidth
LORACR 5                    # coding rate 4/5
LORASW 0x12                 # LoRa APRS sync word
LORATXPOWER 17              # dBm

# Position beacon over LoRa RF
PBEACON delay=1 every=30 sendto=10 overlay=L symbol="igate" lat=0.0000 long=0.0000 comment="LoRa APRS iGate"

# iGate -- forward received packets to APRS-IS
IGSERVER noam.aprs2.net
IGLOGIN W1ABC-10 12345

# Also beacon position to APRS-IS
PBEACON delay=1 every=30 sendto=IG overlay=L symbol="igate" lat=0.0000 long=0.0000 comment="LoRa APRS iGate"
```

Replace 0.0000 with your actual latitude and longitude in decimal degrees.
Replace lorapi_rfm95w with the profile matching your hardware (see table below).
Generate your APRS passcode at https://apps.magicbug.co.uk/passcode if needed.

---

## Starting Dire Wolf

On Pi 4/5 (with RTL-SDR for VHF APRS):

```bash
sudo systemctl stop direwolf
rtl_fm -f 144.390M -M fm -s 22050 -g 40 - | direwolf -c ~/direwolf.conf -r 22050 -
```

On Pi 3 (LoRa only):

```bash
sudo systemctl stop direwolf
direwolf -c ~/direwolf.conf
```

On a successful start you will see:

```
loraspi: GPIO chip base offset = 0
LoRa channel 10: 433.775 MHz SF12 BW125.0 kHz CR 4/5 SX1262
```

### Run as a systemd service

```bash
sudo cp systemd/direwolf.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable direwolf
sudo systemctl start direwolf
```

Monitor: journalctl -u direwolf -f

---

## Digipeating LoRa packets (optional)

```
# In direwolf.conf -- use the channel number from LCHANNEL
DIGIPEAT 10 10 ^TEST$ ^WIDE[12]-[12]$
```

---

## Hardware Profiles

Profiles are selected with LORAHW in direwolf.conf. They are defined in the s_profiles[] array in src/loraspi.c.

| Profile name | Module | Chip | Frequency | Max LORATXPOWER | TX tested | RX tested |
|---|---|---|---|---|---|---|
| meshadv_400m30s | MeshAdv-Pi Hat (E22-400M30S, 1W) | SX1262 | 433/470 MHz | 22 dBm | yes | yes |
| meshadv_400m33s | MeshAdv-Pi Hat (E22-400M33S, 2W) | SX1262 | 433/470 MHz | 8 dBm | yes | yes |
| meshadv_900m30s | MeshAdv-Pi Hat (E22-900M30S, 1W) | SX1262 | 868/915 MHz | 22 dBm | | |
| meshadv_900m33s | MeshAdv-Pi Hat (E22-900M33S, 2W) | SX1262 | 868/915 MHz | 8 dBm | | |
| lorapi_rfm95w | Digital Concepts LoRa-Pi (RFM95W) | SX1276 | 868/915 MHz | 17 dBm | | |
| lorapi_rfm98w | Digital Concepts LoRa-Pi (RFM98W) | SX1278 | 433 MHz | 17 dBm | yes | yes |
| generic_sx1276 | Generic SX1276/SX1278 breakout | SX1276 | varies | 17 dBm | | |

> **33S modules:** The `max_tx_power_dbm` for 33S variants is the maximum safe input to the SX1262 register — the external PA amplifies this to ~33 dBm output. Setting `LORATXPOWER` above 8 on a 33S module will damage the PA. Dire Wolf enforces this limit automatically and will print an error and clamp the value if exceeded.

To add support for a new LoRa hat, add a row to the s_profiles[] array in src/loraspi.c and rebuild Dire Wolf.

---

## LoRa APRS frequency

433.775 MHz is the standard LoRa APRS frequency worldwide.
Standard parameters: SF12, BW125, CR4/5, sync word 0x12.

---

## Log output

| Color | Meaning |
|-------|---------|
| Green | Packet received from LoRa radio |
| Magenta | Packet transmitted over LoRa radio |
| Red | Error or invalid/dropped packet |

Colors are suppressed automatically when output is redirected to a file or systemd journal.

---

## Troubleshooting

**No init message at startup**
- Check SPI is enabled: `ls /dev/spidev*`
- Verify LORAHW in direwolf.conf matches a known profile name (case-sensitive)
- Check SPI device permissions: `ls -l /dev/spidev*`
- Ensure the LCHANNEL block appears before any PBEACON lines in direwolf.conf
- Make sure the LoRa hat is fully and correctly seated on the GPIO header

**"Config file: Send to channel N is not valid"**
- The LCHANNEL block must appear **before** any PBEACON sendto=N lines in direwolf.conf. Move the LCHANNEL block to the top of the file.

**"GPIO setup failed for output/input pin N"**
- Dire Wolf could not claim a GPIO pin via libgpiod. Run `sudo gpioinfo` to see what is holding it.
- If the consumer shown is `sysfs`, a previous run left the pin exported via the legacy sysfs interface. Clear it:
  ```bash
  # Find the sysfs base offset first
  ls /sys/class/gpio/
  # Then unexport each pin (base + BCM number, e.g. base=512, pin 12 → 524)
  for n in 524 525 528 530 532 533; do echo $n | sudo tee /sys/class/gpio/unexport; done
  ```
  Adjust the numbers to match your system's base offset and hardware profile pins.
- If the consumer is another process, ensure only one instance of Dire Wolf is running.
- If Dire Wolf is running as a non-root user, ensure the user is in the `gpio` group:
  ```bash
  sudo usermod -a -G gpio $USER
  ```
  Then log out and back in.

**SX1262 BUSY timeout at startup**
- Usually caused by killing Dire Wolf and immediately restarting before the chip has recovered. Wait a few seconds and try again.
- If running as a systemd service, `sudo systemctl restart <service-name>` handles this cleanly.
- If the problem persists after a clean start, check that the hat is properly seated and powered.

**No packets received / poor range**
- Confirm frequency and sync word match other stations (433.775 MHz, 0x12 is standard)
- Verify spreading factor (SF12 is standard)
- Check Dire Wolf console for SPI errors or GPIO setup failures — if GPIO is not working, the external PA and LNA on the hat will not be enabled, severely reducing TX power and RX sensitivity
- Verify the LoRa hat is correctly seated on the GPIO header

---

## Adding support for a new LoRa hat

Hardware profiles are defined in the s_profiles[] array in src/loraspi.c. Each row defines the chip type, SPI bus, and GPIO pin assignments for one hat.

To add a new profile:

1. Open src/loraspi.c and find the s_profiles[] array.
2. Add a new row following this format:

```c
{ "profile_name", LORA_CHIP_SX1262, bus, dev, cs, reset, irq, busy, tx_en, rx_en, pa_boost, tcxo, tcxo_voltage, max_tx_power_dbm },
```

| Field | Description |
|-------|-------------|
| profile_name | Name used in LORAHW in direwolf.conf |
| chip | LORA_CHIP_SX1276 or LORA_CHIP_SX1262 |
| bus, dev | SPI bus and device (usually 0, 0) |
| cs | BCM GPIO pin number for chip select |
| reset | BCM GPIO pin number for reset |
| irq | BCM GPIO pin number for DIO0 (SX1276) or DIO1 (SX1262) |
| busy | BCM GPIO pin for BUSY (SX1262 only, -1 for SX1276) |
| tx_en | BCM GPIO pin for TX enable (-1 if not used) |
| rx_en | BCM GPIO pin for RX enable (-1 if not used) |
| pa_boost | true for SX1276 PA_BOOST pin, false for RFO |
| tcxo | true if the module uses a TCXO (most SX1262 modules do) |
| tcxo_voltage | TCXO supply voltage in volts (check module datasheet — typically 1.8V for 900 MHz, 2.2V for 400 MHz) |
| max_tx_power_dbm | Maximum safe LORATXPOWER register input value — Dire Wolf clamps and warns if exceeded. For modules with an external PA (33S variants) this is the SX1262 input level, not the RF output power. |

3. Pin numbers are BCM GPIO numbers. Check your hat's schematic or documentation for the correct assignments.
4. Rebuild and reinstall Dire Wolf:

```bash
cd ~/direwolf
make -C build -j$(nproc)
sudo make -C build install
```

5. Set LORAHW your_profile_name in direwolf.conf and start Dire Wolf.
