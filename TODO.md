# TODO: ESP32-S3 EFI Development Roadmap

## Completed Tasks

### Phase 1: Remove P4-related Code ✅
- [x] Remove `firmware/s3/components/engine_control/include/p4_control_config.h`
- [x] Remove `firmware/s3/sdkconfig.old`
- [x] Update any includes that reference p4_control_config.h (No references found)

### Phase 2: Add IRAM_ATTR to Timing-Critical Functions ✅
- [x] high_precision_timing.h/.c - hp_update_phase_predictor(), hp_record_jitter()
- [x] mcpwm_injection_hp.c - scheduling functions
- [x] mcpwm_ignition_hp.c - scheduling and calculation functions
- [x] engine_control.c - engine_sync_tooth_callback()
- [x] sync.c - sync_update_from_capture(), sync_update_cmp_capture()

### Phase 3: Optimize High Precision Timing Code ✅
- [x] Integer-optimized conversion functions (hp_us_to_cycles, hp_cycles_to_us_u32)
- [x] Utility functions (hp_elapsed_cycles, hp_elapsed_us, hp_deadline_reached)
- [x] RPM-based conversions (hp_us_per_degree, hp_degrees_to_us, hp_us_to_degrees)
- [x] Precise busy-wait delay (hp_delay_us)

### Phase 4: Code Organization Improvements ✅
- [x] Updated documentation in header files
- [x] Added IRAM_ATTR declarations to headers
- [x] Improved function documentation with @note for IRAM_ATTR functions
- [x] Removed ESP_LOG calls from IRAM functions

### Phase 5-9: Design Documents ✅
- [x] ESP-NOW Module Design - [`docs/design/espnow_module_design.md`](docs/design/espnow_module_design.md)
- [x] CLI Interface Design - [`docs/design/cli_interface_design.md`](docs/design/cli_interface_design.md)
- [x] Data Logger Design - [`docs/design/data_logger_design.md`](docs/design/data_logger_design.md)
- [x] Remote Tuning Protocol Design - [`docs/design/tuning_protocol_design.md`](docs/design/tuning_protocol_design.md)
- [x] Testing and Validation Design - [`docs/design/testing_validation_design.md`](docs/design/testing_validation_design.md)

### EMS Logic Review & Fixes ✅

**Issues Identified and Fixed:**

#### Critical Fixes (Phase A)
- [x] **Fix current_counter calculation** - Now uses actual MCPWM timer counter instead of synthetic value
  - Added `mcpwm_injection_hp_get_counter()` function
  - Added `mcpwm_ignition_hp_get_counter()` function
  - Updated `fuel_injection_hp.c` to use real counter
  - Updated `ignition_timing_hp.c` to use real counter

#### High Priority Fixes (Phase B)
- [x] **Acceleration enrichment integration** - Added to fuel calculation
  - New function `fuel_calc_accel_enrichment()`
  - Integrated into `fuel_calc_pulsewidth_us()`
  - Uses MAP delta detection with 200ms decay

- [x] **Lambda PID verification** - Implementation confirmed complete
  - P, I, D terms implemented
  - Anti-windup present
  - Output clamping

- [x] **Timer overflow handling** - Fixed in sync module
  - Proper 32-bit overflow handling in `sync_get_data()`
  - Correct latency calculation across overflow boundary

#### Code Quality Fixes (Phase C)
- [x] **Limp mode recovery checks** - Added safety checks
  - Minimum 5 seconds in limp mode before recovery
  - 2 second hysteresis for safe conditions
  - New `safety_mark_conditions_safe()` function

- [x] **Sensor fallback timeout** - Added 100ms timeout
  - New `g_last_sensor_timestamp_ms` variable
  - Fallback data invalidated after timeout

- [x] **Consolidated angle wrapping functions** - New shared utility
  - Created `math_utils.h` with shared functions
  - `wrap_angle_360()`, `wrap_angle_720()`, `clamp_float()`, `clamp_u32()`
  - Updated all modules to use shared header

---

## Next Steps (Priority Order)

### Phase 5: Communication Module - ESP-NOW ✅
**Priority: High** | **Estimated Complexity: Medium** | **Design: ✅ Complete**

The spec requires ESP-NOW for supervision and updates (mutually exclusive with USB CDC).

**Design Document**: [`docs/design/espnow_module_design.md`](docs/design/espnow_module_design.md)

- [x] Create technical design document
- [x] Create ESP-NOW module structure
  - [x] `firmware/s3/components/engine_control/include/espnow_link.h`
  - [x] `firmware/s3/components/engine_control/src/espnow_link.c`
- [x] Implement ESP-NOW initialization
- [x] Define message protocol for ECU data
- [x] Implement async framing and processing
- [x] Add to CMakeLists.txt
- [x] Integration with engine_control module

### Phase 6: CLI Interface ✅
**Priority: High** | **Estimated Complexity: Medium** | **Design: ✅ Complete**

**Design Document**: [`docs/design/cli_interface_design.md`](docs/design/cli_interface_design.md)

- [x] Create technical design document
- [x] Create CLI module structure
  - [x] `firmware/s3/components/engine_control/include/cli_interface.h`
  - [x] `firmware/s3/components/engine_control/src/cli_interface.c`
- [x] Implement command parser
- [x] Create essential commands (status, sensors, tables, config, diag, stream, reset)
- [x] USB CDC integration

