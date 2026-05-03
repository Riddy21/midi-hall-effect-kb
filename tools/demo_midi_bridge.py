#!/usr/bin/env python3
"""
Demo MIDI bridge: hall-effect serial depth frames → MIDI (**piano** or **string**).

Firmware wire format (**24 bytes**, ver 3): see ``include/config.h``.

Modes:

  • **Piano** — All keys on **MIDI channel 1**. Hammer physics at Note On; pressure / CC74 follow depth × strike gain.

  • **String** — Same channel. **No expressive Note On velocity** (fixed **1** so MIDI stays valid); loudness is **only**
    pressure + **CC 11** (optional CC74), **EMA-smoothed** from depth. Trigger depth defaults to **0**. Hammer hidden.

GUI: **Mode** (Piano / String). Mapping: **Trigger**, **Hammer / dynamics**, **Expression**. **Monitor** tab: live wire-style value per key (read-only bar + integer).

CLI ``--mode``: ``piano`` (default), ``string``, ``monitor_only`` (normalized 0..1 rows), ``raw_keys``
(integer wire counts 0..strength-scale per key). Legacy ``mpe*`` names map to piano-style timing.

CLI / headless:
  python tools/demo_midi_bridge.py --no-gui -p PORT --mode piano
  python tools/demo_midi_bridge.py --no-gui -p PORT --mode raw_keys   # tab-separated wire counts k0..k9

Tkinter on Homebrew Python (macOS): GUI needs Tcl/Tk; install e.g. ``brew install python-tk@3.13``
and reuse the same ``python3`` binary.

pip install -r tools/requirements-demo-midi-bridge.txt
"""

from __future__ import annotations

import argparse
import math
import queue
import struct
import sys
import threading
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable

try:
    import serial
except ImportError:  # pragma: no cover
    print(
        "Missing dependency: pip install -r tools/requirements-demo-midi-bridge.txt",
        file=sys.stderr,
    )
    raise SystemExit(1) from None

try:
    import rtmidi
except ImportError:  # pragma: no cover
    print(
        "Missing dependency: pip install -r tools/requirements-demo-midi-bridge.txt",
        file=sys.stderr,
    )
    raise SystemExit(1) from None

DEFAULT_STRENGTH_SCALE = 1023
KEY_COUNT = 10


def _print_tkinter_missing_hint() -> None:
    v = f"{sys.version_info.major}.{sys.version_info.minor}"
    print(
        "Tkinter is not available for this Python (missing built-in _tkinter).\n"
        "The GUI cannot run until Tcl/Tk is linked for this interpreter.\n",
        file=sys.stderr,
    )
    if sys.platform == "darwin":
        print(
            f"  macOS (Homebrew): try matching your Python version, e.g.\n"
            f"    brew install python-tk@{v}\n"
            "  Then run this script again with the same `python3` command.\n",
            file=sys.stderr,
        )
    print(
        "Headless mode (no GUI):\n"
        "  python3 tools/demo_midi_bridge.py --no-gui -p /dev/cu.usbmodem… [--mode …]\n",
        file=sys.stderr,
    )


def ensure_tkinter() -> None:
    """Exit with stderr hints if ``tkinter`` cannot load (e.g. Homebrew python without python-tk)."""
    try:
        import tkinter as tk  # noqa: F401
        import tkinter.ttk as ttk  # noqa: F401
    except ImportError as e:
        _print_tkinter_missing_hint()
        raise SystemExit(1) from e


def tkinter_usable() -> bool:
    try:
        import tkinter  # noqa: F401
        import tkinter.ttk  # noqa: F401
    except ImportError:
        return False
    return True


SYNC = bytes((0xA5, 0x5A))
FRAME_LEN = 24
FRAME_VER_DEPTH_ONLY = 3

# String mode: Note On must use velocity ≥1 (0 means Note Off per MIDI). Loudness comes from CCs only.
STRING_NOTE_ON_VELOCITY = 1

# All voices share MIDI channel 1 (status byte channel nibble 0).
MIDI_VOICE_CHANNEL = 0

# Piano: Note Off slightly below trigger. String mode uses the trigger depth exactly (no extra margin).
NOTE_OFF_DEPTH_HYSTERESIS = 0.14


class MidiRoutingMode(str, Enum):
    """Host articulation layout (single MIDI channel for all keys)."""

    PIANO = "piano"
    STRING = "string"


def piano_note_on_threshold(note_on_depth: float) -> float:
    """Strict ``crossed_up`` needs a positive floor when trigger is 0 (unused at 0 in piano UI)."""
    return max(float(note_on_depth), 1e-4)


def string_note_on_cross(prev_v: float, v: float, note_on_depth: float) -> bool:
    """True when depth crosses upward past the trigger. At trigger 0, fire on first increase from rest at 0."""
    level = float(note_on_depth)
    if level <= 0.0:
        return prev_v <= 0.0 and v > prev_v
    return prev_v < level <= v


class BridgeMode(str, Enum):
    """CLI ``--mode`` presets."""

    PIANO = "piano"
    STRING = "string"
    MONITOR_ONLY = "monitor_only"
    RAW_KEYS = "raw_keys"


_LEGACY_CLI_MODES: dict[str, BridgeMode] = {
    "mpe_bottom_timed": BridgeMode.PIANO,
    "mpe_bottom_fixed": BridgeMode.PIANO,
    "mpe": BridgeMode.PIANO,
    "mpe_fixed_vel": BridgeMode.PIANO,
}


CLI_LEGACY_FIXED_VELOCITY: frozenset[str] = frozenset({"mpe_bottom_fixed", "mpe_fixed_vel"})


def parse_bridge_mode_arg(value: str) -> BridgeMode:
    if value in _LEGACY_CLI_MODES:
        return _LEGACY_CLI_MODES[value]
    return BridgeMode(value)


