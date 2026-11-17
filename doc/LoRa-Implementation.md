# LoRa APRS — Implementation Notes

This document is for developers who want to understand how the LoRa APRS
support is implemented, build Dire Wolf from source with LoRa support, or
contribute changes back to the upstream WB2OSZ/direwolf project.

For end-user setup, see [LoRa-APRS.md](LoRa-APRS.md).

---

## Architecture

LoRa frames are sent and received by a native SPI driver that talks directly to the
LoRa HAT:

```
┌──────────────────────────────────────────────────────┐
│                      Dire Wolf                        │
│                                                       │
│  APRS-IS ──────── igate.c                            │
│  beacon.c ──► tq.c                                   │
│                    │                                  │
│                loraspi.c                              │
│                LCHANNEL                               │
│            (native SPI driver)                        │
└─────────────────────┬─────────────────────────────────┘
                      │  Linux spidev + libgpiod
            ┌─────────┴───────────┐
            │  SX1276 / SX1262    │
            │  LoRa hat           │
            │  (RX + TX)          │
            └─────────────────────┘
```

---

## Modified original Dire Wolf source files

These files from the upstream WB2OSZ/direwolf repository were modified to add LoRa
support. There is no behaviour change for existing channel types.

### `src/audio.h`
- Added `MEDIUM_LORA` to `enum medium_e` (alongside `MEDIUM_RADIO`, `MEDIUM_NETTNC`, `MEDIUM_SERTNC`).
- Added 22 `lora_*[]` fields to `struct audio_s` to hold per-channel LoRa configuration:
  `lora_chip`, `lora_freq_mhz`, `lora_sf`, `lora_bw_khz`, `lora_cr`, `lora_sw`, `lora_txpower`,
  `lora_txpower_set`, `lora_max_txpower`,
  `lora_pin_cs`, `lora_pin_reset`, `lora_pin_irq`, `lora_pin_busy`, `lora_pin_tx_en`,
  `lora_pin_rx_en`, `lora_pa_boost`, `lora_tcxo`, `lora_tcxo_voltage`, `lora_spi_bus`,
  `lora_spi_dev`, `lora_spi_speed`, `lora_hw` (profile name).

### `src/config.c`
- Added `#include "loraspi.h"`.
- Added parsing for eight new directives: `LCHANNEL`, `LORAHW`, `LORAFREQ`, `LORASF`,
  `LORABW`, `LORACR`, `LORASW`, `LORATXPOWER`. `LCHANNEL` sets
  `chan_medium[chan] = MEDIUM_LORA`, fills in the defaults (433.775 MHz, SF12, BW125, CR4/5,
  sync word 0x12, 20 dBm, `meshadv_400m30s` wiring) and makes the channel current for the
  lines that follow. Values are range-checked as they are parsed.
  `LORAHW` calls `loraspi_apply_profile()` to copy pin/chip settings from a named
  hardware profile.
- Extended the `chan_medium` validity checks for `DIGIPEAT`, `TTOBJ` and the IGate transmit
  channels to accept `MEDIUM_LORA`. `CDIGIPEAT` still accepts radio channels only. `SCHANNEL` now sets the current channel for a following `MYCALL`, and `FILTER` accepts LoRa channels.

### `src/direwolf.c`
- Added `#include "loraspi.h"`.
- Added `loraspi_init(&audio_config)` call during startup, after `sertnc_init()`.
- Added the `-d L` debug option, which calls `loraspi_set_debug()`.

### `src/tq.c`
- Added `#include "loraspi.h"`.
- Added a routing branch in the transmit-queue dispatcher: if the destination channel
  has `chan_medium == MEDIUM_LORA`, route the packet to `loraspi_send_packet()` instead
  of the normal audio TX path.

### `src/beacon.c`
- Extended the valid-channel check to include `MEDIUM_LORA`, so that `PBEACON sendto=N`
  works when channel N is a LoRa channel.

### `src/digipeater.c`
- Extended the valid receive-channel check to include `MEDIUM_LORA`.

### `src/server.c`
- The AGW port-information reply now counts and lists `MEDIUM_LORA` channels (`PortN LoRa`).

### `src/CMakeLists.txt`
- Added `loraspi.c` to the `direwolf` executable source list.

---

## New files added

These files do not exist in the upstream repository and were created entirely for this
LoRa implementation.

### Dire Wolf source

| File | Purpose |
|------|---------|
| `src/loraspi.c` | Native SPI driver for SX1276 and SX1262 LoRa chips. Implements `loraspi_init()` (opens spidev, configures chip, starts rx/tx threads), `loraspi_send_packet()` (encodes AX.25 to TNC2, prepends the LoRa APRS header, transmits), and `loraspi_apply_profile()` (copies a named hardware profile into `audio_s`). Linux-only; a no-op stub is compiled on non-Linux platforms. |
| `src/loraspi.h` | Public interface for `loraspi.c` — declares `loraspi_init()`, `loraspi_send_packet()`, and `loraspi_apply_profile()`. |

### Installer

| File | Purpose |
|------|---------|
| `install-lora.sh` | Raspberry Pi installer: dependencies (including `libgpiod-dev`), build, SPI enable, a starter `~/direwolf.conf` and a systemd service. `--upgrade` rebuilds and restarts whichever service is active (`direwolf` or a custom name). The generated service runs as the login user and pipes `rtl_fm` into Dire Wolf, so it assumes an RTL-SDR dongle. |

### Documentation

| File | Purpose |
|------|---------|
| `doc/LoRa-APRS.md` | End-user setup guide — native SPI driver. |
| `doc/LoRa-Implementation.md` | This file. |

---

## LoRa APRS packet format

LoRa APRS uses standard TNC2 format with a 3-byte header prepended:

```
0x3C 0xFF 0x01  <TNC2 text>
```

