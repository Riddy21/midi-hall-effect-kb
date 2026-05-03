#!/usr/bin/env python3
"""
Bridge raw MIDI bytes from an Arduino CDC serial port to a CoreMIDI virtual MIDI port.

Firmware must use **serial MIDI**, not plain **uno** (default **uno** is scan/calib + ASCII table only).
Build for the same board with **`midi_serial`** (alias of **uno_midi_serial**):

  pio run -e midi_serial -t upload

Python deps (use a venv on macOS / Homebrew Python — PEP 668):

  cd /path/to/midi-hall-effect-kb
  python3 -m venv .venv
  . .venv/bin/activate
  pip install -r tools/requirements-serial-midi.txt

Run:

  python tools/serial_midi_bridge.py --port /dev/cu.usbmodemXXXX

In **Audio MIDI Setup → MIDI Studio** you should see the virtual source; in your DAW, pick it
as a MIDI input. Stop **PlatformIO Serial Monitor** while this script runs.
Serial baud defaults to 115200 — match **`SERIAL_MONITOR_BAUD`** / `platformio.ini` **monitor_speed**
and firmware **midi_serial** (Uno CDC).

The script opens the port with **DTR/RTS de-asserted** so macOS does not constantly **reset** an
Arduino-class board (a common cause of “serial disconnected” / reconnect storms).
"""

from __future__ import annotations

import argparse
import signal
import sys
import time
from typing import List, Optional, Sequence

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Missing pyserial: pip install -r tools/requirements-serial-midi.txt", file=sys.stderr)
    raise

try:
    import rtmidi
except ImportError:
    print("Missing python-rtmidi: pip install -r tools/requirements-serial-midi.txt", file=sys.stderr)
    raise


def parse_channel_voice_messages(buffer: bytearray) -> Sequence[List[int]]:
    """
    Remove complete channel-voice/system-common short messages from the front of ``buffer``.
    Leaves an incomplete trailing message in ``buffer``.
    Drops unexpected bytes (< 0x80) unless they arrive as MIDI data bytes (caller should rely
    on firmware always emitting status); we skip orphan data bytes aggressively.
    """
    messages: List[List[int]] = []
    i = 0
    n = len(buffer)

    def need(more: int) -> bool:
        return i + more > n

    while i < n:
        status = buffer[i]
        # Single-byte real-time (allowed on wire; firmware usually won't send).
        if status >= 0xF8:
            messages.append([status])
            i += 1
            continue

        # Status byte required for firmware path; stray data skipped.
        if status < 0x80:
            i += 1
            continue

        # Ignore SysEx etc. safely (consume until terminator if present).
        if status == 0xF0:
            try:
                end = buffer.index(0xF7, i + 1)
            except ValueError:
                break
            messages.append(list(buffer[i : end + 1]))
            i = end + 1
            continue
        if 0xF1 <= status <= 0xF7:
            messages.append([status])
            i += 1
            continue

        hi = status & 0xF0
        if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
            if need(3):
                break
            messages.append([status, buffer[i + 1], buffer[i + 2]])
            i += 3
            continue
        if hi in (0xC0, 0xD0):
            if need(2):
                break
            messages.append([status, buffer[i + 1]])
            i += 2
            continue

        # Unknown status; skip one byte to resync.
        i += 1

    del buffer[:i]
    return messages


def default_usb_modem_port() -> Optional[str]:
    ports = list(serial.tools.list_ports.comports())
    arduino_vid = ("2341", "VID_2341")

    def is_arduino(p: serial.tools.list_ports.ListPortInfo) -> bool:
        hw = p.hwid or ""
        desc = (p.description or "").lower()
        return any(v in hw for v in arduino_vid) or "arduino" in desc

    usb_candidates: List[str] = []
    for p in ports:
        dev = p.device
        if "usbmodem" in dev.lower() or dev.startswith("COM"):
            if is_arduino(p):
                return dev
            usb_candidates.append(dev)
    return usb_candidates[0] if usb_candidates else None


def open_serial_cdc(port: str, baud: int, *, timeout_s: float) -> serial.Serial:
    """Open CDC serial without the usual DTR glitch that resets AVR/Rp2040 USB sketches."""
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = timeout_s
    ser.write_timeout = 0
    ser.dsrdtr = False
    ser.rtscts = False
    ser.xonxoff = False
    ser.open()
    try:
        ser.setDTR(False)
    except Exception:
        pass
    try:
        ser.setRTS(False)
    except Exception:
        pass
    try:
        ser.reset_input_buffer()
    except (OSError, serial.SerialException):
        pass
    return ser


def list_serial_ports() -> None:
    for p in serial.tools.list_ports.comports():
        print(f"{p.device}\t{p.description}\t{p.hwid}")


def run(port: str, baud: int, virtual_name: str, verbose: bool) -> None:
    midi_out = rtmidi.MidiOut()
    midi_out.open_virtual_port(virtual_name)

    buf = bytearray()

    print(f"MIDI bridge: serial {port} @ {baud} baud → virtual MIDI “{virtual_name}”")
    print("Ctrl+C to quit. Ensure Serial Monitor is closed.\n")

    backoff = 1.0
    while True:
        try:
            with open_serial_cdc(port, baud, timeout_s=0.05) as ser:
                backoff = 1.0
                while True:
                    try:
                        chunk = ser.read(512)
                    except (serial.SerialException, OSError):
                        raise
                    if chunk:
                        buf.extend(chunk)
                        for msg in parse_channel_voice_messages(buf):
                            midi_out.send_message(msg)
                            if verbose:
                                print(time.strftime("%H:%M:%S"), "→", msg)
                    # read() blocked up to timeout; no tight spin
        except (serial.SerialException, OSError) as e:
            print(f"[serial disconnected or busy] {e}", file=sys.stderr)
            print("Waiting to reconnect...", file=sys.stderr)
            time.sleep(backoff)
            backoff = min(backoff + 1.0, 5.0)


def main(argv: Sequence[str]) -> int:
    ap = argparse.ArgumentParser(description="Serial → virtual CoreMIDI bridge (flash midi_serial / uno_midi_serial first).")
    ap.add_argument(
        "--port",
        "-p",
        metavar="DEVICE",
        help="Serial device (e.g. /dev/cu.usbmodem323101). Omit to auto-pick cu.usbmodem*.",
    )
    ap.add_argument("--baud", "-b", type=int, default=115200, help="Serial baud (default 115200).")
    ap.add_argument(
        "--virtual-name",
        "-n",
        default="HallKB Serial MIDI",
        help="Name shown in MIDI Studio / DAW (default %(default)s).",
    )
    ap.add_argument("--list", action="store_true", help="List serial ports and exit.")
    ap.add_argument("-v", "--verbose", action="store_true", help="Print each forwarded message.")
    args = ap.parse_args(list(argv))

    if args.list:
        list_serial_ports()
        return 0

    port = args.port or default_usb_modem_port()
    if not port:
        print("No USB serial modem port found — use --list and --port", file=sys.stderr)
        return 1

    def handle_sigint(_sig, _frame):
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_sigint)

    run(port=port, baud=args.baud, virtual_name=args.virtual_name, verbose=args.verbose)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
