#!/usr/bin/env python3
"""
lora_sdr_bridge.py - LoRa APRS SDR bridge: GNU Radio -> Dire Wolf via KISS TCP.

Architecture
------------
RTL-SDR -> GNU Radio (gr-lora_sdr) -> lora_sdr_bridge.py <-- Dire Wolf (NCHANNEL)

The bridge acts as a KISS TCP *server*.  Dire Wolf connects to it as a client
using the NCHANNEL directive in direwolf.conf:

    NCHANNEL 10  127.0.0.1  8002

Each decoded LoRa packet is:
  1. Validated (TNC2 header check — drops corrupted SDR decodes)
  2. Encoded as a minimal AX.25 UI frame
  3. Wrapped in KISS framing and sent to the connected Dire Wolf instance

RX-only
-------
RTL-SDR is a receive-only device.  Any KISS TX frames sent by Dire Wolf
(e.g. beacons, digipeated frames) are read, decoded for display, and dropped.

Configuration (~/lora.conf)
---------------------------
    LORAFREQ    433.775      # MHz
    LORABW      125          # kHz: 125, 250, 500
    LORASF      12           # spreading factor 6-12
    LORACR      5            # coding rate 5-8
    LORASW      0x12         # sync word (0x12 private, 0x34 LoRa-APRS)
    SDRDEVICE   0            # RTL-SDR device index
    SDRGAIN     40           # tuner gain dB (0 = auto)
    SDRSAMPLERATE 1000000    # IQ sample rate
    KISSPORT    8002         # TCP port this bridge listens on (Dire Wolf connects here)

direwolf.conf:
    NCHANNEL 10  127.0.0.1  8002

Requirements
------------
    pip3 install pyyaml
    # GNU Radio with gr-lora_sdr (see doc/LoRa-SDR.md for install steps)

Usage
-----
    python3 lora_sdr_bridge.py
    python3 lora_sdr_bridge.py -c /path/to/lora.conf
    python3 lora_sdr_bridge.py --log-level DEBUG
"""

import argparse
import logging
import os
import shutil
import socket
import struct
import sys
import textwrap
import threading
import time

log = logging.getLogger("lora_sdr_bridge")


# ---------------------------------------------------------------------------
# Colored logging — Dire Wolf color scheme
# ---------------------------------------------------------------------------

_ANSI_GREEN   = '\033[1;32m'
_ANSI_MAGENTA = '\033[1;35m'
_ANSI_RED     = '\033[1;31m'
_ANSI_DKGREEN = '\033[0;32m'
_ANSI_RESET   = '\033[0m'


class _ColorFormatter(logging.Formatter):
    """
    Applies Dire Wolf-compatible ANSI colors to status/error log output.
    Packet RX/TX lines are printed via _dw_print() instead.
    Colors are suppressed when stderr is not a TTY.
    """
    def format(self, record):
        msg = super().format(record)
        if not sys.stderr.isatty():
            return msg
        if record.levelno >= logging.WARNING:
            return _ANSI_RED + msg + _ANSI_RESET
        if record.levelno == logging.DEBUG:
            return _ANSI_DKGREEN + msg + _ANSI_RESET
        return msg


def _dw_print(heard, packet, color=None):
    """
    Print a packet in Dire Wolf style (two lines, no timestamp):

      K6ATV-1  LoRa SDR  SNR=5.0dB
      [LoRa] K6ATV-1>APLETK,WIDE1-1*:!3707.01N/12134.58W>LoRa APRS

    Long packet lines are wrapped at the terminal width.
    """
    is_tty   = sys.stderr.isatty()
    lora_pfx = '[LoRa] '
    term_w   = shutil.get_terminal_size((80, 24)).columns if is_tty else 0

    if term_w > len(lora_pfx) + 20:
        packet_line = textwrap.fill(
            lora_pfx + packet,
            width=term_w,
            subsequent_indent=' ' * len(lora_pfx),
        )
    else:
        packet_line = lora_pfx + packet

    out = heard + '\n' + packet_line + '\n'
    if is_tty and color:
        sys.stderr.write(color + out + _ANSI_RESET)
    else:
        sys.stderr.write(out)
    sys.stderr.flush()


# ---------------------------------------------------------------------------
# Config parser
# ---------------------------------------------------------------------------

def parse_lora_conf(path):
    """Parse a lora.conf file.  Returns a dict with all keys lowercased."""
    cfg = {}
    try:
        with open(path) as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                parts = line.split(None, 1)
                if len(parts) == 2:
                    cfg[parts[0].lower()] = parts[1].strip()
    except FileNotFoundError:
        log.error("Config file not found: %s", path)
        sys.exit(1)
    return cfg


