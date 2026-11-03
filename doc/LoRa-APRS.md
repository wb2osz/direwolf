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
- A user in the `gpio` and `spi` groups (see step 4 of the installation)
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

The script installs dependencies (including `libgpiod-dev`), builds Dire Wolf, enables SPI, and creates a starter config at `~/direwolf.conf`. It will prompt you for your callsign, passcode, location, hardware profile, and frequency (press Enter for 433.775 MHz). It also installs a `direwolf` systemd service that runs as your user and starts Dire Wolf at boot.

The installer assumes an RTL-SDR dongle for VHF APRS on every Pi model: it installs the RTL-SDR tools, blacklists the DVB driver, and the service pipes `rtl_fm` into Dire Wolf. For a LoRa-only station without a dongle, edit `ADEVICE` in `~/direwolf.conf` to `ADEVICE null null`, delete the VHF `CHANNEL 0` block, and change the `ExecStart` line of `/etc/systemd/system/direwolf.service` to `/usr/local/bin/direwolf -c /home/<you>/direwolf.conf`, then run `sudo systemctl daemon-reload`.

SPI is enabled automatically. If a reboot is required the script will say so — reboot before using the LoRa hat.

4. Add your user to the `gpio` and `spi` groups so Dire Wolf can reach the GPIO lines and `/dev/spidev*` without root:

```bash
sudo usermod -a -G gpio,spi $USER
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

### Where the LoRa settings live

All LoRa settings are in `direwolf.conf`. There is no separate LoRa config file.

- **`LCHANNEL n` sets the context.** Every line from `LCHANNEL` down to the next
  `CHANNEL` or `LCHANNEL` line applies to that LoRa channel. `n` must be from
  `MAX_RADIO_CHANS` up to `MAX_TOTAL_CHANS`-1 (6 to 15 in the default build), and at most 4 LoRa channels
  are allowed. Put the block after the VHF channel settings and before any `PBEACON`
  that uses `sendto=n`; a `PBEACON` that comes first fails with "Send to channel N is not valid".
- **`LCHANNEL` vs. `NCHANNEL`/`SCHANNEL`.** Use `LCHANNEL` for a LoRa hat wired to the
  Pi's SPI bus. Use `NCHANNEL`/`SCHANNEL` for an external LoRa KISS TNC (see
  APRS-LoRa-VHF-APRS-Bridge.pdf).
- **`LORAHW` vs. the radio settings.** `LORAHW` picks the hardware profile: the module's
  wiring, TCXO and maximum power (see Hardware Profiles below). `LORAFREQ`, `LORASF`,
  `LORABW`, `LORACR`, `LORASW` and `LORATXPOWER` are the radio settings.
- **`LORATXPOWER` is capped by the profile.** If you set it above the profile's maximum
  (for example 8 dBm on `meshadv_400m33s`), Dire Wolf prints an error and clamps it to
  the hardware limit. If you leave it out, the default is 20 dBm, quietly capped to the profile's maximum.
- **What `LORATXPOWER` measures.** It is the power the SX126x/SX127x chip is told to
  produce (the `SetTxParams` value), *before* the module's external power amplifier.
  On E22 M30S and M33S modules the power at the antenna connector is higher than the
  setting. That is why the profile caps are so low: 18 dBm for the 400 MHz 30S (29.96 dBm, about 1 W, at the antenna) and only 8 dBm
  for the 33S. Do not read the number as antenna power.
- **Defaults and valid values.**

  | Directive | Default | Accepted values |
  |---|---|---|
  | `LORAHW` | `meshadv_400m30s` (1 W, 433 MHz) | a profile name from the table below (not case-sensitive) |
  | `LORAFREQ` | 433.775 | 137 to 1020 MHz |
  | `LORASF` | 12 | 7 to 12 |
  | `LORABW` | 125 | 125, 250 or 500 (kHz) |
  | `LORACR` | 5 | 5 to 8 (coding rate 4/5 to 4/8) |
  | `LORASW` | 0x12 | 0x00 to 0xFF |
  | `LORATXPOWER` | 20, capped to the profile maximum | 0 to 22 (dBm), measured at the chip output, before the module's external PA |

  Set `LORAHW` and `LORAFREQ` explicitly if you use a 868/915 MHz module or a different hat.
  A value outside its range is rejected with an error and the default stays in effect.

```
# VHF APRS via RTL-SDR (piped from rtl_fm on stdin)
# With no RTL-SDR (LoRa only) use: ADEVICE null null
ADEVICE stdin null
CHANNEL 0
MYCALL W1ABC-10
MODEM 1200