Example:
```
3c ff 01 4b 36 41 54 56 2d 31 3e 41 50 4c 45 54 4b ...
         K  6  A  T  V  -  1  >  A  P  L  E  T  K ...
```

The header (`<\xff\x01`) is required by all LoRa APRS firmware (ESP32 iGates,
trackers, etc.). The native SPI driver strips it on receive and prepends it on transmit. (This is the LoRa APRS header, not the radio's PHY preamble.)

---

## Building Dire Wolf with LoRa support

No special build flags are required — `loraspi.c` is unconditionally included in the
build. On non-Linux platforms `loraspi.c` compiles to empty stubs; the runtime SPI
code is guarded by `#ifdef __linux__`.

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

The native SPI driver (`LCHANNEL`) is activated at runtime only when an `LCHANNEL`
directive appears in `direwolf.conf`.

---

## Notes for upstream contribution

The modified and new C source files form a self-contained patch suitable for a pull
request to WB2OSZ/direwolf.

**Modified files** (8):
`src/audio.h`, `src/config.c`, `src/direwolf.c`, `src/tq.c`,
`src/beacon.c`, `src/digipeater.c`, `src/server.c`, `src/CMakeLists.txt`

**New C files** (2): `src/loraspi.c`, `src/loraspi.h`

The installer script and documentation are independent of the Dire Wolf
build and can be contributed separately or maintained in this fork.

Key design decisions that upstream reviewers may ask about:

- **Native SPI driver (`LCHANNEL`)** — `loraspi.c` opens `/dev/spidevX.Y` directly via
  `ioctl` and controls reset/IRQ/TX_EN/RX_EN pins via libgpiod. No external Python
  library is required. Two threads per channel: rx_thread polls the chip every 10 ms (about 100 Hz; the IRQ pins in the profiles are not used for
  receive) and injects received frames via `dlq_rec_frame()`; tx_thread pulls from a semaphore-triggered
  queue and calls `chip_transmit()`. A per-channel `pthread_mutex_t` serialises all
  SPI bus access between the two threads. After TX completes the chip drops to STDBY;
  `chip_start_rx()` is called immediately to re-arm continuous receive mode.

- **GPIO implementation** — GPIO is controlled via libgpiod (preferred) with a fallback
  to the legacy sysfs interface when libgpiod is not available at build time. libgpiod
  supports Pi Zero 2 W, Pi 3, Pi 4, and Pi 5 without any pin offset calculation.
  Both libgpiod v1 (Debian Bullseye/Bookworm) and v2 (Debian Trixie and later) are
  supported via compile-time version detection (`LIBGPIOD_VERSION_MAJOR`).
  The running user must be in the `gpio` group, or Dire Wolf must run as root, for
  libgpiod to claim GPIO lines. If GPIO setup fails, Dire Wolf prints an error for each
  failed pin and continues — but TX power and RX sensitivity will be degraded on hats
  with external PA/LNA (TX_EN and RX_EN will not be driven).

  **Important:** the legacy sysfs GPIO interface does not release exported pins when the
  process exits. If Dire Wolf previously ran with sysfs GPIO, those pins will show
  `consumer="sysfs"` in `gpioinfo` and libgpiod will be unable to claim them. To clear:
  find the chip base offset (`ls /sys/class/gpio/`), then unexport each pin
  (`echo <base+bcm> | sudo tee /sys/class/gpio/unexport`).

- **`MEDIUM_LORA` channel medium** — added to `enum medium_e` in `audio.h` alongside
  the existing `MEDIUM_RADIO`, `MEDIUM_NETTNC`, `MEDIUM_SERTNC`. The code that
  whitelists those three mediums for beaconing, digipeating, the IGate and the AGW port list was
  extended to include `MEDIUM_LORA`. Connected mode (`cdigipeater.c`, `ax25_link.c`) was not: it
  is limited to radio channels.

- **Virtual channel numbering** — `LCHANNEL` assigns channels in the range
  `[MAX_RADIO_CHANS, MAX_TOTAL_CHANS)`, the same range used by `NCHANNEL` and
  `SCHANNEL`.

- **Hardware profiles** — `loraspi_apply_profile()` copies pin/chip settings from a
  compile-time table in `loraspi.c` into `audio_s`. This avoids per-board `#ifdef`
  chains and keeps board-specific data in one place. Future improvement: read profiles
  from an external file (e.g. `hardware_profiles.yaml`) at runtime so users can add
  new hat support without recompiling.

- <a id="tx-power-clamping"></a>**TX power clamping** — each profile includes a `max_tx_power_dbm` field representing
  the maximum safe value for the `LORATXPOWER` directive. On modules with an external PA
  (e.g. 33S variants), this is the maximum safe input to the SX1262 register, not the RF
  output power. `loraspi_init()` compares `LORATXPOWER` against this limit at startup and
  clamps it, printing an error if the user set a value above the limit (the 20 dBm default is
  capped quietly). This prevents users from accidentally
  over-driving the external PA and damaging the module.

---

## Adding support for a new LoRa hat

Hardware profiles are defined in the `s_profiles[]` array in `src/loraspi.c`. Each row defines the chip type, SPI bus, and GPIO pin assignments for one hat.

To add a new profile, add a row following this format:

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
| max_tx_power_dbm | Maximum safe LORATXPOWER register input value. Dire Wolf clamps to it. For modules with an external PA (33S variants) this is the SX1262 input level, not the RF output power: the 33S limit is 8, and a higher setting can damage the PA. |

Pin numbers are BCM GPIO numbers; check the hat's schematic for the assignments. Then rebuild and reinstall:

```bash
cd ~/direwolf
make -C build -j$(nproc)
sudo make -C build install
```

and set `LORAHW your_profile_name` in direwolf.conf.
