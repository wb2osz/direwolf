#!/bin/bash
# install-lora.sh — Dire Wolf LoRa APRS installer
#
# Supported hardware:
#   Pi 3, Pi 4, Pi 5 — direwolf + native SPI LoRa HAT driver (LCHANNEL)
#                      + VHF APRS via RTL-SDR (rtl_fm piped into Direwolf)
#   Pi 5 is not yet fully tested.
#
# What this script does:
#   1. Detects Raspberry Pi model
#   2. Installs build dependencies
#   3. Enables SPI (required for LoRa hat)
#   4. Builds and installs Dire Wolf from source
#   5. Blacklists the DVB kernel driver so rtl_fm can use the RTL-SDR dongle
#   6. Prompts for callsign, passcode, location, hardware profile, frequency
#   7. Writes ~/direwolf.conf (home directory of the invoking user);
#      all LoRa settings live in that one file
#   8. Installs and enables the systemd service
#
# Usage:
#   Fresh install:
#     git clone -b feature/lora-spi https://github.com/radiohound/direwolf.git
#   (After this work is merged upstream, clone wb2osz/direwolf instead.)
#     cd direwolf
#     sudo bash install-lora.sh
#
#   Upgrade (preserves existing config files):
#     cd direwolf
#     git pull
#     sudo bash install-lora.sh --upgrade

set -euo pipefail
trap 'echo "[ERROR] Script failed at line $LINENO" >&2' ERR

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

RED='\033[1;31m'
GRN='\033[1;32m'
YEL='\033[1;33m'
RST='\033[0m'

info()  { echo -e "${GRN}[INFO]${RST}  $*"; }
warn()  { echo -e "${YEL}[WARN]${RST}  $*"; }
error() { echo -e "${RED}[ERROR]${RST} $*" >&2; exit 1; }

require_root() {
    [ "$EUID" -eq 0 ] || error "This script must be run as root (use sudo)."
}

# ---------------------------------------------------------------------------
# Pi model detection
# ---------------------------------------------------------------------------

detect_pi_model() {
    local model
    model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)

    if echo "$model" | grep -q "Raspberry Pi 5"; then
        PI_MODEL=5
        warn "Pi 5 has not been fully tested with this installer. Proceeding as Pi 4."
    elif echo "$model" | grep -q "Raspberry Pi 4"; then
        PI_MODEL=4
    elif echo "$model" | grep -q "Raspberry Pi 3"; then
        PI_MODEL=3
    else
        warn "Could not detect Raspberry Pi model ('$model'). Proceeding as Pi 4."
        PI_MODEL=4
    fi

    info "Detected: $model (treating as Pi $PI_MODEL)"
}

# ---------------------------------------------------------------------------
# Prompt for configuration
# ---------------------------------------------------------------------------

