# midi-hall-effect-kb

Firmware for an **Arduino Uno** that reads **hall-effect keyboard** columns through a **single analog multiplexer**, calibrates each key’s ADC range, stores calibration in **EEPROM**, and streams key state over **Serial** as a tab-separated table (percent and raw ADC). Calibration mode uses a **digital input** and **LED**; no calibration UI on Serial. **MIDI** is optional and selected with a **PlatformIO environment** (`uno` stays table/calibration only — see **[MIDI (PlatformIO environments)](#midi-platformio-environments)**).

## Features

- **Mux:** `Mux` / `MuxConfig` — address lines and channel count driven from `config.h` (or a custom struct in `main.cpp`).
- **Sampling:** Scan all keys each loop; optional settle delay after mux change (see scan-rate tests).
- **Calibration:** Hold cal pin in the configured “calibrate” level to capture min/max; release to commit to EEPROM (versioned blob).
- **Debouncing:** `Button` on the cal input with optional fast release while a cal session is active.
- **EEPROM:** `EepromStore` + `EepromSlot<T>` for typed regions (header is `eeprom_store.h`, not `eeprom.h`, to avoid clashing with Arduino’s `EEPROM.h` on macOS).

## Requirements

- [PlatformIO](https://platformio.org/) (CLI or IDE extension)
- **Arduino Uno** (ATmega328P) for upload and on-target tests
- Toolchain pulls `platform = atmelavr`, `framework = arduino` per `platformio.ini`

## Quick start

```bash
cd midi-hall-effect-kb
pio run -e uno              # build firmware
pio run -e uno -t upload    # flash connected Uno
pio device monitor -b 115200
```

That matches **`default_envs = uno`** in `platformio.ini`: everyday builds and **`pio test -e uno`** use this image. **`uno`** does **not** emit MIDI bytes on `Serial`; use **`midi_serial`** when you need the host bridge workflow (next section).

### Upload fails: `stk500_recv(): programmer is not responding`

The Uno bootloader talks over **the same USB‑serial CDC device** your monitor and scripts use. If anything else holds the port, **`pio run … -t upload`** will fail — often with **“programmer is not responding.”**

**Quit** PlatformIO Serial Monitor, **`pio device monitor`**, the Arduino IDE monitor, **`screen`/`minicom`**, and **`tools/serial_midi_bridge.py`**, then upload again.

On macOS you can confirm who has the device:

```bash
lsof /dev/cu.usbmodem*
```

Stop that process or close its terminal (**Ctrl+C** on the bridge), then retry **`pio run -e uno -t upload`**.

## MIDI (PlatformIO environments)

Same **Uno** hardware, different firmware image — pick the environment for what should appear on the USB‑serial CDC port:

| Environment | MCU / cable | Serial output |
|-------------|-------------|---------------|
| **`uno`** *(default)* | ATmega328P Uno | Tabular **ASCII** poll table (`Serial.print` debugging). **`MIDIOUT_ENABLED`** off. |
| **`midi_serial`** | Same board | **`uno_midi_serial`** alias. **Raw MIDI** bytes on `Serial`; poll table **suppressed** so the stream is usable by a bridge. |
| **`uno_midi_serial`** | Same | Same flags as **`midi_serial`**. |

Build and flash serial‑MIDI firmware:

```bash
pio run -e midi_serial -t upload
```

### Host bridge (macOS): Serial → virtual CoreMIDI

Uno exposes a **CDC serial** device (`/dev/cu.usbmodem…`), not class‑compliant USB‑MIDI in hardware. Forward bytes to CoreMIDI with **`tools/serial_midi_bridge.py`**.

Only **one program** may open the CDC port at a time. **`serial_midi_bridge.py` and `pio … -t upload` cannot run together** — stop the bridge (Ctrl+C), upload firmware, then start the bridge again.

Before running the bridge, **quit PlatformIO Serial Monitor** (and anything else holding the port).

```bash
python3 -m venv .venv
. .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r tools/requirements-serial-midi.txt
python tools/serial_midi_bridge.py --list                    # enumerate ports
python tools/serial_midi_bridge.py -p /dev/cu.usbmodemXXXX   # explicit device
python tools/serial_midi_bridge.py                           # omit -p to auto-pick cu.usbmodem*
```

Keep **baud `115200`** on both ends (`platformio.ini` **`monitor_speed`**, **`include/serial_baud.h`** / **`MIDI_SERIAL_BAUD`** in **`include/config.h`**).

The bridge opens serial with **DTR/RTS de‑asserted** so macOS is less likely to **reset** the board and churn “serial disconnected” reconnect loops (details in **`tools/serial_midi_bridge.py`**).

**Verbose (`-v`) shows no MIDI when pressing keys:** use firmware **`midi_serial`**; **`PIN_CALIB_MODE` (pin 7)** idle **LOW** (wire ~10k to GND per **`config.h`**) — a floating or HIGH cal pin used to trap the device in calibration at boot (**no MIDI**). If presses never trip Note On, lower **`KEY_MAP_DEADZONE_PERCENT`** (or recalibrate so each key sweeps a wider min..max).

### Other boards (quick reference)

`platformio.ini` also defines Leonardo/Micro **native USB MIDI** (`midi_usb`, `midi_usb_micro`, **`MIDIUSB`**) and **Raspberry Pi Pico** (`pico`, `pico_midi_serial`). All compile-time MIDI knobs (transport flags, baud, per-key note map, channel, calibration deadzone) live in **`include/config.h`**; **`include/midi.h`** is the API surface.

## Configuration

Edit **`include/config.h`**: mux enable and address pins, analog input pin, calibration pin and active level, LED pin, deadzone percent, EEPROM-related tuning, mux channel count / address bit count. For MIDI builds (**`midi_serial`** / **`midi_usb`**), **`MIDI_MAP_PHYSICAL_KEY00_NOTE`** … **`MIDI_MAP_PHYSICAL_KEY09_NOTE`** map multiplex columns (**`k0`…`k9`**) to MIDI notes (defaults are ascending piano white keys from C4 — **60,62,64,…,76**) and **`MIDI_VOICE_CHANNEL`** sets the host-visible 1–16 channel. Note On/Off boundaries are driven by the calibration deadzone (**`KEY_MAP_DEADZONE_PERCENT`**): a key fires Note On as soon as the calibrated 0–100 % strength leaves 0, and Note Off when it returns to 0.  
The application constructs the mux in **`src/main.cpp`**:

```cpp
static Mux g_mux(muxDefaultConfig());
```

Replace `muxDefaultConfig()` with an inline `MuxConfig{ ... }` if this board uses different pins without changing the shared defaults file.

## Tests

```bash
pio test -e uno
pio test -e uno -f test_basic
pio test -e uno -f test_scan_rate
pio test -e uno -f test_eeprom_calibration
```

Some suites need the board connected (upload + serial). EEPROM tests temporarily back up and restore bytes in the calibration slot. More detail: **`test/README`**.

## Repository layout

| Path | Purpose |
|------|---------|
| `include/config.h` | Board and tuning macros |
| `include/mux.h`, `src/mux.cpp` | Multiplexer driver |
| `include/button.h`, `src/button.cpp` | Debounced digital input |
| `include/eeprom_store.h`, `src/eeprom.cpp` | EEPROM access |
| `include/key_calibration.h`, `src/key_calibration.cpp` | Calibration state, EEPROM blob, mapping |
| `src/main.cpp` | Application entry, owns `Mux`, key loop, Serial / MIDI output |
| `include/midi.h` | MIDI outbound API + `midiKeyboardPhysicalNote()` (transport flags + tunables in **`config.h`**) |
| `test/` | Unity tests |
| `.cursor/rules/project-conventions.mdc` | Cursor agent notes (style, build, architecture) |

## Agent / contributor notes

Coding conventions, filename rules (including `eeprom_store.h`), command cheat sheet, and a **checklist for adding new modules** (e.g. `platformio.ini` `build_src_filter`) are in **`.cursor/rules/project-conventions.mdc`**.
