# ECU S3 Pro-Spec - Project Specification

Pasta de referencia do projeto: `C:\Users\Pedro\Desktop\ESP32-EFI`

## 1. Purpose
This project implements an engine control unit (ECU) using a single ESP32-S3 for real-time engine control and supervision/connectivity. The design targets precise angular synchronization, sequential fuel injection, just-in-time ignition, and closed-loop lambda control with robust safety and diagnostics.

## 2. System Architecture
- Single-processor architecture.
- ESP32-S3: engine control, sensor acquisition, actuator timing, and supervision/connectivity (ESP-NOW, USB, CAN for peripherals).

High-level flow:
1. Capture: CKP/CMP pulses -> PCNT/ETM -> sync.
2. Decision: interpolation of 16x16 maps (fuel, ignition) + corrections.
3. Execution: MCPWM generates injector and ignition pulses.
4. Feedback: ADC reads sensors for closed-loop and protections.

## 3. Hardware Specification
ESP32-S3 (Engine Control + Supervision)
- Dual-core, up to 240 MHz.
- Flash/PSRAM: TBD per board variant.
- ADC: 12-bit, 7 channels (MAP, CLT, IAT, TPS, O2, VBAT, spare).
- MCPWM: 4 channels for injectors.
- MCPWM: 4 channels for ignition.

Connectivity
- ESP-NOW for supervision and updates.
- USB CDC (mutually exclusive with ESP-NOW).
- CAN (TWAI): available for external peripherals.

Sensors (primary)
- MAP: 0-250 kPa.
- CLT: -40 to 120 C.
- IAT: -40 to 120 C.
- TPS: 0-100%.
- O2: 0-5 V.
- CKP/CMP: crank/cam position (60-2 wheel).
- Battery voltage: 8-16 V.
- Spare ADC: user-defined.

Actuators
- 4 injectors, port injection.
- 4 ignition coils.

Pin mapping (reference)
- Injectors: GPIO 12, 13, 15, 2.
- Ignition: GPIO 16, 17, 18, 21.
- Sensors (analog): ADC1 CH0..CH5 (board-routed).
- CKP/CMP: GPIO 34, 35.
- CAN (TWAI): GPIO 4 (TX), GPIO 5 (RX).
- Communication: ESP-NOW or USB CDC (mutually exclusive).

## 4. Software Architecture
RTOS: FreeRTOS.
Suggested task priorities:
- Control loop: 10.
- Sensor reading: 9.
- Communication: 8.
- System monitoring: 7.

Key components (ESP-IDF)
- engine_control: orchestration and control logic.
- sync: angular synchronization and phase detection.
- sensor_processing: sensor acquisition, filtering, calibration.
- fuel_injection: injector pulse computation and scheduling.
- ignition_timing: ignition advance computation and scheduling.
- safety_monitor: watchdog, protections, limp mode.
- config_manager: persistent configuration and maps in NVS.
- logger: structured logging.
- espnow_link: wireless supervision link (ESP-NOW).
- twai_bridge: CAN (TWAI) interface for external peripherals.
- pcnt_rpm / rpm_measurement: RPM measurement, filtering, diagnostics.

## 5. Engine Control Logic
Fuel injection
- Speed-density strategy with 16x16 fuel map.
- Injector pulse width range: 500 to 20000 us.
- Sequential injection with just-in-time scheduling.
- Battery compensation and warmup/accel enrichment.

Ignition timing
- 16x16 ignition map based on RPM and load.
- Advance range: 5 to 35 degrees (base 10 deg).
- Knock protection retards timing.

Lambda closed-loop
- PID control with anti-windup.
- Correction clamp: 25%.

Start-up
1. Initialize cranking logic and prime pulse injection.
2. Partial sync at 360 degrees.
3. Full sync at 720 degrees.

## 5.1 Logic Flow (Operational Sequence)
This section describes the end-to-end runtime logic of the ECU.

1. System boot
   - Initialize RTOS, clocks, peripherals, and configuration.
   - Load calibration tables and limits from NVS.
   - Start safety monitor and logging.

2. Sensor acquisition
   - ADC acquires MAP, TPS, CLT, IAT, O2, battery voltage, and spare.
   - MAP and TPS have higher sampling priority.
   - Digital filtering produces stable engineering values.
   - Sensor validation flags faults and substitutes limp defaults if needed.

3. Synchronization (CKP/CMP)
   - PCNT counts teeth and ETM timestamps events.
   - Gap detection identifies the missing-tooth reference.
   - Phase detection validates the 720-degree cycle.
   - RPM and time-per-degree are updated.

4. Control computation (per control loop tick)
   - Load is computed from MAP and RPM.
   - Fuel pulse width is derived from the 16x16 fuel map.
   - Corrections apply: warmup, accel enrichment, battery compensation, lambda PID.
   - Ignition advance is derived from the 16x16 ignition map.
   - Knock logic may retard advance.

