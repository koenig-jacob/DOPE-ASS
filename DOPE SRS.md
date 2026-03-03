# Software Requirements Specification

# DOPE — Digital Optical Performance Engine

## Version 2.0 — DRAFT

**Date:** 2026-02-27  
**Language Target:** C++17  
**Primary Platform:** ESP32-P4 @ 400MHz

---

# 1. Introduction

## 1.1 Purpose

**DOPE** (Digital Optical Performance Engine) is a C++ ballistic computation library targeting the ESP32-P4. It takes in normalized sensor data and spits out a `FiringSolution` — that's it. It handles the math: trajectory integration, atmospheric correction, Coriolis, spin drift, cant. Everything else is someone else's problem.

DOPE has no opinion about your display, your UI, your camera, or your target. It doesn't know what platform it's running on and doesn't care. It just does the ballistics.

---

## 1.2 Architectural Boundary

DOPE's boundary is simple: scalar numbers in, scalar numbers out. It doesn't touch camera frames, LiDAR point clouds, depth maps, user profiles, or anything rendered on screen. If you have a vision pipeline or a LiDAR mapper, that lives above DOPE. Whatever it produces, your application layer distills it down to a single range and orientation and hands that to DOPE. DOPE computes one firing solution at a time.

---

# 2. System Architecture

```
┌───────────────────────────────┐
│        UI / Application       │
│  - Profiles                   │
│  - LiDAR mapping              │
│  - Target selection           │
│  - Rendering                  │
├───────────────────────────────┤
│   DOPE (Ballistic Engine)     │
│  - AHRS                       │
│  - Atmosphere                 │
│  - Drag integration           │
│  - Coriolis / Eötvös          │
│  - Spin drift                 │
│  - Cant correction            │
├───────────────────────────────┤
│       Sensor Drivers          │
│  - IMU                        │
│  - Magnetometer               │
│  - Barometer                  │
│  - LRF                        │
│  - Encoder                    │
└───────────────────────────────┘
```

---

# 3. Operating Modes

|Mode|Description|
|---|---|
|IDLE|Insufficient data for solution|
|SOLUTION_READY|Valid firing solution available|
|FAULT|Required inputs missing or invalid|

Range ingestion is continuous — there's no separate RANGING mode. DOPE just keeps the latest valid range and uses it.

---

# 4. Units Policy

All internal calculations use SI units:

- Distance: meters
    
- Velocity: m/s
    
- Pressure: Pascals
    
- Temperature: Celsius (converted to Kelvin internally)
    
- Mass: kg
    
- Angles: radians internally
    

Imperial units are converted at the input boundary only when required by legacy reference algorithms.

---

# 5. Default Model

## 5.1 Internal ISA Defaults

When no overrides are provided, DOPE falls back to ISA-standard conditions:

- Altitude: 0 m
- Pressure: 101325 Pa
- Temperature: 15 °C
- Humidity: 0.5
- Wind: 0 m/s
- Latitude: unset — GPS/GNSS is used if the application provides it; magnetometer-derived otherwise; Coriolis is disabled if neither is available

Running on defaults never triggers a FAULT. DOPE will soldier on with whatever it has and set diagnostic bits to let the application know what it's doing.

---

## 5.2 Default Override Mechanism

The application can seed DOPE's defaults at startup — useful for loading a user profile. Call `BCE_SetDefaultOverrides()` with whichever fields you want to override; the rest stay at ISA values.

```cpp
struct BCE_DefaultOverrides {
    bool use_altitude;
    float altitude_m;

    bool use_pressure;
    float pressure_pa;

    bool use_temperature;
    float temperature_c;

    bool use_humidity;
    float humidity_fraction;

    bool use_wind;
    float wind_speed_ms;
    float wind_heading_deg;

    bool use_latitude;
    float latitude_deg;
};
```

```cpp
void BCE_SetDefaultOverrides(const BCE_DefaultOverrides* defaults);
```

---

# 6. Maximum Trajectory Range

```cpp
constexpr uint32_t BCE_MAX_RANGE_M = 2500;
```

2500 m is the hard ceiling — enough headroom above the current 2 km LRF to accommodate stronger hardware later. All internal buffers are statically allocated to this size at compile time; nothing is heap-allocated at runtime. The application can impose a lower operational limit if needed.

---

# 7. Sensor Ingestion

Everything comes in through one call:

```cpp
void BCE_Update(const SensorFrame* frame);
```

The application populates a `SensorFrame` from its peripheral drivers and calls this each cycle. What DOPE does with each sensor is described below.

---

## 7.1 IMU (ISM330DHCX)

- 3-axis accelerometer
    
- 3-axis gyroscope
    
- Mahony/Madgwick fusion
    
- Outputs quaternion
    
- Computes pitch, roll, yaw
    
- Detects dynamic vs static state
    
- Supports bias calibration
    

---

## 7.2 Magnetometer (RM3100)

- 3-axis magnetic field
    
- Hard iron + soft iron calibration
    
- Declination correction
    
- Disturbance detection
    
- Optional suppression if disturbed
    
- **Magnetic dip angle (inclination) measurement**
    