prompt_config() {
    echo "" > /dev/tty
    echo "--- Station configuration ---" > /dev/tty
    echo "This information will be written to $HOME_DIR/direwolf.conf" > /dev/tty
    echo "" > /dev/tty

    CALLSIGN=""
    while [ -z "$CALLSIGN" ]; do
        printf "Your callsign (e.g. W1ABC-10): " > /dev/tty
        read -r CALLSIGN < /dev/tty || true
        CALLSIGN=$(echo "$CALLSIGN" | tr '[:lower:]' '[:upper:]')
        [ -z "$CALLSIGN" ] && warn "Callsign cannot be empty."
    done

    PASSCODE=""
    while [ -z "$PASSCODE" ]; do
        printf "APRS passcode for %s: " "$CALLSIGN" > /dev/tty
        read -r PASSCODE < /dev/tty || true
        [ -z "$PASSCODE" ] && warn "Passcode cannot be empty."
    done

    LAT=""
    while [ -z "$LAT" ]; do
        printf "Latitude  (decimal degrees, e.g. 37.0026, negative = south): " > /dev/tty
        read -r LAT < /dev/tty || true
        [ -z "$LAT" ] && warn "Latitude cannot be empty."
    done

    LON=""
    while [ -z "$LON" ]; do
        printf "Longitude (decimal degrees, e.g. -121.5852, negative = west): " > /dev/tty
        read -r LON < /dev/tty || true
        [ -z "$LON" ] && warn "Longitude cannot be empty."
    done

    echo "" > /dev/tty
    echo "Hardware profiles:" > /dev/tty
    echo "  meshadv_400m30s  MeshAdv-Pi Hat with E22-400M30S (SX1262, 433/470 MHz, 1W)" > /dev/tty
    echo "  meshadv_400m33s  MeshAdv-Pi Hat with E22-400M33S (SX1262, 433/470 MHz, 2W)" > /dev/tty
    echo "  lorapi_rfm95w    LoRa-Pi RFM95W (SX1276, 868/915 MHz)" > /dev/tty
    echo "  lorapi_rfm98w    LoRa-Pi RFM98W (SX1278, 433 MHz)" > /dev/tty
    echo "  generic_sx1276   Generic SX1276/SX1278 breakout" > /dev/tty
    echo "" > /dev/tty
    HW_PROFILE=""
    while [ -z "$HW_PROFILE" ]; do
        printf "Hardware profile: " > /dev/tty
        read -r HW_PROFILE < /dev/tty || true
        [ -z "$HW_PROFILE" ] && warn "Hardware profile cannot be empty."
    done

    LORAFREQ=""
    while [ -z "$LORAFREQ" ]; do
        printf "LoRa frequency MHz [433.775, the most common LoRa APRS frequency]: " > /dev/tty
        read -r LORAFREQ < /dev/tty || true
        [ -z "$LORAFREQ" ] && LORAFREQ="433.775"
    done

    echo "" > /dev/tty
    info "Callsign:  $CALLSIGN"
    info "Location:  $LAT, $LON"
    info "Hardware:  $HW_PROFILE  Frequency: $LORAFREQ MHz"
    printf "Continue? [Y/n] " > /dev/tty
    read -r yn < /dev/tty || true
    case "${yn}" in
        [nN]*) error "Aborted by user." ;;
    esac
}

# ---------------------------------------------------------------------------
# Package dependencies
# ---------------------------------------------------------------------------

install_deps() {
    info "Updating package lists..."
    apt-get update -qq

    info "Installing build tools and Dire Wolf dependencies..."
    apt-get install -y \
        git cmake build-essential \
        libsndfile1-dev libasound2-dev \
        libgps-dev gpsd \
        libhamlib-dev \
        libgpiod-dev

    # RTL-SDR tools needed on all Pi models for VHF APRS via rtl_fm
    info "Installing RTL-SDR tools..."
    apt-get install -y rtl-sdr
}

# ---------------------------------------------------------------------------
# Enable SPI
# ---------------------------------------------------------------------------

enable_spi() {
    if [ -e /dev/spidev0.0 ]; then
        info "SPI already enabled."
        return
    fi
    info "Enabling SPI interface..."
    if command -v raspi-config > /dev/null 2>&1; then
        raspi-config nonint do_spi 0
        info "SPI enabled. A reboot will be required after installation."
        REBOOT_REQUIRED=1
    else
        warn "raspi-config not found — enable SPI manually with: sudo raspi-config"
    fi
}

# ---------------------------------------------------------------------------
# RTL-SDR kernel driver blacklist (the DVB driver otherwise claims the dongle)
# ---------------------------------------------------------------------------

blacklist_dvb() {
    local conf=/etc/modprobe.d/rtlsdr.conf
    if grep -q "dvb_usb_rtl28xxu" "$conf" 2>/dev/null; then
        info "RTL-SDR DVB driver already blacklisted."
    else
        info "Blacklisting dvb_usb_rtl28xxu kernel driver..."
        echo "blacklist dvb_usb_rtl28xxu" > "$conf"
        modprobe -r dvb_usb_rtl28xxu 2>/dev/null || true
    fi
}

# ---------------------------------------------------------------------------
# Build and install Dire Wolf
# ---------------------------------------------------------------------------

install_direwolf() {
    local src_dir
    src_dir="$(cd "$(dirname "$0")" && pwd)"

    info "Building Dire Wolf from $src_dir ..."
    cmake -S "$src_dir" -B "$src_dir/build"
    make -C "$src_dir/build" -j"$(nproc)"
    make -C "$src_dir/build" install
    info "Dire Wolf installed to /usr/local/bin/direwolf."
}

# ---------------------------------------------------------------------------
# Write configuration files
# ---------------------------------------------------------------------------

