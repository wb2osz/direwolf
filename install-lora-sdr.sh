#!/bin/bash
# install-lora-sdr.sh — Dire Wolf LoRa APRS SDR receive path installer
#
# Supported hardware:
#   Pi 4  — full install (direwolf + GNU Radio + gr-lora_sdr + lora-sdr-bridge)
#   Pi 3  — not supported by this script (SDR path too CPU-intensive)
#   Pi 5  — not yet tested; use Pi 4 path at your own risk
#
# What this script does:
#   1. Detects Raspberry Pi model and aborts on unsupported hardware
#   2. Installs build dependencies and RTL-SDR tools
#   3. Blacklists the DVB kernel driver that conflicts with RTL-SDR
#   4. Builds and installs GNU Radio gr-lora_sdr from source
#   5. Builds and installs Dire Wolf from source
#   6. Prompts for callsign, APRS passcode, and location
#   7. Writes /etc/direwolf/direwolf.conf and /etc/direwolf/lora.conf
#   8. Installs and enables systemd services
#
# Usage:
#   git clone https://github.com/radiohound/direwolf.git
#   cd direwolf
#   git checkout feature/lora-spi
#   sudo bash install-lora-sdr.sh

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
    [[ $EUID -eq 0 ]] || error "This script must be run as root (use sudo)."
}

# ---------------------------------------------------------------------------
# Pi model detection
# ---------------------------------------------------------------------------

detect_pi_model() {
    local model
    model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)

    if echo "$model" | grep -q "Raspberry Pi 4"; then
        PI_MODEL=4
    elif echo "$model" | grep -q "Raspberry Pi 5"; then
        PI_MODEL=5
        warn "Pi 5 has not been fully tested with this installer. Proceeding anyway."
    elif echo "$model" | grep -q "Raspberry Pi 3"; then
        error "Pi 3 is not supported by this installer (SDR path requires Pi 4+). "\
              "For Pi 3, use the native SPI driver (LCHANNEL) path instead."
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
    echo "This information will be written to /etc/direwolf/direwolf.conf" > /dev/tty
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
    info "Callsign: $CALLSIGN  Passcode: $PASSCODE  Lat: $LAT  Lon: $LON"
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
        python3-pip

    info "Installing RTL-SDR tools..."
    apt-get install -y rtl-sdr

    info "Installing GNU Radio..."
    apt-get install -y gnuradio gr-osmosdr

    info "Installing gr-lora_sdr build dependencies..."
    apt-get install -y \
        libboost-all-dev \
        pybind11-dev python3-pybind11

    info "Installing Python packages..."
    pip3 install --break-system-packages pyyaml 2>/dev/null \
        || pip3 install pyyaml
}

# ---------------------------------------------------------------------------
# RTL-SDR kernel driver blacklist
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
# Build and install gr-lora_sdr
# ---------------------------------------------------------------------------

install_gr_lora_sdr() {
    if python3 -c "from gnuradio import lora_sdr" 2>/dev/null; then
        info "gr-lora_sdr already installed — skipping build."
        return
    fi

    info "Building gr-lora_sdr from source (this will take a while on Pi 4)..."
    local build_dir
    build_dir=$(mktemp -d)
    git clone --depth=1 https://github.com/tapparelj/gr-lora_sdr.git "$build_dir/gr-lora_sdr"
    cmake -S "$build_dir/gr-lora_sdr" -B "$build_dir/gr-lora_sdr/build" \
        -DCMAKE_INSTALL_PREFIX=/usr
    make -C "$build_dir/gr-lora_sdr/build" -j"$(nproc)"
    make -C "$build_dir/gr-lora_sdr/build" install
    ldconfig

    # cmake installs to site-packages which may not be in sys.path on Debian/Ubuntu.
    # Add a .pth file so Python finds it without needing PYTHONPATH.
    local site_pkg
    site_pkg=$(python3 -c "import sys; print([p for p in sys.path if 'python3' in p and 'dist-packages' not in p and p][-1])" 2>/dev/null || true)
    if [ -n "$site_pkg" ] && [ -d "$site_pkg" ]; then
        echo "$site_pkg" > /usr/lib/python3/dist-packages/lora_sdr.pth
        info "Added $site_pkg to Python path via lora_sdr.pth"
    fi

    python3 -c "from gnuradio import lora_sdr; print('lora_sdr OK')" \
        || error "gr-lora_sdr installed but Python import failed."
    info "gr-lora_sdr installed successfully."
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
    install -m 755 "$src_dir/scripts/lora_sdr_bridge.py"   /usr/local/bin/lora_sdr_bridge.py
    install -m 755 "$src_dir/scripts/lora_sdr_flowgraph.py" /usr/local/bin/lora_sdr_flowgraph.py
}

