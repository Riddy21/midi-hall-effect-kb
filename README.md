# midi-hall-effect-kb

Firmware for an **Arduino Uno** that reads **hall-effect keyboard** columns through a **single analog multiplexer**, calibrates each key’s ADC range, stores calibration in **EEPROM**, and streams key state over **Serial** as a tab-separated table (percent and raw ADC). Calibration mode uses a **digital input** and **LED**; no calibration UI on Serial.

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

## Configuration

Edit **`include/config.h`**: mux enable and address pins, analog input pin, calibration pin and active level, LED pin, deadzone percent, EEPROM-related tuning, mux channel count / address bit count.  
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
| `src/main.cpp` | Application entry, owns `Mux`, key loop, Serial output |
| `test/` | Unity tests |
| `.cursor/rules/project-conventions.mdc` | Cursor agent notes (style, build, architecture) |

## Agent / contributor notes

Coding conventions, filename rules (including `eeprom_store.h`), command cheat sheet, and a **checklist for adding new modules** (e.g. `platformio.ini` `build_src_filter`) are in **`.cursor/rules/project-conventions.mdc`**.