@dataclass
class BridgeMappingParams:
    """Live-adjustable mapping from depth → MIDI (updated every GUI tick / frame)."""

    enable_midi_output: bool = True
    timed_velocity: bool = True
    routing_mode: MidiRoutingMode = MidiRoutingMode.PIANO
    note_on_depth: float = 0.98
    dynamics: int = 100
    """Maps to MIDI velocity span (and scales expression while held)."""
    hammer_key_mass: float = 1.0
    """Higher → heavier key (same gesture yields quieter strikes)."""
    hammer_v_cap: float = 22.0
    """Approach rate (depth units / s) treated as “full hammer speed” contribution."""
    hammer_travel_gamma: float = 1.35  # >1 penalizes shallow partial travel more
    dynamics_contrast: float = 0.5
    """0 = gentle (soft strokes stay louder); 1 = strong (soft strokes much quieter)."""
    speed_smooth_tau_s: float = 0.048
    """EMA time constant for downward approach rate while armed."""
    expr_depth_low: float = 0.0
    expr_depth_high: float = 1.0
    expr_out_min: int = 0
    expr_out_max: int = 127
    send_pressure: bool = True
    send_cc74: bool = True
    send_cc11_expression: bool = True
    """String mode: CC 11 (expression) follows smoothed depth for volume swells."""
    string_volume_smooth_tau_s: float = 0.075
    """EMA time constant for string-mode volume (depth → CC); larger = smoother."""


@dataclass
class KeyVoiceState:
    held: bool = False
    prev_d: float = 0.0
    arm_mono_t: float | None = None
    arm_depth_sum: float = 0.0
    arm_disp_sum: float = 0.0
    """Sum of positive Δdepth while armed (stroke path length toward bottom)."""
    arm_samples: int = 0
    arm_prev_mono: float | None = None
    ema_approach_rate: float = 0.0
    """Smoothed downward depth velocity (depth units per second) during armed stroke."""
    press_gain01: float = 1.0
    """0..1 from stroke (speed + depth path); scales velocity and expression volume."""
    string_vol_ema: float = -1.0
    """String mode: smoothed volume (−1 = not holding note yet); after Note On, tracks MIDI 0..127 as float from 0."""


@dataclass
class EngineState:
    keys: list[KeyVoiceState] = field(default_factory=lambda: [KeyVoiceState() for _ in range(KEY_COUNT)])
    last_frame_mono: float | None = None