# ---------------------------------------------------------------------------
# Write configuration files
# ---------------------------------------------------------------------------

write_configs() {
    mkdir -p /etc/direwolf

    info "Writing /etc/direwolf/direwolf.conf ..."
    cat > /etc/direwolf/direwolf.conf << EOF
# Dire Wolf configuration — LoRa APRS SDR receive path
# Generated by install-lora-sdr.sh

MYCALL  $CALLSIGN

# No physical audio device (SDR-only setup)
ADEVICE null null

# LoRa SDR bridge — Dire Wolf connects to the bridge on port 8002
NCHANNEL 10  127.0.0.1  8002

# iGate — beacon position to APRS-IS
# Uncomment and configure IGSERVER/IGLOGIN to enable iGate operation
#IGSERVER noam.aprs2.net
#IGLOGIN  $CALLSIGN $PASSCODE
#PBEACON delay=1 every=30 sendto=IG overlay=L symbol="igate" lat=$LAT long=$LON comment="$CALLSIGN LoRa APRS SDR iGate"
EOF

    info "Writing /etc/direwolf/lora.conf ..."
    cat > /etc/direwolf/lora.conf << EOF
# LoRa SDR bridge configuration
# Generated by install-lora-sdr.sh

# RF parameters — must match the transmitting station
LORAFREQ      915.000
LORABW        125
LORASF        12
LORACR        5
LORASW        0x12

# RTL-SDR settings
SDRDEVICE     0
SDRGAIN       40
SDRSAMPLERATE 1000000

# TCP connection — bridge listens, Dire Wolf connects
KISSPORT  8002
EOF

    info "Configuration files written to /etc/direwolf/."
}

# ---------------------------------------------------------------------------
# systemd services
# ---------------------------------------------------------------------------

install_services() {
    local src_dir
    src_dir="$(cd "$(dirname "$0")" && pwd)"

    info "Installing systemd service files..."

    # Determine the real login user (not root)
    local login_user
    login_user=$(logname 2>/dev/null || echo "pi")

    # LoRa SDR bridge service — starts first, Dire Wolf connects to it
    cat > /etc/systemd/system/lora-sdr-bridge.service << EOF
[Unit]
Description=LoRa APRS SDR Bridge (RTL-SDR + GNU Radio receive path)
After=network.target

[Service]
User=$login_user
ExecStart=/usr/bin/python3 /usr/local/bin/lora_sdr_bridge.py -c /etc/direwolf/lora.conf
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

    # Dire Wolf service — starts after bridge is up, retries NCHANNEL connection
    cat > /etc/systemd/system/direwolf.service << EOF
[Unit]
Description=Dire Wolf APRS TNC
After=network.target lora-sdr-bridge.service
Wants=lora-sdr-bridge.service

[Service]
User=$login_user
ExecStartPre=/bin/sleep 3
ExecStart=/usr/local/bin/direwolf -c /etc/direwolf/direwolf.conf
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

    info "Enabling and starting services..."
    systemctl daemon-reload
    systemctl enable direwolf lora-sdr-bridge
    systemctl start direwolf lora-sdr-bridge

    info "Services started. Check status with:"
    info "  journalctl -u direwolf -f"
    info "  journalctl -u lora-sdr-bridge -f"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

main() {
    require_root
    detect_pi_model

    echo ""
    echo "======================================================"
    echo " Dire Wolf LoRa APRS SDR receive path installer"
    echo " Target: Raspberry Pi $PI_MODEL"
    echo "======================================================"
    echo ""

    prompt_config
    echo "DEBUG: prompt_config done"
    install_deps
    echo "DEBUG: install_deps done"
    blacklist_dvb
    install_gr_lora_sdr
    install_direwolf
    install_scripts
    write_configs
    install_services

    echo ""
    echo "======================================================"
    info "Installation complete."
    echo ""
    echo "  To monitor received packets:"
    echo "    journalctl -u direwolf -f"
    echo "    journalctl -u lora-sdr-bridge -f"
    echo ""
    echo "  To enable iGate (forward to APRS-IS), edit:"
    echo "    /etc/direwolf/direwolf.conf"
    echo "  and uncomment the IGSERVER/IGLOGIN/PBEACON lines."
    echo "======================================================"
}

main "$@"