write_configs() {
    local conf="$HOME_DIR/direwolf.conf"

    info "Writing $conf ..."

    cat > "$conf" << EOF
# Dire Wolf configuration — VHF APRS + LoRa APRS
# Generated by install-lora.sh

# VHF APRS via RTL-SDR (piped from rtl_fm on stdin)
ADEVICE stdin null
CHANNEL 0
MYCALL   $CALLSIGN
MODEM 1200

# ---- LoRa channel (native SPI HAT) ----
# Every line from LCHANNEL down to the next CHANNEL/LCHANNEL line applies to the LoRa channel.
# Keep this after the VHF channel settings and before any PBEACON that uses sendto=10.
# LORAHW picks the module wiring/TCXO and its maximum power; the other LORA* lines are radio settings.
LCHANNEL 10
MYCALL   $CALLSIGN
LORAHW   $HW_PROFILE
LORAFREQ $LORAFREQ
LORASF   12
LORABW   125
LORACR   5
LORASW   0x12
LORATXPOWER 17

# Position beacon over LoRa RF
PBEACON delay=1 every=30 sendto=10 overlay=L symbol="igate" lat=$LAT long=$LON comment="$CALLSIGN LoRa APRS"

# iGate — forward received packets to APRS-IS
IGSERVER noam.aprs2.net
IGLOGIN  $CALLSIGN $PASSCODE

# Also beacon position to APRS-IS
PBEACON delay=1 every=30 sendto=IG overlay=L symbol="igate" lat=$LAT long=$LON comment="$CALLSIGN LoRa APRS iGate"
EOF

    chown "$LOGIN_USER:$LOGIN_USER" "$conf"

    info "Configuration written to $conf."
}

# ---------------------------------------------------------------------------
# systemd services
# ---------------------------------------------------------------------------

install_services() {
    info "Installing systemd service file..."

    local conf="$HOME_DIR/direwolf.conf"

    # Pipe rtl_fm into direwolf for VHF APRS alongside LoRa
    cat > /etc/systemd/system/direwolf.service << EOF
[Unit]
Description=Dire Wolf - LoRa APRS igate + VHF APRS via RTL-SDR
After=network.target

[Service]
User=$LOGIN_USER
ExecStart=/bin/bash -c 'rtl_fm -f 144.390M -M fm -s 22050 -g 40 - | /usr/local/bin/direwolf -c $conf -r 22050 stdin'
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

    info "Enabling service..."
    systemctl daemon-reload
    systemctl enable direwolf
    systemctl start direwolf
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

REBOOT_REQUIRED=0
UPGRADE_MODE=0
LOGIN_USER=""
HOME_DIR=""

main() {
    # Parse arguments
    for arg in "$@"; do
        case "$arg" in
            --upgrade) UPGRADE_MODE=1 ;;
            *) error "Unknown argument: $arg" ;;
        esac
    done

    require_root

    # Determine the user who invoked sudo (config files go in their home dir)
    LOGIN_USER=$(logname 2>/dev/null || echo "${SUDO_USER:-pi}")
    HOME_DIR=$(eval echo "~$LOGIN_USER")

    detect_pi_model

    echo ""
    echo "======================================================"
    if [ "$UPGRADE_MODE" -eq 1 ]; then
        echo " Dire Wolf LoRa APRS upgrade"
    else
        echo " Dire Wolf LoRa APRS installer"
    fi
    echo " Target: Raspberry Pi $PI_MODEL"
    echo "======================================================"
    echo ""

    if [ "$UPGRADE_MODE" -eq 0 ]; then
        prompt_config
    fi
    install_deps
    enable_spi
    blacklist_dvb
    install_direwolf
    if [ "$UPGRADE_MODE" -eq 0 ]; then
        write_configs
        install_services
    else
        info "Upgrade mode — skipping config and service changes."
        info "Restarting Dire Wolf to pick up new binary..."
        # Restart whichever direwolf service is active on this system
        for svc in direwolf direwolf-chase; do
            if systemctl is-active --quiet "$svc"; then
                info "Restarting $svc..."
                systemctl restart "$svc" || true
                break
            fi
        done
    fi

    echo ""
    echo "======================================================"
    if [ "$UPGRADE_MODE" -eq 1 ]; then
        info "Upgrade complete."
    else
        info "Installation complete."
    fi
    echo ""
    echo "  Monitor Dire Wolf:      journalctl -u direwolf -f"
    echo "  Edit configuration:     $HOME_DIR/direwolf.conf"
    echo "======================================================"

    if [ "$REBOOT_REQUIRED" -eq 1 ]; then
        echo ""
        warn "SPI was just enabled. Please reboot before using the LoRa hat:"
        warn "  sudo reboot"
    fi
}

main "$@"