def open_serial_cdc(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.05
    ser.dsrdtr = False
    ser.rtscts = False
    ser.xonxoff = False
    ser.open()
    try:
        ser.reset_input_buffer()
    except (OSError, serial.SerialException):
        pass
    try:
        ser.setDTR(False)
        ser.setRTS(False)
    except (AttributeError, OSError, serial.SerialException):
        pass
    return ser


def xor_body(body: bytes) -> int:
    x = 0
    for b in body:
        x ^= b
    return x


def _consume_depth_frames(rx: bytearray, strength_scale: int):
    """Parse validated depth frames; mutates ``rx``. Yields ``(wire_counts, depths01)`` per frame."""
    sf = float(strength_scale)
    while True:
        i = rx.find(SYNC)
        if i < 0:
            if len(rx) > 4096:
                del rx[:-128]
            return
        if i > 0:
            del rx[:i]
        if len(rx) < FRAME_LEN:
            return
        pkt = bytes(rx[:FRAME_LEN])
        if xor_body(pkt[2:23]) != pkt[23]:
            del rx[:1]
            continue
        if pkt[2] != FRAME_VER_DEPTH_ONLY:
            del rx[:2]
            continue
        words = struct.unpack("<10H", pkt[3:23])
        if any(w > strength_scale for w in words):
            del rx[:1]
            continue
        del rx[:FRAME_LEN]
        counts = list(words)
        depths = [w / sf for w in words]
        yield counts, depths


def extract_frames(rx: bytearray, strength_scale: int):
    for _, depths in _consume_depth_frames(rx, strength_scale):
        yield depths


def extract_frames_raw_counts(rx: bytearray, strength_scale: int):
    """Yield per-key quantized strengths exactly as encoded on the wire (before ``/ strength_scale``)."""
    for counts, _ in _consume_depth_frames(rx, strength_scale):
        yield counts


def parse_notes_csv(spec: str) -> list[int]:
    parts = [p.strip() for p in spec.split(",") if p.strip()]
    notes = [int(p, 10) for p in parts]
    if len(notes) != KEY_COUNT:
        raise ValueError(f"expected {KEY_COUNT} notes, got {len(notes)}")
    for n in notes:
        if not 0 <= n <= 127:
            raise ValueError(f"note out of range: {n}")
    return notes


def clamp_vel(v: int) -> int:
    return max(1, min(127, v))


def clamp_midi7(v: int) -> int:
    return max(0, min(127, v))


def depth_to_expression_midi7(d: float, p: BridgeMappingParams) -> int:
    """Map depth through [expr_depth_low, expr_depth_high] → [expr_out_min, expr_out_max]."""
    lo_d, hi_d = p.expr_depth_low, p.expr_depth_high
    if hi_d <= lo_d:
        hi_d = lo_d + 1e-6
    t = (d - lo_d) / (hi_d - lo_d)
    t = max(0.0, min(1.0, t))
    c0, c1 = p.expr_out_min, p.expr_out_max
    if c1 < c0:
        c0, c1 = c1, c0
    return clamp_midi7(int(round(c0 + t * (c1 - c0))))


def send_volume_ccs(midi: MidiOutPort, channel: int, vol_midi7: int, p: BridgeMappingParams) -> None:
    """Send channel pressure / CC11 / CC74 using an already-computed 7-bit level."""
    v = clamp_midi7(vol_midi7)
    if p.send_pressure:
        midi.send_raw([0xD0 | channel, v])
    if p.send_cc11_expression:
        midi.send_raw([0xB0 | channel, 11, v])
    if p.send_cc74:
        midi.send_raw([0xB0 | channel, 74, v])


def smooth_string_volume_midi7(kv: KeyVoiceState, target_midi7: int, dt_s: float, tau_s: float) -> int:
    """Exponential smoothing toward mapped depth volume; first samples after Note On start at MIDI 0 (silent)."""
    tgt = float(clamp_midi7(target_midi7))
    tau = max(1e-6, float(tau_s))
    alpha = 1.0 - math.exp(-dt_s / tau)
    if kv.string_vol_ema < 0.0:
        kv.string_vol_ema = 0.0
    kv.string_vol_ema += alpha * (tgt - kv.string_vol_ema)
    return clamp_midi7(int(round(kv.string_vol_ema)))


def crossed_up(prev_v: float, v: float, level: float) -> bool:
    return prev_v < level <= v


def crossed_down(prev_v: float, v: float, level: float) -> bool:
    return prev_v > level >= v


def note_off_depth(note_on_depth: float, hysteresis: float = NOTE_OFF_DEPTH_HYSTERESIS) -> float:
    return max(0.0, note_on_depth - hysteresis)


def stroke_arm_threshold(note_on_depth: float) -> float:
    """Depth ↑ crossing arms stroke timing / averaging (derived from note_on_depth)."""
    return max(0.04, min(note_on_depth * 0.11, note_on_depth - 0.04))


def clamp01(x: float) -> float:
    return max(0.0, min(1.0, x))


def hammer_strike_energy01(
    downward_disp_sum: float,
    avg_depth_armed: float,
    armd: float,
    note_on: float,
    approach_rate: float,
    p: BridgeMappingParams,
) -> float:
    """Hammer-like strike energy: travel × (speed²) / mass, shallow travel strongly limits loudness."""
    L = max(1e-6, float(note_on) - float(armd))
    s_path = clamp01(max(0.0, downward_disp_sum) / L)
    s_depth = clamp01(max(0.0, float(avg_depth_armed) - float(armd)) / L)
    travel_core = math.sqrt(max(1e-12, s_path * s_depth))
    travel = travel_core ** max(1.0, float(p.hammer_travel_gamma))
    v_cap = max(0.5, float(p.hammer_v_cap))
    v_rel = clamp01(float(approach_rate) / v_cap)
    mass = max(0.25, float(p.hammer_key_mass))
    raw = travel * (v_rel**2) / mass
    return clamp01(raw)


def shaped_gain(gain01: float, dynamics_contrast: float) -> float:
    """Higher contrast → low raw gains map much quieter (gamma curve)."""
    c = clamp01(dynamics_contrast)
    gamma = 1.0 + c * 2.5
    return clamp01(max(0.0, gain01) ** gamma)


def midi_velocity_from_gain(shaped_gain01: float, dynamics: int) -> int:
    """Linear map shaped 0..1 loudness → MIDI velocity below ``dynamics`` ceiling."""
    dyn = clamp_vel(dynamics)
    floor = max(8, min(dyn // 4, dyn - 12))
    return clamp_vel(floor + int(round((dyn - floor) * clamp01(shaped_gain01))))


class MidiOutPort:
    def __init__(self, virtual_name: str, enabled: bool) -> None:
        self._midi: rtmidi.MidiOut | None = None
        self._enabled = enabled
        self._virtual_name = virtual_name

    def open(self) -> None:
        if not self._enabled:
            return
        self.close()
        self._midi = rtmidi.MidiOut()
        self._midi.open_virtual_port(self._virtual_name)

    def close(self) -> None:
        if self._midi is not None:
            del self._midi
            self._midi = None

    def send_raw(self, msg: list[int]) -> None:
        if self._midi is not None:
            self._midi.send_message(msg)


def process_frame_unified(
    depths: list[float],
    now_mono: float,
    notes: list[int],
    p: BridgeMappingParams,
    st: EngineState,
    midi: MidiOutPort,
    log: Callable[[str], None] | None,
) -> None:
    dt_sample = 1.0 / 120.0
    if st.last_frame_mono is not None:
        dt_sample = max(1e-6, min(0.2, now_mono - st.last_frame_mono))
    st.last_frame_mono = now_mono

    if not p.enable_midi_output:
        for i in range(KEY_COUNT):
            st.keys[i].prev_d = depths[i]
        return

    if p.routing_mode == MidiRoutingMode.STRING:
        off_depth = max(0.0, float(p.note_on_depth))
    else:
        off_depth = note_off_depth(p.note_on_depth)
    thr_on = piano_note_on_threshold(p.note_on_depth)

    for i in range(KEY_COUNT):
        ch = MIDI_VOICE_CHANNEL
        d = depths[i]
        kv = st.keys[i]
        prev = kv.prev_d
        note = notes[i]

        if p.routing_mode == MidiRoutingMode.STRING:
            if not kv.held:
                if string_note_on_cross(prev, d, p.note_on_depth):
                    midi.send_raw([0x90 | ch, note, STRING_NOTE_ON_VELOCITY])
                    kv.held = True
                    kv.string_vol_ema = -1.0
                    tgt = depth_to_expression_midi7(d, p)
                    vol_sm = smooth_string_volume_midi7(kv, tgt, dt_sample, p.string_volume_smooth_tau_s)
                    send_volume_ccs(midi, ch, vol_sm, p)
                    if log:
                        log(f"k{i} NoteOn ch={ch + 1} n={note} vel={STRING_NOTE_ON_VELOCITY} (string CC volume)")
            else:
                if crossed_down(prev, d, off_depth):
                    midi.send_raw([0x80 | ch, note, 0])
                    kv.held = False
                    kv.string_vol_ema = -1.0
                    if log:
                        log(f"k{i} NoteOff ch={ch + 1} n={note}")
                else:
                    tgt = depth_to_expression_midi7(d, p)
                    vol_sm = smooth_string_volume_midi7(kv, tgt, dt_sample, p.string_volume_smooth_tau_s)
                    send_volume_ccs(midi, ch, vol_sm, p)
            kv.prev_d = d
            continue

        if not kv.held:
            if p.timed_velocity:
                armd = stroke_arm_threshold(p.note_on_depth)
                if crossed_up(prev, d, armd) and d < p.note_on_depth:
                    kv.arm_mono_t = now_mono
                    kv.arm_depth_sum = 0.0
                    kv.arm_disp_sum = 0.0
                    kv.arm_samples = 0
                    kv.arm_prev_mono = None
                    kv.ema_approach_rate = 0.0
                elif d < armd:
                    kv.arm_mono_t = None
                    kv.arm_prev_mono = None
                    kv.ema_approach_rate = 0.0
                    kv.arm_disp_sum = 0.0

                if kv.arm_mono_t is not None:
                    if kv.arm_prev_mono is not None:
                        dt_f = max(1e-6, now_mono - kv.arm_prev_mono)
                        dd = max(0.0, d - prev)
                        rate = dd / dt_f
                        tau = max(1e-6, float(p.speed_smooth_tau_s))
                        alpha = 1.0 - math.exp(-dt_f / tau)
                        if kv.arm_samples >= 1:
                            if kv.arm_samples == 1:
                                kv.ema_approach_rate = rate
                            else:
                                kv.ema_approach_rate += alpha * (rate - kv.ema_approach_rate)
                    kv.arm_prev_mono = now_mono
                    kv.arm_disp_sum += max(0.0, d - prev)
                    kv.arm_depth_sum += d
                    kv.arm_samples += 1

            if crossed_up(prev, d, thr_on):
                armd = stroke_arm_threshold(p.note_on_depth)
                if p.timed_velocity:
                    if kv.arm_mono_t is not None and kv.arm_samples > 0:
                        avg_d = kv.arm_depth_sum / kv.arm_samples
                        e01 = hammer_strike_energy01(
                            kv.arm_disp_sum,
                            avg_d,
                            armd,
                            p.note_on_depth,
                            kv.ema_approach_rate,
                            p,
                        )
                        sg = shaped_gain(e01, p.dynamics_contrast)
                        kv.press_gain01 = sg
                        vel = midi_velocity_from_gain(sg, p.dynamics)
                    else:
                        disp_est = max(0.0, d - armd)
                        dd = max(0.0, d - prev)
                        v_inst = dd / dt_sample
                        e01 = hammer_strike_energy01(
                            disp_est,
                            d,
                            armd,
                            p.note_on_depth,
                            v_inst,
                            p,
                        )
                        sg = shaped_gain(e01, p.dynamics_contrast)
                        kv.press_gain01 = sg
                        vel = midi_velocity_from_gain(sg, p.dynamics)
                else:
                    kv.press_gain01 = 1.0
                    vel = clamp_vel(p.dynamics)
                midi.send_raw([0x90 | ch, note, vel])
                kv.held = True
                kv.arm_mono_t = None
                kv.arm_depth_sum = 0.0
                kv.arm_disp_sum = 0.0
                kv.arm_samples = 0
                kv.arm_prev_mono = None
                kv.ema_approach_rate = 0.0
                if log:
                    log(f"k{i} NoteOn ch={ch + 1} n={note} vel={vel}")
        else:
            if crossed_down(prev, d, off_depth):
                midi.send_raw([0x80 | ch, note, 0])
                kv.held = False
                kv.arm_mono_t = None
                kv.arm_depth_sum = 0.0
                kv.arm_disp_sum = 0.0
                kv.arm_samples = 0
                kv.arm_prev_mono = None
                kv.ema_approach_rate = 0.0
                kv.press_gain01 = 1.0
                if log:
                    log(f"k{i} NoteOff ch={ch + 1} n={note}")
            else:
                base = depth_to_expression_midi7(d, p)
                cc_val = clamp_midi7(int(round(float(base) * clamp01(kv.press_gain01))))
                if p.send_pressure:
                    midi.send_raw([0xD0 | ch, cc_val])
                if p.send_cc74:
                    midi.send_raw([0xB0 | ch, 74, cc_val])

        kv.prev_d = d


def silence_all_keys(midi: MidiOutPort, notes: list[int]) -> None:
    for i in range(KEY_COUNT):
        midi.send_raw([0x80 | MIDI_VOICE_CHANNEL, notes[i], 0])


def run_serial_loop(
    port: str,
    baud: int,
    strength_scale: int,
    stop_evt: threading.Event,
    frame_queue: queue.Queue[
        tuple[float, list[float], list[int]] | tuple[str, str]
    ],
) -> None:
    rx = bytearray()
    try:
        with open_serial_cdc(port, baud) as ser:
            while not stop_evt.is_set():
                chunk = ser.read(512)
                if chunk:
                    rx.extend(chunk)
                for raw_counts, strengths in _consume_depth_frames(rx, strength_scale):
                    frame_queue.put((time.monotonic(), strengths, raw_counts))
    except Exception as e:  # pragma: no cover
        frame_queue.put(("error", str(e)))  # type: ignore[arg-type]


def run_cli(args: argparse.Namespace) -> None:
    mode = parse_bridge_mode_arg(args.mode)

    if mode == BridgeMode.RAW_KEYS:
        with open_serial_cdc(args.port, args.baud) as ser:
            rx = bytearray()
            while True:
                chunk = ser.read(512)
                if chunk:
                    rx.extend(chunk)
                for counts in extract_frames_raw_counts(rx, args.strength_scale):
                    print("\t".join(str(w) for w in counts), flush=True)
        return

    notes = parse_notes_csv(args.notes)
    st = EngineState()
    p = BridgeMappingParams()
    if mode == BridgeMode.MONITOR_ONLY:
        p.enable_midi_output = False
    elif args.mode in CLI_LEGACY_FIXED_VELOCITY:
        p.timed_velocity = False

    if mode == BridgeMode.STRING:
        p.routing_mode = MidiRoutingMode.STRING
        p.note_on_depth = 0.0
    else:
        p.routing_mode = MidiRoutingMode.PIANO

    midi = MidiOutPort(args.midi_name, enabled=p.enable_midi_output)
    if p.enable_midi_output:
        midi.open()

    try:
        with open_serial_cdc(args.port, args.baud) as ser:
            rx = bytearray()
            while True:
                chunk = ser.read(512)
                if chunk:
                    rx.extend(chunk)
                for strengths in extract_frames(rx, args.strength_scale):
                    now = time.monotonic()
                    if mode == BridgeMode.MONITOR_ONLY:
                        print("\t".join(f"{s:.6f}" for s in strengths), flush=True)
                    process_frame_unified(strengths, now, notes, p, st, midi, None)
    finally:
        midi.close()


def launch_gui(default_port: str | None = None) -> None:
    ensure_tkinter()
    import tkinter as tk
    from tkinter import ttk

    root = tk.Tk()
    root.title("Hall MIDI bridge")
    root.minsize(520, 560)

    frame_q: queue.Queue = queue.Queue(maxsize=256)
    stop_evt = threading.Event()
    reader_thread_ref: dict[str, threading.Thread | None] = {"t": None}

    wire_live_counts: list[int] = [0] * KEY_COUNT
    wire_prog_widgets: list[ttk.Progressbar] = []
    wire_val_labels: list[ttk.Label] = []

    st = EngineState()
    midi_port = MidiOutPort("Hall MIDI Bridge", enabled=False)

    # --- Tk variables ---
    port_var = tk.StringVar(value=(default_port or ""))
    baud_var = tk.IntVar(value=115200)
    scale_var = tk.IntVar(value=DEFAULT_STRENGTH_SCALE)
    midi_name_var = tk.StringVar(value="Hall MIDI Bridge")
    notes_var = tk.StringVar(value="60,62,64,65,67,69,71,72,74,76")

    routing_var = tk.StringVar(value=MidiRoutingMode.PIANO.value)

    enable_midi_var = tk.BooleanVar(value=True)
    timed_velocity_var = tk.BooleanVar(value=True)

    note_on_depth_piano_var = tk.DoubleVar(value=0.98)
    note_on_depth_string_var = tk.DoubleVar(value=0.0)
    dynamics_var = tk.IntVar(value=100)
    hammer_mass_var = tk.DoubleVar(value=1.0)
    hammer_v_cap_var = tk.DoubleVar(value=22.0)
    hammer_gamma_var = tk.DoubleVar(value=1.35)
    dynamics_contrast_var = tk.DoubleVar(value=0.5)
    speed_smooth_tau_ms = tk.DoubleVar(value=48.0)

    string_smooth_tau_ms = tk.DoubleVar(value=75.0)

    expr_depth_lo = tk.DoubleVar(value=0.0)
    expr_depth_hi = tk.DoubleVar(value=1.0)
    expr_out_lo = tk.IntVar(value=0)
    expr_out_hi = tk.IntVar(value=127)

    send_pressure_var = tk.BooleanVar(value=True)
    send_cc11_var = tk.BooleanVar(value=True)
    send_cc74_var = tk.BooleanVar(value=True)

    status_var = tk.StringVar(value="Disconnected.")
    log_buffer: list[str] = []

    def log_line(s: str) -> None:
        log_buffer.append(time.strftime("%H:%M:%S ") + s)
        if len(log_buffer) > 200:
            del log_buffer[:100]

    last_depth_log_mono = [0.0]

    def log_depth_throttled(strengths: list[float], *, prefer_fast_log: bool) -> None:
        t = time.monotonic()
        interval = 0.05 if prefer_fast_log else 0.075
        if t - last_depth_log_mono[0] < interval:
            return
        last_depth_log_mono[0] = t
        log_line("depth\t" + "\t".join(f"{s:.2f}" for s in strengths))

    def refresh_ports(combo: ttk.Combobox | None = None) -> None:
        from serial.tools import list_ports

        names = [p.device for p in list_ports.comports()]
        if combo is not None:
            combo["values"] = names
        if names and not port_var.get():
            port_var.set(names[0])

    def collect_mapping_params() -> BridgeMappingParams:
        dc = max(0.0, min(1.0, float(dynamics_contrast_var.get())))
        tau_ms = max(0.0, float(speed_smooth_tau_ms.get()))
        tau_s = tau_ms / 1000.0
        hm = max(0.25, float(hammer_mass_var.get()))
        hv = max(0.5, float(hammer_v_cap_var.get()))
        hg = max(1.0, float(hammer_gamma_var.get()))
        routing = MidiRoutingMode(str(routing_var.get()))
        if routing == MidiRoutingMode.STRING:
            nod = float(note_on_depth_string_var.get())
        else:
            nod = float(note_on_depth_piano_var.get())
        str_smooth_ms = max(5.0, float(string_smooth_tau_ms.get()))
        str_smooth_s = str_smooth_ms / 1000.0
        return BridgeMappingParams(
            enable_midi_output=enable_midi_var.get(),
            timed_velocity=timed_velocity_var.get(),
            routing_mode=routing,
            note_on_depth=nod,
            dynamics=clamp_vel(int(round(float(dynamics_var.get())))),
            hammer_key_mass=hm,
            hammer_v_cap=hv,
            hammer_travel_gamma=hg,
            dynamics_contrast=dc,
            speed_smooth_tau_s=tau_s,
            string_volume_smooth_tau_s=str_smooth_s,
            expr_depth_low=float(expr_depth_lo.get()),
            expr_depth_high=float(expr_depth_hi.get()),
            expr_out_min=clamp_midi7(int(round(float(expr_out_lo.get())))),
            expr_out_max=clamp_midi7(int(round(float(expr_out_hi.get())))),
            send_pressure=send_pressure_var.get(),
            send_cc11_expression=send_cc11_var.get(),
            send_cc74=send_cc74_var.get(),
        )

    def parse_notes_safe() -> list[int] | None:
        try:
            return parse_notes_csv(notes_var.get())
        except ValueError as e:
            status_var.set(str(e))
            return None

    def stop_reader() -> None:
        stop_evt.set()
        th = reader_thread_ref["t"]
        if th is not None and th.is_alive():
            th.join(timeout=1.5)
        reader_thread_ref["t"] = None
        stop_evt.clear()

    connected = {"on": False}

    def toggle_connect() -> None:
        if not connected["on"]:
            p = port_var.get().strip()
            if not p:
                status_var.set("Pick a serial port.")
                return
            notes = parse_notes_safe()
            if notes is None:
                return
            midi_port.close()
            midi_port._virtual_name = midi_name_var.get().strip() or "Hall MIDI Bridge"
            midi_port._enabled = enable_midi_var.get()
            try:
                if midi_port._enabled:
                    midi_port.open()
            except Exception as e:
                status_var.set(f"MIDI error: {e}")
                return
            try:
                stop_reader()
                wire_live_counts[:] = [0] * KEY_COUNT
                for i in range(KEY_COUNT):
                    st.keys[i] = KeyVoiceState()
                connected["on"] = True
                btn_connect.config(text="Disconnect")
                set_live_status()
                reader_thread_ref["t"] = threading.Thread(
                    target=run_serial_loop,
                    args=(p, baud_var.get(), scale_var.get(), stop_evt, frame_q),
                    daemon=True,
                )
                reader_thread_ref["t"].start()
            except Exception as e:
                status_var.set(f"Serial error: {e}")
                connected["on"] = False
                btn_connect.config(text="Connect")
                midi_port.close()
        else:
            notes_off = parse_notes_safe()
            stop_reader()
            if notes_off and midi_port._enabled and midi_port._midi is not None:
                silence_all_keys(midi_port, notes_off)
            connected["on"] = False
            btn_connect.config(text="Connect")
            midi_port.close()
            for i in range(KEY_COUNT):
                st.keys[i] = KeyVoiceState()
            wire_live_counts[:] = [0] * KEY_COUNT
            status_var.set("Disconnected.")

    def set_live_status() -> None:
        """Update status line while serial is connected (reflects MIDI vs monitor)."""
        if not connected["on"]:
            return
        p = port_var.get().strip()
        if midi_port._enabled and midi_port._midi is not None:
            tail = "MIDI virtual port"
        elif midi_port._enabled:
            tail = "MIDI (not open)"
        else:
            tail = "monitor"
        status_var.set(f"Reading {p} → {tail}")

    def sync_midi_from_toggle() -> None:
        """Enable MIDI virtual port from ``Enable MIDI`` while serial stays connected."""
        if not connected["on"]:
            return
        want_midi = enable_midi_var.get()
        if want_midi == midi_port._enabled and (not want_midi or midi_port._midi is not None):
            set_live_status()
            return
        notes_off = parse_notes_safe()
        if midi_port._enabled and notes_off and midi_port._midi is not None:
            silence_all_keys(midi_port, notes_off)
        midi_port.close()
        midi_port._virtual_name = midi_name_var.get().strip() or "Hall MIDI Bridge"
        midi_port._enabled = want_midi
        try:
            if want_midi:
                midi_port.open()
        except Exception as e:
            status_var.set(f"MIDI toggle failed: {e}")
            return
        for i in range(KEY_COUNT):
            st.keys[i] = KeyVoiceState()
        set_live_status()

    def tick() -> None:
        notes = parse_notes_safe()
        try:
            while True:
                item = frame_q.get_nowait()
                if isinstance(item, tuple) and len(item) == 2:
                    a, b = item
                    if a == "error":
                        status_var.set(f"Serial thread: {b}")
                        continue
                    continue
                if isinstance(item, tuple) and len(item) == 3:
                    now_mono, strengths, raw_counts = item
                    wire_live_counts[:] = raw_counts
                    mp = collect_mapping_params()
                    log_depth_throttled(strengths, prefer_fast_log=not mp.enable_midi_output)
                    if notes is None:
                        continue
                    process_frame_unified(
                        strengths, now_mono, notes, mp, st, midi_port, log_line
                    )
        except queue.Empty:
            pass

        mx = max(1, int(scale_var.get()))
        live = connected["on"]
        for i in range(KEY_COUNT):
            if i >= len(wire_prog_widgets):
                break
            v = wire_live_counts[i] if live else 0
            wire_prog_widgets[i].configure(maximum=mx, value=min(v, mx))
            wire_val_labels[i].configure(text=str(v) if live else "—")

        if log_text is not None:
            log_text.configure(state=tk.NORMAL)
            log_text.delete("1.0", tk.END)
            log_text.insert(tk.END, "\n".join(log_buffer[-40:]))
            log_text.configure(state=tk.DISABLED)

        root.after(15, tick)

    # --- Layout ---
    top = ttk.Frame(root, padding=8)
    top.pack(fill=tk.X)

    ttk.Label(top, text="Serial port").grid(row=0, column=0, sticky=tk.W)
    port_combo = ttk.Combobox(top, textvariable=port_var, width=28)
    port_combo.grid(row=0, column=1, sticky=tk.EW)
    ttk.Button(top, text="Refresh", command=lambda: refresh_ports(port_combo)).grid(row=0, column=2)

    ttk.Label(top, text="Baud").grid(row=1, column=0, sticky=tk.W)
    ttk.Entry(top, textvariable=baud_var, width=10).grid(row=1, column=1, sticky=tk.W)

    ttk.Label(top, text="Strength scale").grid(row=2, column=0, sticky=tk.W)
    ttk.Entry(top, textvariable=scale_var, width=10).grid(row=2, column=1, sticky=tk.W)

    ttk.Label(top, text="Mode").grid(row=3, column=0, sticky=tk.W, pady=(6, 0))
    mode_fr = ttk.Frame(top)
    mode_fr.grid(row=3, column=1, columnspan=2, sticky=tk.W, pady=(6, 0))
    for text_lbl, rval in (
        ("Piano", MidiRoutingMode.PIANO.value),
        ("String", MidiRoutingMode.STRING.value),
    ):
        ttk.Radiobutton(mode_fr, text=text_lbl, variable=routing_var, value=rval).pack(
            side=tk.LEFT, padx=(0, 12)
        )

    tog_fr = ttk.Frame(top)
    tog_fr.grid(row=4, column=0, columnspan=3, sticky=tk.W, pady=(4, 0))
    ttk.Checkbutton(tog_fr, text="Enable MIDI output", variable=enable_midi_var).pack(
        side=tk.LEFT, padx=(0, 14)
    )
    timed_vel_cb = ttk.Checkbutton(
        tog_fr,
        text="Velocity from stroke (hammer model)",
        variable=timed_velocity_var,
    )
    timed_vel_cb.pack(side=tk.LEFT)

    ttk.Label(top, text="MIDI port name").grid(row=5, column=0, sticky=tk.W)
    ttk.Entry(top, textvariable=midi_name_var, width=28).grid(row=5, column=1, columnspan=2, sticky=tk.EW)

    ttk.Label(top, text=f"Notes ({KEY_COUNT}, comma)").grid(row=6, column=0, sticky=tk.W)
    ttk.Entry(top, textvariable=notes_var, width=36).grid(row=6, column=1, columnspan=2, sticky=tk.EW)

    btn_connect = ttk.Button(top, text="Connect", command=toggle_connect)
    btn_connect.grid(row=7, column=0, pady=6)
    ttk.Label(top, textvariable=status_var, wraplength=400).grid(row=7, column=1, columnspan=2, sticky=tk.W)

    mode_hint = ttk.Label(
        top,
        text="Piano: MIDI channel 1 · hammer model.",
        wraplength=520,
        font=("TkDefaultFont", 9),
    )
    mode_hint.grid(row=8, column=0, columnspan=3, sticky=tk.W, pady=(4, 0))

    top.columnconfigure(1, weight=1)

    nb = ttk.Notebook(root, padding=(8, 0))
    nb.pack(fill=tk.BOTH, expand=True)

    fr_map = ttk.Frame(nb, padding=8)
    fr_mon = ttk.Frame(nb, padding=8)

    widgets_hammer: list[ttk.Scale] = []

    def bind_float_display(var: tk.DoubleVar, lbl: ttk.Label) -> None:
        def upd(*_a: object) -> None:
            try:
                lbl.configure(text=f"{float(var.get()):.2f}")
            except tk.TclError:
                pass

        var.trace_add("write", lambda *_: upd())
        upd()

    def bind_int_display(var: tk.IntVar, lbl: ttk.Label) -> None:
        def upd(*_a: object) -> None:
            try:
                lbl.configure(text=str(int(round(float(var.get())))))
            except tk.TclError:
                pass

        var.trace_add("write", lambda *_: upd())
        upd()

    def row_slider_d(parent: ttk.Frame, row: int, label: str, var: tk.DoubleVar, frm: float, to: float) -> ttk.Scale:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky=tk.W, padx=(0, 6))
        sc = ttk.Scale(parent, from_=frm, to=to, variable=var)
        sc.grid(row=row, column=1, sticky=tk.EW)
        val_lbl = ttk.Label(parent, width=7)
        val_lbl.grid(row=row, column=2, padx=(4, 0))
        bind_float_display(var, val_lbl)
        parent.columnconfigure(1, weight=1)
        return sc

    def row_slider_i(parent: ttk.Frame, row: int, label: str, var: tk.IntVar, frm: int, to: int) -> ttk.Scale:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky=tk.W, padx=(0, 6))
        sc = ttk.Scale(parent, from_=frm, to=to, variable=var)
        sc.grid(row=row, column=1, sticky=tk.EW)
        val_lbl = ttk.Label(parent, width=7)
        val_lbl.grid(row=row, column=2, padx=(4, 0))
        bind_int_display(var, val_lbl)
        parent.columnconfigure(1, weight=1)
        return sc

    fr_map.columnconfigure(0, weight=1)

    HAMMER_ROW = 1
    EXPR_ROW = 2

    lf_t = ttk.LabelFrame(fr_map, text="Trigger", padding=8)
    lf_t.grid(row=0, column=0, sticky=tk.EW, pady=(0, 8))
    ttk.Label(
        lf_t,
        text=(
            "Note On when depth crosses upward through the trigger. "
            "Piano default is near full press. String default is 0: first upward motion from calibrated rest at 0 "
            "(no synthetic trigger floor). "
            f"Piano Note Off uses −{NOTE_OFF_DEPTH_HYSTERESIS:.2f} hysteresis; String Note Off uses the trigger depth exactly."
        ),
        wraplength=460,
    ).grid(row=0, column=0, columnspan=3, sticky=tk.W)
    fr_trig_piano = ttk.Frame(lf_t)
    fr_trig_piano.grid(row=1, column=0, columnspan=3, sticky=tk.EW)
    row_slider_d(fr_trig_piano, 0, "Piano: trigger depth (↑)", note_on_depth_piano_var, 0.05, 1.0)
    fr_trig_string = ttk.Frame(lf_t)
    fr_trig_string.grid(row=2, column=0, columnspan=3, sticky=tk.EW)
    row_slider_d(fr_trig_string, 0, "String: trigger depth (↑)", note_on_depth_string_var, 0.0, 1.0)
    fr_trig_string.grid_remove()

    lf_v = ttk.LabelFrame(fr_map, text="Hammer / dynamics", padding=8)
    lf_v.grid(row=HAMMER_ROW, column=0, sticky=tk.EW, pady=(0, 8))
    ttk.Label(
        lf_v,
        text=(
            "Strike loudness ≈ travel × (approach speed)² / key mass (evaluated at Note On). "
            "Shallow motion transfers little energy even if acceleration spikes. "
            "τ low-pass filters approach speed over the stroke."
        ),
        wraplength=460,
    ).grid(row=0, column=0, columnspan=3, sticky=tk.W)
    row_slider_i(lf_v, 1, "Dynamics (velocity ceiling)", dynamics_var, 1, 127)
    sc_hm = row_slider_d(lf_v, 2, "Key mass (heavier → quieter)", hammer_mass_var, 0.35, 3.0)
    sc_hv = row_slider_d(lf_v, 3, "Strike speed cap (depth/s)", hammer_v_cap_var, 6.0, 72.0)
    sc_hg = row_slider_d(lf_v, 4, "Travel emphasis γ", hammer_gamma_var, 1.0, 2.2)
    widgets_hammer.extend([sc_hm, sc_hv, sc_hg])

    sc_dc = row_slider_d(lf_v, 5, "Dynamics contrast", dynamics_contrast_var, 0.0, 1.0)
    widgets_hammer.append(sc_dc)
    sc_tau = row_slider_d(lf_v, 6, "Approach smoothing τ (ms)", speed_smooth_tau_ms, 5.0, 220.0)
    widgets_hammer.append(sc_tau)

    lf_string = ttk.LabelFrame(fr_map, text="String mode", padding=8)
    lf_string.grid(row=HAMMER_ROW, column=0, sticky=tk.EW, pady=(0, 8))
    ttk.Label(
        lf_string,
        text=(
            "Polyphonic on MIDI channel 1. Note On velocity is fixed at 1 (inaudible); loudness is only "
            "from smoothed depth → pressure / CC11—tune curves under Expression."
        ),
        wraplength=460,
    ).grid(row=0, column=0, columnspan=3, sticky=tk.W)
    row_slider_d(lf_string, 1, "Volume smoothing τ (ms)", string_smooth_tau_ms, 5.0, 250.0)
    lf_string.grid_remove()

    lf_e = ttk.LabelFrame(fr_map, text="Expression / controllers", padding=8)
    lf_e.grid(row=EXPR_ROW, column=0, sticky=tk.EW)
    ttk.Label(
        lf_e,
        text=(
            "Maps sensor depth to MIDI while keys are held (String: volume; Piano: scaled by hammer strike gain)."
        ),
        wraplength=460,
    ).grid(row=0, column=0, columnspan=3, sticky=tk.W)
    row_slider_d(lf_e, 1, "depth → output min", expr_depth_lo, 0.0, 1.0)
    row_slider_d(lf_e, 2, "depth → output max", expr_depth_hi, 0.0, 1.0)
    row_slider_i(lf_e, 3, "MIDI out min", expr_out_lo, 0, 127)
    row_slider_i(lf_e, 4, "MIDI out max", expr_out_hi, 0, 127)
    ttk.Checkbutton(lf_e, text="Channel pressure", variable=send_pressure_var).grid(
        row=5, column=0, columnspan=2, sticky=tk.W, pady=(6, 0)
    )
    ttk.Checkbutton(lf_e, text="CC 11 expression (volume / swells)", variable=send_cc11_var).grid(
        row=6, column=0, columnspan=2, sticky=tk.W
    )
    ttk.Checkbutton(lf_e, text="CC 74 (optional timbre)", variable=send_cc74_var).grid(
        row=7, column=0, columnspan=2, sticky=tk.W
    )

    def refresh_timed_widgets(*_args: object) -> None:
        if routing_var.get() == MidiRoutingMode.STRING.value:
            for w in widgets_hammer:
                w.configure(state=tk.DISABLED)
            return
        on_spd = timed_velocity_var.get()
        st_tw = tk.NORMAL if on_spd else tk.DISABLED
        for w in widgets_hammer:
            w.configure(state=st_tw)

    def refresh_mode_widgets(*_args: object) -> None:
        m = routing_var.get()
        if m == MidiRoutingMode.STRING.value:
            lf_v.grid_remove()
            fr_trig_piano.grid_remove()
            fr_trig_string.grid()
            lf_string.grid(row=HAMMER_ROW, column=0, sticky=tk.EW, pady=(0, 8))
            timed_vel_cb.state(["disabled"])
            mode_hint.configure(
                text="String: MIDI channel 1 poly · trigger from rest (default) · depth → volume (pressure + CC11)."
            )
        else:
            lf_string.grid_remove()
            fr_trig_string.grid_remove()
            fr_trig_piano.grid()
            lf_v.grid(row=HAMMER_ROW, column=0, sticky=tk.EW, pady=(0, 8))
            timed_vel_cb.state(["!disabled"])
            mode_hint.configure(text="Piano: MIDI channel 1 poly · hammer physics at Note On.")
        refresh_timed_widgets()

    ttk.Label(
        fr_mon,
        text=(
            "Bars show each key’s wire value (same integer as the serial frame, 0 … strength scale). "
            "Log: normalized depths + MIDI events."
        ),
        wraplength=480,
    ).pack(anchor=tk.W)

    lf_wire = ttk.LabelFrame(fr_mon, text="Key wire values", padding=6)
    lf_wire.pack(fill=tk.X, padx=4, pady=(0, 6))
    for ki in range(KEY_COUNT):
        ttk.Label(lf_wire, text=f"k{ki}", width=4).grid(row=ki, column=0, sticky=tk.W, pady=1)
        pb = ttk.Progressbar(lf_wire, length=260, mode="determinate", maximum=DEFAULT_STRENGTH_SCALE)
        pb.grid(row=ki, column=1, sticky=tk.EW, padx=(6, 8), pady=1)
        vl = ttk.Label(lf_wire, text="—", width=7, anchor=tk.E)
        vl.grid(row=ki, column=2, sticky=tk.E)
        wire_prog_widgets.append(pb)
        wire_val_labels.append(vl)
    lf_wire.columnconfigure(1, weight=1)

    nb.add(fr_map, text="Mapping")
    nb.add(fr_mon, text="Monitor")

    log_text: tk.Text | None
    log_text = tk.Text(fr_mon, height=14, state=tk.DISABLED)
    log_text.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

    enable_midi_var.trace_add("write", lambda *_: sync_midi_from_toggle())
    routing_var.trace_add("write", lambda *_: refresh_mode_widgets())
    timed_velocity_var.trace_add("write", lambda *_: refresh_timed_widgets())
    refresh_mode_widgets()

    def on_close() -> None:
        stop_evt.set()
        th = reader_thread_ref["t"]
        if th is not None and th.is_alive():
            th.join(timeout=1.5)
        reader_thread_ref["t"] = None
        stop_evt.clear()
        notes_off = parse_notes_safe()
        if notes_off and midi_port._enabled and midi_port._midi is not None:
            silence_all_keys(midi_port, notes_off)
        midi_port.close()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)
    refresh_ports(port_combo)
    root.after(50, tick)
    root.mainloop()


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Hall serial → MIDI bridge (piano / string)",
        epilog=(
            "GUI needs Tkinter (brew install python-tk@<minor> on macOS Homebrew Python). "
            "If Tkinter is missing but you pass --port / -p, the bridge runs headless automatically."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--list-ports", action="store_true")
    ap.add_argument("--no-gui", action="store_true", help="CLI-only (requires --port)")
    ap.add_argument("-p", "--port")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--strength-scale", type=int, default=DEFAULT_STRENGTH_SCALE)
    cli_modes = sorted({m.value for m in BridgeMode} | set(_LEGACY_CLI_MODES.keys()))
    ap.add_argument(
        "--mode",
        choices=cli_modes,
        default=BridgeMode.PIANO.value,
        help=(
            "piano | string | monitor_only | raw_keys · legacy mpe* aliases → piano "
            "(fixed vel: mpe_bottom_fixed, mpe_fixed_vel)"
        ),
    )
    ap.add_argument("--midi-name", default="Hall MIDI Bridge")
    ap.add_argument(
        "--notes",
        default="60,62,64,65,67,69,71,72,74,76",
        help=f"Comma-separated MIDI notes for k0..k{KEY_COUNT - 1}",
    )
    args = ap.parse_args()

    if args.list_ports:
        from serial.tools import list_ports

        for p in list_ports.comports():
            print(p.device, "-", p.description)
        return

    if args.no_gui:
        if not args.port:
            ap.error("--port required with --no-gui")
        run_cli(args)
        return

    if args.port and not tkinter_usable():
        print(
            "Tkinter not available — running headless because --port was set.\n"
            "For the GUI after `brew install python-tk@3.13`, run the same app with Homebrew's "
            "`python3` (check `which python3`; frameworks under /opt/homebrew usually pick up "
            "python-tk). Omit -p to see full Tk install hints only.\n",
            file=sys.stderr,
        )
        run_cli(args)
        return

    if not tkinter_usable():
        ensure_tkinter()

    launch_gui(default_port=args.port)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nQuit.", file=sys.stderr)