- **Autonomous latitude estimation from dip angle** (see §7.2.1; lowest-priority source after GPS/GNSS and manual)
    

### 7.2.1 Latitude Estimation via Magnetic Dip

The magnetic dip angle (inclination) ψ is related to geographic latitude φ by the dipole approximation:

```
tan(ψ) ≈ 2 × tan(φ)
```

DOPE computes the dip angle from the calibrated magnetometer vector (ratio of vertical to horizontal field components) and inverts this relation to produce an estimated latitude:

```
φ_estimated = atan(tan(ψ) / 2)
```

This estimate is:

- Autonomous — requires no GPS or manual entry
    
- Sufficient for Coriolis/Eötvös corrections at engagement ranges ≤ 2500 m
    
- Subject to ±1–5° typical error from crustal magnetic anomalies and non-dipole field components; worst-case ±10° in high-anomaly regions
    

Accuracy is adequate because Coriolis corrections are small at typical engagement ranges; a 10° latitude error produces < 0.1 MOA Coriolis error at 1000 m.

**Priority rule:** The magnetometer-derived latitude is used only when neither a GPS/GNSS feed nor a manually entered latitude is available. See priority hierarchy in §11.4.

**Manual entry as calibration:** When the user enters a known latitude manually at a known position, that value serves simultaneously as the live latitude input **and** as a reference calibration point for the magnetometer estimator. This allows the dip-angle model to be locally anchored, improving estimation accuracy in subsequent sessions at or near the same location.

If the magnetometer is disturbed (disturbance flag set), the latitude estimate is suppressed. The same disturbance gate that prevents heading from being updated also invalidates the dip-derived latitude.
    

---

## 7.3 Barometer (BMP581)

Provides absolute pressure (Pa), temperature (°C), and optionally humidity. DOPE uses these to compute air density:

```
ρ = P / (R_specific × T_kelvin)
```

A calibration call is available to zero the baro against local conditions:

```cpp
void BCE_CalibrateBaro(void);
```

---

## 7.4 Laser Rangefinder (JRT D09C)

DOPE ingests LRF data continuously over UART at 1–16 Hz. Each measurement carries a range in meters, a timestamp, and a confidence value. DOPE stores the latest valid measurement, takes a quaternion snapshot of device orientation at the moment the range arrives, and flags the range stale if the timestamp gets too old. There's no RANGING mode — range just flows in whenever the LRF fires.
    

---

## 7.5 GPS / GNSS Receiver (optional)

DOPE doesn't parse NMEA or talk to a GNSS chip directly — that's the application's job. The application resolves a latitude value from whatever receiver it has and passes a single float into DOPE via `BCE_Update` or `BCE_SetDefaultOverrides`. When a GPS-derived latitude is present, it takes top priority in the latitude hierarchy (see §11.4).

---

## 7.6 Zoom Encoder

Accepts the current focal length in mm and computes the corresponding field of view. If optical flow data is available, it converts that to angular velocity as well.
    

---

# 8. Manual Inputs

Persist until changed.

|Parameter|Description|
|---|---|
|BC|Ballistic coefficient|
|Drag model|G1–G8|
|Muzzle velocity|m/s|
|Bullet mass|grains|
|Bullet length|mm|
|Caliber|inches|
|Twist rate|signed inches/turn|
|Zero range|m|
|Sight height|mm|
|Wind speed|m/s|
|Wind heading|deg true|
|Latitude|deg (see §11.4 — GPS/GNSS feed, manual entry, or magnetometer estimation)|
|Altitude override|m|
|Humidity|fraction|

---

# 9. Zeroing Logic

DOPE keeps the zero angle in sync automatically. Any time BC, MV, drag model, zero range, sight height, or atmospheric correction changes, the zero is recomputed. There's no manual trigger for this — it just happens.

---

# 10. Calibration APIs

These calls let the application push physical alignment corrections into DOPE — IMU bias, magnetometer hard/soft iron, boresight offset, reticle mechanical offset, baro zero, and gyro zero.

```cpp
void BCE_SetIMUBias(const float accel_bias[3], const float gyro_bias[3]);
void BCE_SetMagCalibration(const float hard_iron[3], const float soft_iron[9]);
void BCE_SetBoresightOffset(const BoresightOffset* offset);
void BCE_SetReticleMechanicalOffset(float vertical_moa, float horizontal_moa);
void BCE_CalibrateBaro(void);
void BCE_CalibrateGyro(void);
```

---

# 11. Ballistic Solver

## 11.1 Model

Point-mass trajectory with piecewise power-law drag, integrated with an adaptive timestep. The output is a trajectory table at 1 m range resolution.
    

---

## 11.2 Atmospheric Correction

Uses the standard 4-factor ballistic coefficient correction:

```
BC_corrected = BC × FA × (1 + FT - FP) × FR
```

Imperial unit conversions are handled internally where the reference algorithms require them.

---

## 11.3 Wind

Wind is manual-entry only in v1. DOPE decomposes the user's speed and heading into head/crosswind components:

```
headwind  = wind_speed × cos(angle)
crosswind = wind_speed × sin(angle)
```

---

