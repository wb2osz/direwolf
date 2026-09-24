#!/bin/bash
# install-lora.sh — Dire Wolf LoRa APRS installer
#
# Supported hardware:
#   Pi 3  — direwolf + native SPI driver (LCHANNEL) + VHF APRS via RTL-SDR
#           GNU Radio / gr-lora_sdr SDR path skipped (too CPU-intensive for Pi 3)
#   Pi 4  — full install: all of the above + GNU Radio + gr-lora_sdr + SDR bridge
#   Pi 5  — same as Pi 4 (not yet fully tested)
#
# What this script does:
#   1. Detects Raspberry Pi model
#   2. Installs build dependencies
#   3. Enables SPI (required for LoRa hat)
#   4. Builds and installs Dire Wolf from source
#   5. Installs bridge scripts
#   6. On Pi 4/5: blacklists DVB kernel driver, builds gr-lora_sdr, fixes Python path
#   7. Prompts for callsign, passcode, location, hardware profile, frequency
#   8. Writes ~/direwolf.conf and ~/lora.conf (home directory of the invoking user)
#   9. Installs and enables systemd services
#
# Usage:
#   Fresh install:
#     git clone https://github.com/radiohound/direwolf.git
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
        warn "Pi 3 detected — SDR path will be skipped (too CPU-intensive)."
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
    echo "  meshadv_900m30s  MeshAdv-Pi Hat with E22-900M30S (SX1262, 868/915 MHz, 1W)" > /dev/tty
    echo "  meshadv_900m33s  MeshAdv-Pi Hat with E22-900M33S (SX1262, 868/915 MHz, 2W)" > /dev/tty
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
        printf "LoRa frequency MHz (433.775 MHz is standard worldwide): " > /dev/tty
        read -r LORAFREQ < /dev/tty || true
        [ -z "$LORAFREQ" ] && warn "Frequency cannot be empty."
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
        libgpiod-dev \
        python3-pip

    info "Installing Python packages..."
    pip3 install --break-system-packages pyyaml LoRaRF 2>/dev/null \
        || pip3 install pyyaml LoRaRF

    # RTL-SDR tools needed on all Pi models for VHF APRS via rtl_fm
    info "Installing RTL-SDR tools..."
    apt-get install -y rtl-sdr

    if [ "$PI_MODEL" -ge 4 ]; then
        info "Installing GNU Radio..."
        apt-get install -y gnuradio gr-osmosdr

        info "Installing gr-lora_sdr build dependencies..."
        apt-get install -y \
            libboost-all-dev \
            pybind11-dev python3-pybind11
    fi
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
# RTL-SDR kernel driver blacklist (Pi 4/5 only)
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
# Build and install gr-lora_sdr (Pi 4/5 only)
# ---------------------------------------------------------------------------

