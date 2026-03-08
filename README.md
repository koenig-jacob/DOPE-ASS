# DOPE-ASS
### Digital Open-Source Precise Extrapolator — Automated Scope System

DOPE-ASS is the firmware for a custom smart scope built on the ESP32-P4. It drives the camera, display, sensor stack, and user interface, and uses [DOPE](https://github.com/crunchy4159/DOPE) as a git submodule to handle all the ballistic math.

The scope renders a live camera feed with a first-focal-plane tactical reticle overlay. Point at a target, pull the range trigger, and the hold corrections appear on the glass — elevation, windage, Coriolis, spin drift, cant, the works. DOPE-ASS handles everything above the math. DOPE handles the math.

---

XMR Donation Address: 8BWmYeEc8xQekZXC29ATz4aLagtw4y1U7JxFxrZFYyoaLDccPjTT6KRYvEVYeirr3M9p7ZQsvJSDeQUctB68wZPaDvZ1ifu

---

## Hardware

| Component | Part |
|---|---|
| MCU | ESP32-P4 @ 400MHz |
| Camera | IMX477 |
| Lens | Arducam 8–50mm zoom |
| Laser Rangefinder | JRT D09C |
| IMU | ISM330DHCX |
| Magnetometer | RM3100 |
| Barometer | BMP581 |
| Display | 390×390 AMOLED |
| Input | I2C rotary encoder + power button + range trigger |
| GNSS | Optional external receiver |

---

## Architecture

DOPE-ASS is layered cleanly around the DOPE submodule:

```
┌───────────────────────────────┐
│        UI / Application       │  ← DOPE-ASS
│  - Profiles                   │
│  - Target selection           │
│  - Rendering                  │
├───────────────────────────────┤
│   DOPE (Ballistic Engine)     │  ← DOPE submodule
│  - AHRS                       │
│  - Atmosphere                 │
│  - Drag integration           │
│  - Coriolis / Eötvös          │
│  - Spin drift / cant          │
├───────────────────────────────┤
│       Sensor Drivers          │  ← DOPE-ASS
│  - IMU, Mag, Baro, LRF        │
│  - Encoder                    │
└───────────────────────────────┘
```

Each cycle, DOPE-ASS collects raw sensor data, normalizes it into a `SensorFrame`, and calls `BCE_Update()`. DOPE returns a `FiringSolution` with hold values in MOA/MIL that DOPE-ASS renders on the display.

DOPE is a read-only submodule. DOPE-ASS never writes to it.

---

## What DOPE-ASS Does

- Drives all peripherals: IMX477 camera, 390×390 AMOLED, LRF, IMU, magnetometer, barometer, encoder
- Renders a **first-focal-plane etched-glass reticle** that scales correctly at all zoom levels
- HUD always shows: **range, hold elevation, hold windage, battery, mode**
- Three view modes: **Camera** (live feed + HUD), **Data** (full solution readout), **Settings**
- Two ranging modes: **Manual** (range trigger fires one shot) and **Continuous** (1–16 Hz auto-update)
- Hierarchical profile system: Gun → Cartridge → Atmosphere → Preferences
- Interactive in-app zeroing procedure
- Calibration mode for IMU bias, magnetometer hard/soft iron, boresight, and baro
- Data logging on shot events, session start/end, and calibration state changes
- Latitude sourced from GPS (if present), manual entry, or magnetometer dip angle estimation — in that priority order

---

## Repo Structure

```
DOPE-ASS/
├── DOPE/               # Ballistic engine submodule (read-only)
├── src/                # Application source
├── DOPE-ASS SRS.md     # Full software requirements for this repo
├── DOPE/DOPE-ASS SRS.md # DOPE engine SRS reference (in submodule)
└── README.md           # You are here
```

---

## Submodule Setup

DOPE is included as a git submodule so that updates to the ballistic engine flow in immediately without copying code.

```sh
git clone --recurse-submodules https://github.com/your-org/DOPE-ASS
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

To pull the latest DOPE engine:

```sh
git submodule update --remote DOPE
```

---

## Documentation

- [DOPE-ASS SRS](DOPE-ASS%20SRS.md) — full requirements for the application layer
- [DOPE Engine SRS](DOPE/DOPE-ASS%20SRS.md) — reference specification from the DOPE submodule

---

## Status

Early development. Hardware prototype in progress. See [Open Items](DOPE-ASS%20SRS.md#15-open-items) in the SRS for what's still TBD.