### Phase 7: Data Logger ✅
**Priority: Medium** | **Estimated Complexity: Medium** | **Design: ✅ Complete**

**Design Document**: [`docs/design/data_logger_design.md`](docs/design/data_logger_design.md)

- [x] Create technical design document
- [x] Create data logger module
  - [x] `firmware/s3/components/engine_control/include/data_logger.h`
  - [x] `firmware/s3/components/engine_control/src/data_logger.c`
- [x] Implement circular buffer
- [x] Implement trigger-based logging

### Phase 8: Remote Tuning Protocol ✅
**Priority: Medium** | **Estimated Complexity: High** | **Design: ✅ Complete**

**Design Document**: [`docs/design/tuning_protocol_design.md`](docs/design/tuning_protocol_design.md)

- [x] Create technical design document
- [x] Create tuning protocol module
  - [x] `firmware/s3/components/engine_control/include/tuning_protocol.h`
  - [x] `firmware/s3/components/engine_control/src/tuning_protocol.c`
- [x] Implement message protocol
- [x] Implement parameter operations

### Phase 9: Testing and Validation ✅
**Priority: High** | **Estimated Complexity: Medium** | **Design: ✅ Complete**

**Design Document**: [`docs/design/testing_validation_design.md`](docs/design/testing_validation_design.md)

- [x] Create technical design document
- [x] Create test framework
  - [x] `firmware/s3/components/engine_control/include/test_framework.h`
  - [x] `firmware/s3/components/engine_control/src/test_framework.c`
- [x] Test assertion macros
- [x] Test registration and execution
- [x] Performance measurement
- [x] Memory leak detection

### Phase 10: Documentation Updates
**Priority: Medium** | **Estimated Complexity: Low**

- [ ] Update spec.md with S3-specific details
- [ ] Update PHASE_2_3_4_SUMMARY.md for S3
- [ ] Create API documentation
- [ ] Update wiring documentation
- [ ] Create tuning guide
- [ ] Create troubleshooting guide

---

## Files Modified Summary

### New Files Created
 | File | Purpose |
 |------|---------|
 | `include/math_utils.h` | Shared math utilities (angle wrap, clamp) |
 | `include/espnow_link.h` | ESP-NOW communication module header |
 | `include/cli_interface.h` | CLI interface module header |
 | `include/data_logger.h` | Data logger module header |
 | `include/tuning_protocol.h` | Remote tuning protocol header |
 | `include/test_framework.h` | Testing framework header |
 | `src/espnow_link.c` | ESP-NOW communication module implementation |
 | `src/cli_interface.c` | CLI interface module implementation |
 | `src/data_logger.c` | Data logger module implementation |
 | `src/tuning_protocol.c` | Remote tuning protocol implementation |
 | `src/test_framework.c` | Testing framework implementation |
 | `plans/ems_logic_review.md` | EMS logic review and issue analysis |
 | `plans/ems_fix_implementation_plan.md` | Implementation plan for fixes |
 | `plans/timer_counter_explanation.md` | Timer counter approach explanation |

### Modified Files
| File | Changes |
|------|---------|
| `include/mcpwm_injection_hp.h` | Added `mcpwm_injection_hp_get_counter()` |
| `src/mcpwm_injection_hp.c` | Implemented `mcpwm_injection_hp_get_counter()` |
| `include/mcpwm_ignition_hp.h` | Added `mcpwm_ignition_hp_get_counter()` |
| `src/mcpwm_ignition_hp.c` | Implemented `mcpwm_ignition_hp_get_counter()` |
| `control/fuel_injection_hp.c` | Use real timer counter, include math_utils.h |
| `control/ignition_timing_hp.c` | Use real timer counter, include math_utils.h |
| `control/fuel_calc.c` | Added acceleration enrichment |
| `include/fuel_calc.h` | Added `fuel_calc_accel_enrichment()` |
| `sync.c` | Fixed timer overflow handling |
| `safety_monitor.c` | Added limp mode recovery checks |
| `include/safety_monitor.h` | Added `safety_mark_conditions_safe()` |
| `control/engine_control.c` | Added sensor fallback timeout, use math_utils.h |

---

## Performance Targets (from spec.md)

| Metric | Target | Status |
|--------|--------|--------|
| Sync response | < 100 us | To verify |
| Injection timing | < 50 us | To verify |
| Ignition timing | < 20 us | To verify |
| Closed-loop update | < 1 ms | To verify |
| Angular accuracy | ±0.5 deg | To verify |
| RPM accuracy | ±10 RPM | To verify |
| RPM range | 500-8000 | Implemented |
| Temperature range | -40 to 120°C | Implemented |

---

## Notes

1. **ESP-NOW vs USB CDC**: These are mutually exclusive according to spec.md. The system should detect which is available and use that.

2. **ADC DMA**: Already implemented in sensor_processing.c using `adc_continuous` driver.

3. **Interpolation Cache**: Already implemented in fuel_calc.c with `interp_cache_t` structure.

4. **Safety Systems**: Already implemented in safety_monitor.c with over-rev, overheat, and voltage protections.

5. **TWAI Lambda**: Already implemented for wideband O2 sensor communication via CAN.

6. **Timer Counter Fix**: The critical fix for using actual MCPWM timer counter values ensures timing accuracy within ±1 µs instead of potentially ±50° error at high RPM.