# ---------------------------------------------------------------------------
# TNC2 validation
# ---------------------------------------------------------------------------

def _valid_tnc2_header(text):
    """
    Return True if text has a valid TNC2 address header before the payload.
    TNC2 format: SOURCE>DEST[,PATH...]:payload
    """
    colon = text.find(':')
    if colon < 3:
        return False
    header = text[:colon]
    allowed = set('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789>,-*')
    if not all(c in allowed for c in header):
        return False
    gt = header.find('>')
    if gt < 1:
        return False
    call = header[:gt]
    dash = call.find('-')
    base = call[:dash] if dash != -1 else call
    ssid = call[dash + 1:] if dash != -1 else ''
    if not (1 <= len(base) <= 6):
        return False
    if ssid and not (ssid.isdigit() and 0 <= int(ssid) <= 15):
        return False
    return True


# ---------------------------------------------------------------------------
# AX.25 minimal encoder (TNC2 text -> binary UI frame)
# ---------------------------------------------------------------------------

def _encode_addr(callsign, ssid, is_last, h_bit=False):
    """
    Encode one AX.25 address field (7 bytes).
    callsign — up to 6 chars, padded with spaces
    ssid     — 0-15
    is_last  — True for the last address in the sequence
    h_bit    — True if the digipeater has-been-repeated flag is set
    """
    call = callsign.upper().ljust(6)[:6]
    encoded = bytes(ord(c) << 1 for c in call)
    ssid_byte = 0x60 | ((ssid & 0x0F) << 1)
    if h_bit:
        ssid_byte |= 0x80
    if is_last:
        ssid_byte |= 0x01
    return encoded + bytes([ssid_byte])


def _split_callsign(token):
    """Split 'CALL-SSID*' into (callsign, ssid_int, repeated_flag)."""
    repeated = token.endswith('*')
    token = token.rstrip('*')
    if '-' in token:
        call, ssid_str = token.split('-', 1)
        ssid = int(ssid_str) if ssid_str.isdigit() else 0
    else:
        call = token
        ssid = 0
    return call, ssid, repeated


def tnc2_to_ax25(tnc2):
    """
    Convert a TNC2 text frame to a minimal AX.25 UI binary frame.
    Returns bytes, or None if the frame cannot be encoded.

    AX.25 address order: DST, SRC, DIGI...
    Control: 0x03 (UI), PID: 0xF0 (no layer 3)
    """
    colon = tnc2.find(':')
    if colon < 0:
        return None
    header = tnc2[:colon]
    info   = tnc2[colon + 1:]

    gt = header.find('>')
    if gt < 0:
        return None

    src_token  = header[:gt]
    rest       = header[gt + 1:]
    parts      = rest.split(',')
    dst_token  = parts[0]
    digi_tokens = parts[1:]

    num_addrs = 2 + len(digi_tokens)

    dst_call, dst_ssid, _   = _split_callsign(dst_token)
    src_call, src_ssid, src_rep = _split_callsign(src_token)

    dst_bytes = _encode_addr(dst_call, dst_ssid, is_last=(num_addrs == 2))
    src_bytes = _encode_addr(src_call, src_ssid,
                             is_last=(len(digi_tokens) == 0),
                             h_bit=src_rep)

    digi_bytes = b''
    for i, tok in enumerate(digi_tokens):
        d_call, d_ssid, d_rep = _split_callsign(tok)
        digi_bytes += _encode_addr(d_call, d_ssid,
                                   is_last=(i == len(digi_tokens) - 1),
                                   h_bit=d_rep)

    return dst_bytes + src_bytes + digi_bytes + b'\x03\xf0' + info.encode('ascii', errors='replace')


# ---------------------------------------------------------------------------
# KISS framing
# ---------------------------------------------------------------------------

KISS_FEND  = 0xC0
KISS_FESC  = 0xDB
KISS_TFEND = 0xDC
KISS_TFESC = 0xDD


def kiss_wrap(data, channel=0):
    """Wrap raw AX.25 data in a KISS frame for the given channel."""
    escaped = bytearray()
    for b in data:
        if b == KISS_FEND:
            escaped += bytes([KISS_FESC, KISS_TFEND])
        elif b == KISS_FESC:
            escaped += bytes([KISS_FESC, KISS_TFESC])
        else:
            escaped.append(b)
    type_byte = (channel & 0x0F) << 4   # data frame = high nibble channel, low nibble 0
    return bytes([KISS_FEND, type_byte]) + bytes(escaped) + bytes([KISS_FEND])