5. Actuation scheduling
   - MCPWM schedules injector pulses just-in-time.
   - MCPWM schedules ignition dwell and spark timing (no fallback).
   - All timing uses sync-derived angle/time.

6. Feedback and safety
   - Closed-loop lambda adjusts fuel in real time.
   - Safety monitor checks RPM, temperature, voltage, and sensor plausibility.
   - If faults are detected, the system enters limp mode or cuts fuel/ignition.

7. Communication and supervision
   - S3 publishes engine and sensor data via ESP-NOW or USB CDC (not simultaneously).
   - S3 exposes CAN to external peripherals and diagnostics.
   - Commands or configuration updates are validated and applied.

8. Start/stop behavior
   - Start: prime pulse and partial sync, then full sync.
   - Stop: actuators disabled and state stored for safe shutdown.

## 5.2 Startup Sync (Target Structure)
- Single core system tracks CKP/CMP during cranking and maintains sync state.
- Actuation starts after stable sync is acquired.
- RTC memory can be used to accelerate restarts after resets.

## 6. Synchronization (Sync Module)
- 60-2 wheel decoding (58 teeth + 2 gap).
- Gap detection using tooth period increase (~1.5x).
- Phase detection for 720-degree cycle.
- RPM calculation from tooth period.
- Noise filter: 100 ns.
- Limits: 500 to 8000 RPM.
- Hardware timing chain (target):
  - PCNT counts teeth and applies glitch filter.
  - ETM routes tooth events to a high-resolution timer capture.
  - Timer capture provides deterministic tooth timestamps without CPU jitter.

## 7. Sensor Acquisition
- ADC sampling using DMA/continuous or oneshot (implementation dependent).
- Target sampling rate: 20 kHz ADC, 1 kHz processing, 100 Hz reporting.
- Filters:
  - MAP: moving average (16 samples).
  - TPS: 1st-order low-pass.
  - Temperatures: exponential filter.
- Conversion to engineering units (kPa, C, %, mV).

## 7.1 Timing and Peripherals (Target Structure)
- CKP/CMP: PCNT + ETM + high-resolution timer capture.
- Injection: MCPWM (one-shot pulses scheduled by angle).
- Ignition: MCPWM (dwell + spark timing).
- Safety: multi-stage watchdog (MWDT/RWDT).

## 8. RPM Measurement (PCNT)
- PCNT-based measurement replaces manual ISR counting.
- Filters: EMA for stability.
- Diagnostics: open/short, invalid signal, out-of-range.
- Buffer for history and quality metrics.

## 9. Safety and Diagnostics
Protections
- RPM limiter: 8000 RPM.
- Fuel cutoff: 7500 RPM.
- Temperature limit: 120 C.
- Battery range: 8 to 16 V.

Limp mode
- Conservative fuel and timing values.
- Triggered by sensor faults or heartbeat loss.

Error detection
- Sensor range checks and plausibility.
- Watchdog timers.
- Communication heartbeat monitoring.

## 10. System States
- Normal operation: all sensors and comms OK.
- Limp mode: degraded performance with safe defaults.
- Error states: over-rev, overheat, sensor failure, comm failure.

## 11. Communication
- ESP-NOW for supervision and updates (not used simultaneously with USB).
- USB CDC: 115200 baud (mutually exclusive with ESP-NOW).
- CAN (TWAI): 500 kbps for external peripherals.

Message types
- Engine data, sensor data, diagnostics, control/config (over ESP-NOW or USB).
- Peripheral/diagnostic traffic over CAN.

## 12. Performance Targets
- Sync response: < 100 us.
- Injection timing: < 50 us.
- Ignition timing: < 20 us.
- Closed-loop update: < 1 ms.
- Angular accuracy: +-0.5 deg.
- RPM accuracy: +-10 RPM.

## 13. Data and Configuration
- 16x16 fuel and ignition tables.
- Persistent config in NVS with CRC32 and versioning.
- Calibration for sensors and battery compensation.

## 14. Build and Firmware Layout
- Single project:
  - firmware/s3: ESP32-S3 engine control + supervision.
- ESP-IDF v5.5.1 or later.
- Partition table includes nvs, ota, phy, app slots, config, storage.

## 15. Current Implementation Status (as of 2026-02-11)
- ESP32-S3 defined as primary hardware target.
- PCNT migrated to pulse_cnt driver.
- ADC path implemented and integrated with sensor processing.
- MCPWM injection and MCPWM ignition backends implemented.

## 16. Planned Enhancements
- ADC continuous with DMA for higher throughput.
- ESP-NOW framing and async processing.
- Interpolation cache for steady-state optimization.
- CLI and data logger.
- Remote tuning protocol.

End of specification.