## 11.4 Coriolis and Eötvös

Coriolis and Eötvös corrections need latitude. DOPE accepts it from three sources, in this priority order:

1. **GPS / GNSS** — latitude fed directly by the Application from an attached receiver. Highest accuracy; preempts all other sources.
2. **Manual entry** — latitude entered explicitly by the user via the Application UI or API. Overrides magnetometer estimation. When entered at a known geographic position, also serves as a reference calibration point for the dip-angle estimator.
3. **Magnetometer-derived** — latitude estimated from magnetic dip angle (see §7.2.1). Active only when no GPS feed and no manual entry are present, and the magnetometer is undisturbed.
4. **Unset** — none of the above available; Coriolis disabled.

If no latitude source is available, Coriolis is silently disabled and a diagnostic bit is set in `defaults_active`. No FAULT is raised — it's a graceful degradation.

Earth rotation:

```
ω = 7.2921e-5 rad/s
```

Integrated each timestep.

The `FiringSolution` includes a flag in `defaults_active` indicating which latitude source is active, so the application can show the user an appropriate confidence indicator.

---

## 11.5 Cant

Roll angle from the AHRS is used to compute the lateral POI shift caused by cant. This is folded into the final hold values automatically.

---

## 11.6 Spin Drift

Spin drift is computed using the Litz approximation, which scales with time of flight:

```
drift ∝ TOF^1.83
```

Sign is determined by twist direction — right-hand twist drifts right, left-hand drifts left.

---

# 12. FiringSolution Output

```cpp
struct FiringSolution {
    uint32_t solution_mode;
    uint32_t fault_flags;
    uint32_t defaults_active;

    float hold_elevation_moa;
    float hold_windage_moa;

    float range_m;
    float horizontal_range_m;
    float tof_ms;
    float velocity_at_target_ms;
    float energy_at_target_j;

    float coriolis_windage_moa;
    float coriolis_elevation_moa;
    float spin_drift_moa;
    float wind_only_windage_moa;
    float earth_spin_windage_moa;
    float offsets_windage_moa;
    float cant_windage_moa;

    float cant_angle_deg;
    float heading_deg_true;

    float air_density_kgm3;
};
```

No rendering instructions are included — that's the application's job.

---

# 13. Fault Philosophy

DOPE is conservative about raising hard FAULTs. The only things that will put it into FAULT are genuinely unrecoverable situations: no valid range, no bullet profile loaded, missing MV or BC, an unsolvable zero, AHRS instability, or a severely invalid sensor reading. Everything else — missing environmental data, unknown latitude, stale readings — gets handled with ISA defaults and diagnostic bits. DOPE keeps computing as long as it reasonably can.

---

# 14. Performance Requirements

Targets on ESP32-P4 @ 400MHz:

- 1000 m solution: < 8 ms
- 2500 m solution: < 15 ms
- AHRS update: < 1 ms

No heap allocations after init. All buffers are statically sized at compile time.
    

---

# 15. Hardware Reference

DOPE is hardware-agnostic. It does not depend on any specific peripheral or platform. See the DOPE-ASS SRS for the prototype hardware reference build.

---

# 16. Out of Scope

To be explicit about what DOPE will never do: full 6DOF modeling, variable wind along the flight path, AI target detection, point cloud processing, UI rendering, wireless communication, and data logging. All of that lives above DOPE in the application layer.
    

---

# 17. Summary

DOPE computes ballistic solutions. That's the whole job. It's deterministic, physically consistent, and layered cleanly so that upgrading sensors or adding corrections doesn't require touching unrelated code. Everything above trajectory math — the UI, the vision pipeline, the rendering, the user experience — lives in the application layer, not here.

---

---

# 18. Latitude Source Architecture — Design Rationale

Coriolis and Eötvös corrections need latitude, and we have three ways to get it.

**GPS / GNSS** is the best option when the hardware is present. The application resolves latitude from whatever receiver it has and passes a single float into DOPE — no NMEA parsing happens inside the library. Accuracy is typically better than 0.001°.

**Manual entry** is the intended fallback when GPS isn't available. The user types in their latitude at setup time. This also doubles as a calibration anchor for the magnetometer dip estimator — if you tell DOPE where you are, it can use that to improve the mag-based estimate at that location in future sessions.

**Magnetometer-derived latitude** is the last resort. The RM3100 is already on board for heading, and its measurement of the magnetic dip angle ψ can be inverted to estimate latitude using the dipole approximation `tan(ψ) ≈ 2 × tan(φ)`. This gives ±1–5° accuracy in normal conditions, up to ±10° in magnetically anomalous areas. That sounds coarse, but a 10° latitude error only produces about 0.1 MOA of Coriolis error at 1000 m — well within the system's uncertainty budget. The RM3100 uncertainty budget is treated as **±1.5° flat** for propagation purposes, which is a conservative mid-latitude estimate.

If none of these sources are available, or if the magnetometer is flagged as disturbed (the same flag that suppresses heading also suppresses the dip-derived latitude), Coriolis is quietly disabled and a diagnostic bit is set. No FAULT.

---

End of Document  
DOPE SRS v2.0