def kiss_unwrap(frame):
    """
    Unwrap a KISS frame (after stripping the surrounding FEND bytes).
    Returns (channel, payload_bytes) or (None, None) on error.
    """
    if len(frame) < 1:
        return None, None
    channel = (frame[0] >> 4) & 0x0F
    raw = frame[1:]
    out = bytearray()
    i = 0
    while i < len(raw):
        b = raw[i]
        if b == KISS_FESC:
            i += 1
            if i < len(raw):
                nb = raw[i]
                out.append(KISS_FEND if nb == KISS_TFEND else
                           KISS_FESC if nb == KISS_TFESC else nb)
        else:
            out.append(b)
        i += 1
    return channel, bytes(out)


# ---------------------------------------------------------------------------
# KISS TCP server — Dire Wolf connects here via NCHANNEL
# ---------------------------------------------------------------------------

class KissServer:
    """
    Listen on a TCP port for Dire Wolf to connect as a KISS client.

    Dire Wolf uses:   NCHANNEL 10  127.0.0.1  <KISSPORT>
    This bridge uses: KissServer(port=<KISSPORT>)

    Only one client at a time is expected (Dire Wolf).
    """

    def __init__(self, port):
        self._port   = port
        self._client = None      # currently connected socket
        self._lock   = threading.Lock()
        self._stop   = threading.Event()

    def start(self):
        t = threading.Thread(target=self._accept_loop, daemon=True, name='kiss-srv')
        t.start()

    def _accept_loop(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(('', self._port))
        srv.listen(1)
        srv.settimeout(1.0)
        log.info("KISS server listening on port %d — waiting for Dire Wolf (NCHANNEL)", self._port)

        while not self._stop.is_set():
            try:
                conn, addr = srv.accept()
            except socket.timeout:
                continue
            except OSError:
                break

            log.info("Dire Wolf connected from %s:%d", *addr)
            with self._lock:
                self._client = conn

            self._read_from_direwolf(conn)

            log.info("Dire Wolf disconnected")
            with self._lock:
                self._client = None
            try:
                conn.close()
            except OSError:
                pass

        srv.close()

    def _read_from_direwolf(self, conn):
        """Read KISS frames from Dire Wolf (TX path) — display and drop."""
        buf = b''
        while not self._stop.is_set():
            try:
                data = conn.recv(512)
                if not data:
                    break
                buf += data
                # Extract complete KISS frames delimited by FEND (0xC0)
                while KISS_FEND in buf:
                    start = buf.find(KISS_FEND)
                    end   = buf.find(KISS_FEND, start + 1)
                    if end < 0:
                        buf = buf[start:]
                        break
                    frame_raw = buf[start + 1:end]
                    buf = buf[end:]
                    if not frame_raw:
                        continue
                    _chan, ax25 = kiss_unwrap(frame_raw)
                    if ax25 and len(ax25) > 14:
                        # Decode destination for display (bytes 0-5, shifted right 1)
                        dst = ''.join(chr(b >> 1) for b in ax25[:6]).strip()
                        _dw_print(
                            dst + "  (TX dropped — SDR is RX-only)",
                            ax25[14:].decode('ascii', errors='replace'),
                            _ANSI_MAGENTA,
                        )
            except OSError:
                break

    def send_kiss(self, tnc2):
        """
        Encode TNC2 text as AX.25, wrap in KISS, send to connected Dire Wolf.
        No-op if Dire Wolf is not connected.
        """
        with self._lock:
            conn = self._client
        if conn is None:
            log.warning("Dire Wolf not connected — RX packet dropped")
            return

        ax25 = tnc2_to_ax25(tnc2)
        if ax25 is None:
            log.warning("AX.25 encode failed for: %s", tnc2[:60])
            return

        frame = kiss_wrap(ax25)
        try:
            conn.sendall(frame)
        except OSError as e:
            log.error("Send to Dire Wolf failed: %s", e)
            with self._lock:
                self._client = None

    def stop(self):
        self._stop.set()
        with self._lock:
            if self._client:
                try:
                    self._client.close()
                except OSError:
                    pass


# ---------------------------------------------------------------------------
# SDR bridge main class
# ---------------------------------------------------------------------------

class LoRaSdrBridge:
    """
    Wires the GNU Radio SDR flowgraph to the Dire Wolf KISS server.

    On each decoded LoRa payload:
      1. Strip leading non-printable bytes (hardware preamble artifacts)
      2. Validate TNC2 header — drop corrupted decodes
      3. Encode as AX.25 UI frame, wrap in KISS, forward to Dire Wolf
    """

    def __init__(self, cfg):
        self._cfg  = cfg
        self._kiss = KissServer(port=int(cfg.get('kissport', 8002)))
        self._fg   = None

    def _on_packet(self, payload_bytes, snr=None):
        """Called by the GNU Radio flowgraph for each decoded LoRa frame."""
        # Strip leading non-printable / preamble bytes.
        clean = b''
        for i, b in enumerate(payload_bytes):
            if (0x30 <= b <= 0x39 or 0x41 <= b <= 0x5a):  # 0-9 or A-Z
                clean = payload_bytes[i:]
                break
        if not clean:
            log.debug("Received empty or all-preamble packet — skipped")
            return

        try:
            line = clean.decode('ascii', errors='replace').strip()
        except Exception as e:
            log.warning("Could not decode payload: %s", e)
            return

        if not line:
            return

        if not _valid_tnc2_header(line):
            safe = ''.join(c if c.isprintable() else '?' for c in line)
            log.warning("Dropping packet with invalid TNC2 header (corrupted decode): %s", safe)
            return

        snr_str = f"  SNR={snr:.1f}dB" if snr is not None else ""
        src = line.split('>')[0] if '>' in line else line[:9]
        _dw_print(src + "  LoRa SDR" + snr_str, line, _ANSI_GREEN)

        self._kiss.send_kiss(line)

    def start(self):
        """Start KISS server and GNU Radio flowgraph.  Blocks until stopped."""
        from lora_sdr_flowgraph import LoRaSdrFlowgraph

        self._kiss.start()

        try:
            self._fg = LoRaSdrFlowgraph(
                freq_mhz    = float(self._cfg.get('lorafreq',     '433.775')),
                bw          = int(  self._cfg.get('lorabw',        '125')),
                sf          = int(  self._cfg.get('lorasf',        '12')),
                cr          = int(  self._cfg.get('loracr',        '5')),
                sw          = int(  self._cfg.get('lorasw',        '0x12'), 16),
                device_index= int(  self._cfg.get('sdrdevice',     '0')),
                gain        = float(self._cfg.get('sdrgain',       '40')),
                sample_rate = int(  self._cfg.get('sdrsamplerate', '1000000')),
                callback    = self._on_packet,
            )
        except ImportError as e:
            log.error("Cannot start SDR flowgraph: %s", e)
            sys.exit(1)

        self._fg.start()
        log.info(
            "LoRa SDR bridge running — %.3f MHz SF%s BW%s kHz — KISS port %s",
            float(self._cfg.get('lorafreq', '433.775')),
            self._cfg.get('lorasf', '12'),
            self._cfg.get('lorabw', '125'),
            self._cfg.get('kissport', '8002'),
        )
        log.info("Add to direwolf.conf:  NCHANNEL <chan>  127.0.0.1  %s",
                 self._cfg.get('kissport', '8002'))

        try:
            while self._fg.running:
                time.sleep(1)
        except (KeyboardInterrupt, SystemExit):
            pass
        finally:
            self.stop()

    def stop(self):
        if self._fg and self._fg.running:
            self._fg.stop()
        self._kiss.stop()
        log.info("LoRa SDR bridge stopped")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="LoRa APRS SDR bridge — RTL-SDR/GNU Radio -> Dire Wolf (NCHANNEL)"
    )
    parser.add_argument(
        '-c', '--config',
        default=os.path.join(os.path.expanduser('~'), 'lora.conf'),
        help='Path to lora.conf (default: ~/lora.conf)',
    )
    parser.add_argument(
        '--log-level',
        default='INFO',
        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
        help='Logging verbosity',
    )
    args = parser.parse_args()

    handler = logging.StreamHandler()
    handler.setFormatter(_ColorFormatter(
        fmt='%(asctime)s %(levelname)-8s %(name)s: %(message)s',
        datefmt='%H:%M:%S',
    ))
    logging.root.setLevel(getattr(logging, args.log_level))
    logging.root.addHandler(handler)

    cfg = parse_lora_conf(args.config)
    log.debug("Config loaded from %s: %s", args.config, cfg)

    bridge = LoRaSdrBridge(cfg)

    try:
        import signal
        signal.signal(signal.SIGTERM, lambda s, f: bridge.stop() or sys.exit(0))
        signal.signal(signal.SIGINT,  lambda s, f: bridge.stop() or sys.exit(0))
    except (ValueError, OSError):
        pass

    bridge.start()


if __name__ == '__main__':
    main()