# ---- LoRa channel (native SPI HAT) ----
# Every line from LCHANNEL down to the next CHANNEL/LCHANNEL/SCHANNEL line applies to the LoRa channel.
# LORAHW picks the module wiring/TCXO and its maximum power; the other LORA* lines are radio settings.
LCHANNEL 10
MYCALL W1ABC-10
LORAHW lorapi_rfm98w        # hardware profile (see table below)
LORAFREQ 433.775            # MHz (default; the most common LoRa APRS frequency)
LORASF 12                   # spreading factor
LORABW 125                  # kHz bandwidth
LORACR 5                    # coding rate 4/5
LORASW 0x12                 # LoRa APRS sync word
LORATXPOWER 17              # dBm at the chip, before the module's PA (optional; default 20, capped to the profile maximum)

# Position beacon over LoRa RF
PBEACON delay=1 every=30 sendto=10 overlay=L symbol="igate" lat=0.0000 long=0.0000 comment="LoRa APRS iGate"

# iGate -- forward received packets to APRS-IS
IGSERVER noam.aprs2.net
IGLOGIN W1ABC-10 12345

# Also beacon position to APRS-IS
PBEACON delay=1 every=30 sendto=IG overlay=L symbol="igate" lat=0.0000 long=0.0000 comment="LoRa APRS iGate"
```

Replace 0.0000 with your actual latitude and longitude in decimal degrees.
Replace lorapi_rfm98w with the profile matching your hardware (see table below). For a 868/915 MHz module such as `lorapi_rfm95w`, also set `LORAFREQ` to your regional LoRa APRS frequency.
Generate your APRS passcode at https://apps.magicbug.co.uk/passcode if needed.

---

## Starting Dire Wolf

With an RTL-SDR for VHF APRS:

```bash
sudo systemctl stop direwolf
rtl_fm -f 144.390M -M fm -s 22050 -g 40 - | direwolf -c ~/direwolf.conf -r 22050 -
```

LoRa only (`ADEVICE null null` in the config):

```bash
sudo systemctl stop direwolf
direwolf -c ~/direwolf.conf
```

On a successful start you will see:

```
loraspi: GPIO chip: /dev/gpiochip0 (libgpiod 2.1)
LoRa channel 10: 433.775 MHz SF12 BW125.0 kHz CR 4/5 SX1262
```

If the GPIO lines are claimed through the legacy sysfs interface instead (libgpiod not installed at build time), the first line reads `loraspi: GPIO sysfs base offset = N`.

### Run as a systemd service

`install-lora.sh` already created, enabled and started the `direwolf` service, which runs as your user and reads `~/direwolf.conf`. Do not copy the generic `systemd/direwolf.service` from the source tree over it: that unit runs as a separate `direwolf` user and cannot read your home directory or reach the GPIO lines.

Monitor: `journalctl -u direwolf -f`. Add `-t 0` to the `direwolf` command line to turn off the color codes in the journal.

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
| meshadv_400m30s | MeshAdv-Pi Hat (E22-400M30S, 1W) | SX1262 | 433/470 MHz | 18 dBm | yes | yes |
| meshadv_400m33s | MeshAdv-Pi Hat (E22-400M33S, 2W) | SX1262 | 433/470 MHz | 8 dBm | yes | yes |
| meshadv_900m30s | MeshAdv-Pi Hat (E22-900M30S, 1W) | SX1262 | 868/915 MHz | 22 dBm | | |
| meshadv_900m33s | MeshAdv-Pi Hat (E22-900M33S, 2W) | SX1262 | 868/915 MHz | 8 dBm | | |
| lorapi_rfm95w | Digital Concepts LoRa-Pi (RFM95W) | SX1276 | 868/915 MHz | 17 dBm | | |
| lorapi_rfm98w | Digital Concepts LoRa-Pi (RFM98W) | SX1278 | 433 MHz | 17 dBm | yes | yes |
| generic_sx1276 | Generic SX1276/SX1278 breakout | SX1276 | varies | 17 dBm | | |

> **33S modules:** we could not find official documentation for the E22-400M33S power table (chip setting versus power at the antenna). Until official information or documented testing is available, the 33S profiles are capped at 8 dBm, like the other 33S entries, and Dire Wolf prints no antenna-power estimate for them. Setting `LORATXPOWER` above 8 on a 33S module can damage its external PA. Dire Wolf clamps the value to the profile limit and prints an error. See [LoRa-Implementation.md](LoRa-Implementation.md#tx-power-clamping) for why.

> **Antenna power on the E22-400M30S.** The module manual lists the power at the antenna connector for each chip setting. Dire Wolf interpolates this table and prints the result at startup, for example `LORATXPOWER 14 dBm at the chip, about 27.3 dBm (0.53 W) at the antenna connector`.
>
> | `LORATXPOWER` (chip, dBm) | At the antenna (dBm) |
> |---|---|
> | -5 | 9.77 |
> | -2 | 12.29 |
> | 1 | 15.02 |
> | 4 | 18.08 |
> | 7 | 21.11 |
> | 10 | 24.02 |
> | 14 | 27.25 |
> | 18 | 29.96 (about 1 W, the manual's maximum) |
>
> 18 dBm is the manual's maximum, so the profile caps `LORATXPOWER` there; a higher setting is clamped (the 20 dBm default is capped quietly). These are the vendor's figures, not our measurements; real units vary with frequency, supply voltage and temperature.

To add support for a hat that is not listed, see the last section.

---

## LoRa APRS frequency

433.775 MHz is the most common LoRa APRS frequency. Typical parameters are SF12, BW125, CR4/5 and sync word 0x12.
Some regions use other frequencies or faster settings, so match what the LoRa APRS stations near you use.

---

## Log output

| Color | Meaning |
|-------|---------|
| Green | Packet received from LoRa radio |
| Magenta | Packet transmitted over LoRa radio |
| Red | Error or invalid/dropped packet |

Dire Wolf does not detect whether output is a terminal. Use `-t 0` when logging to a file or the journal to turn the colors off.

Each received packet is shown once, by Dire Wolf's normal decode line, which ends with the signal strength, for example `LoRa RSSI=-97dBm SNR=7.5dB`. Packets that fail to parse are logged with their RSSI and SNR too. While the channel is idle, Dire Wolf logs the noise floor (average, minimum and maximum RSSI) every 60 seconds, which helps with antenna and placement tests.

Start Dire Wolf with `-d L` for extra driver diagnostics, such as the chip status while transmitting.

---

## What works over a LoRa channel

- **Works:** beacons (`PBEACON sendto=n`), digipeating (`DIGIPEAT`), the APRS-IS iGate in both directions, KISS and AGW clients, and `IGTXVIA`. AGW clients see the channel as `PortN LoRa`.
- **Not supported:** connected-mode AX.25 (`CDIGIPEAT`, `ax25_link`) is limited to radio channels with a modem.
- **No channel-access control:** the LoRa driver transmits when a packet reaches the queue. It does not listen first, and `TXDELAY`, `PERSIST` and `SLOTTIME` do not apply. Keep beacon intervals long, especially at SF12, where one packet takes over two seconds. Observe your local duty-cycle and band rules.
- **Packet size:** a LoRa frame carries at most 255 bytes including the 3-byte header. Longer packets are dropped with an error.

---

## Troubleshooting

**No init message at startup**
- Check SPI is enabled: `ls /dev/spidev*`
- Verify LORAHW in direwolf.conf matches a known profile name
- Check SPI device permissions: `ls -l /dev/spidev*` (your user needs the `spi` group)
- Make sure the LoRa hat is fully and correctly seated on the GPIO header

**"Config file: Send to channel N is not valid"**
- The LCHANNEL block must appear **before** any PBEACON sendto=N lines in direwolf.conf. Move any such PBEACON below the LCHANNEL block.

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
- If Dire Wolf is running as a non-root user, ensure the user is in the `gpio` and `spi` groups:
  ```bash
  sudo usermod -a -G gpio,spi $USER
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

See [LoRa-Implementation.md](LoRa-Implementation.md#adding-support-for-a-new-lora-hat).