install_gr_lora_sdr() {
    if python3 -c "from gnuradio import lora_sdr" 2>/dev/null; then
        info "gr-lora_sdr already installed — skipping build."
    else
        info "Building gr-lora_sdr from source (this will take a while)..."
        local build_dir
        build_dir=$(mktemp -d)
        git clone --depth=1 https://github.com/tapparelj/gr-lora_sdr.git "$build_dir/gr-lora_sdr"
        cmake -S "$build_dir/gr-lora_sdr" -B "$build_dir/gr-lora_sdr/build" \
            -DCMAKE_INSTALL_PREFIX=/usr
        make -C "$build_dir/gr-lora_sdr/build" -j"$(nproc)"
        make -C "$build_dir/gr-lora_sdr/build" install
        ldconfig
    fi

    # cmake installs to site-packages which Debian excludes from sys.path.
    # Add a .pth file so the import works without PYTHONPATH.
    local site_pkg
    site_pkg=$(python3 -c "
import sys
candidates = [p for p in sys.path if 'site-packages' in p and p]
print(candidates[-1] if candidates else '')
" 2>/dev/null || true)
    if [ -n "$site_pkg" ] && [ -d "$site_pkg" ]; then
        echo "$site_pkg" > /usr/lib/python3/dist-packages/lora_sdr.pth
        info "Added $site_pkg to Python path via lora_sdr.pth"
    fi

    python3 -c "from gnuradio import lora_sdr; print('lora_sdr OK')" \
        || error "gr-lora_sdr installed but Python import failed. Check PYTHONPATH."
    info "gr-lora_sdr ready."
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
# Install bridge scripts
# ---------------------------------------------------------------------------

install_scripts() {
    local src_dir
    src_dir="$(cd "$(dirname "$0")" && pwd)"

    info "Installing bridge scripts..."

    if [ "$PI_MODEL" -ge 4 ]; then
        install -m 755 "$src_dir/scripts/lora_sdr_bridge.py"    /usr/local/bin/lora_sdr_bridge.py
        install -m 755 "$src_dir/scripts/lora_sdr_flowgraph.py" /usr/local/bin/lora_sdr_flowgraph.py
    fi
}

# ---------------------------------------------------------------------------
# Write configuration files
# ---------------------------------------------------------------------------

write_configs() {
    local conf="$HOME_DIR/direwolf.conf"
    local lora_conf="$HOME_DIR/lora.conf"

    info "Writing $conf ..."

    if [ "$PI_MODEL" -ge 4 ]; then
        # Pi 4/5: VHF APRS via RTL-SDR on channel 0, LoRa on channel 10
        cat > "$conf" << EOF
# Dire Wolf configuration — VHF APRS + LoRa APRS
# Generated by install-lora.sh

# VHF APRS via RTL-SDR (piped from rtl_fm on stdin)
ADEVICE stdin null
CHANNEL 0
MYCALL   $CALLSIGN
MODEM 1200

# LoRa SPI hat — native driver
# Must appear before any PBEACON lines referencing channel 10
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

# LoRa SDR bridge — RX only, Dire Wolf connects to bridge on port 8002
# Uncomment to enable SDR receive path alongside the LoRa hat
#NCHANNEL 11  127.0.0.1  8002
EOF
    else
        # Pi 3: VHF APRS via RTL-SDR + LoRa (GNU Radio SDR path skipped — too CPU-intensive)
        cat > "$conf" << EOF
# Dire Wolf configuration — VHF APRS + LoRa APRS
# Generated by install-lora.sh

# VHF APRS via RTL-SDR (piped from rtl_fm on stdin)
ADEVICE stdin null
CHANNEL 0
MYCALL   $CALLSIGN
MODEM 1200

# LoRa SPI hat — native driver
# Must appear before any PBEACON lines referencing channel 10
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
    fi

    chown "$LOGIN_USER:$LOGIN_USER" "$conf"

    info "Writing $lora_conf ..."
    cat > "$lora_conf" << EOF
# LoRa bridge configuration
# Generated by install-lora.sh

# Hardware profile — matches LORAHW in direwolf.conf
HARDWARE $HW_PROFILE

# RF parameters
LORAFREQ $LORAFREQ
LORABW   125
LORASF   12
LORACR   5
LORASW   0x12
LORATXPOWER 17

# TCP connection to Dire Wolf
KISSHOST 127.0.0.1
KISSPORT 8002
EOF

    if [ "$PI_MODEL" -ge 4 ]; then
        cat >> "$lora_conf" << EOF

# SDR receive settings (lora_sdr_bridge.py only)
SDRDEVICE     0
SDRGAIN       40
SDRSAMPLERATE 1000000
EOF
    fi

    chown "$LOGIN_USER:$LOGIN_USER" "$lora_conf"

    info "Configuration files written to $HOME_DIR/."
}

# ---------------------------------------------------------------------------
# systemd services
# ---------------------------------------------------------------------------

install_services() {
    info "Installing systemd service files..."

    local conf="$HOME_DIR/direwolf.conf"
    local lora_conf="$HOME_DIR/lora.conf"

    if [ "$PI_MODEL" -ge 4 ]; then
        # Pi 4/5: pipe rtl_fm into direwolf for VHF APRS alongside LoRa
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
    else
        # Pi 3: RTL-SDR pipe for VHF APRS (no GNU Radio LoRa SDR path)
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
    fi

    if [ "$PI_MODEL" -ge 4 ]; then
        # SDR bridge — optional, starts before Dire Wolf
        cat > /etc/systemd/system/lora-sdr-bridge.service << EOF
[Unit]
Description=LoRa APRS SDR Bridge (RTL-SDR + GNU Radio receive path)
After=network.target

[Service]
User=$LOGIN_USER
ExecStart=/usr/bin/python3 /usr/local/bin/lora_sdr_bridge.py -c $lora_conf
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
    fi

    info "Enabling services..."
    systemctl daemon-reload
    systemctl enable direwolf
    systemctl start direwolf

    if [ "$PI_MODEL" -ge 4 ]; then
        systemctl enable lora-sdr-bridge
        # lora-sdr-bridge is optional — don't start it unless NCHANNEL is uncommented
        info "lora-sdr-bridge service installed but not started."
        info "To enable SDR receive: uncomment NCHANNEL in $conf"
        info "then: sudo systemctl start lora-sdr-bridge && sudo systemctl restart direwolf"
    fi
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
    if [ "$PI_MODEL" -ge 4 ]; then
        blacklist_dvb
        install_gr_lora_sdr
    fi
    install_direwolf
    install_scripts
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
    if [ "$PI_MODEL" -ge 4 ]; then
        echo "  Monitor SDR bridge:     journalctl -u lora-sdr-bridge -f"
    fi
    echo "  Edit configuration:     $HOME_DIR/direwolf.conf"
    echo "======================================================"

    if [ "$REBOOT_REQUIRED" -eq 1 ]; then
        echo ""
        warn "SPI was just enabled. Please reboot before using the LoRa hat:"
        warn "  sudo reboot"
    fi
}

main "$@"
