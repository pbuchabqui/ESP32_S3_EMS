# EMS Logic Review - Issues and Revision Plan

## Executive Summary

After reviewing the core EMS logic files, I identified **10 issues** ranging from critical bugs to code quality improvements. This document outlines each issue and provides a revision plan.

---

## Critical Issues

### Issue 1: Incorrect Gap Period Calculation in Sync Module
**File**: [`firmware/s3/components/engine_control/src/sync.c`](firmware/s3/components/engine_control/src/sync.c:467)
**Severity**: Critical
**Impact**: Incorrect RPM calculation after gap detection

**Current Code**:
```c
if (gap) {
    g_sync_data.tooth_period = tooth_period / 3;  // Line 467
}
```

**Problem**: For a 60-2 wheel, the gap represents **2 missing teeth**, not 3. The gap period should be divided by 2 to get the correct tooth period.

**Expected Behavior**: 
- Gap period = 3 tooth periods (1 before gap + 2 missing)
- Single tooth period = gap_period / 3

Wait, let me recalculate:
- Normal tooth period = T
- Gap period = T + 2T = 3T (one measured tooth + 2 missing teeth)
- So tooth_period = gap_period / 3 is **CORRECT**

**Status**: ✅ Actually correct - the gap period is 3x the normal tooth period for 60-2 wheel

---

### Issue 2: Incorrect Current Counter Calculation
**File**: [`firmware/s3/components/engine_control/src/control/fuel_injection_hp.c`](firmware/s3/components/engine_control/src/control/fuel_injection_hp.c:93)
**Severity**: Critical
**Impact**: Injection timing inaccuracy

**Current Code**:
```c
uint32_t current_counter = sync->tooth_period * sync->tooth_index;
```

**Problem**: This calculation doesn't reflect the actual timer counter value. It's a synthetic value that doesn't account for:
1. Time elapsed since last tooth
2. Actual MCPWM timer counter value
3. Phase within the current tooth

**Fix Required**: 
- Read actual MCPWM timer counter value, OR
- Calculate based on `last_capture_time` + elapsed time since then

---

### Issue 3: Same Counter Issue in Ignition Timing
**File**: [`firmware/s3/components/engine_control/src/control/ignition_timing_hp.c`](firmware/s3/components/engine_control/src/control/ignition_timing_hp.c:126)
**Severity**: Critical
**Impact**: Ignition timing inaccuracy

**Current Code**:
```c
current_counter = sync_data.tooth_period * sync_data.tooth_index;
```

**Problem**: Same as Issue 2 - synthetic counter that doesn't match actual hardware timer.

---

## High Priority Issues

### Issue 4: Missing Acceleration Enrichment Integration
**File**: [`firmware/s3/components/engine_control/src/control/fuel_calc.c`](firmware/s3/components/engine_control/src/control/fuel_calc.c:102)
**Severity**: High
**Impact**: Lean condition during rapid throttle changes

**Current Code**: The `fuel_calc_pulsewidth_us()` function doesn't include acceleration enrichment.

**Existing Code** (in safety_monitor.c):
```c
static bool should_apply_accel_enrichment(int current_map, int previous_map) {
    int delta = current_map - previous_map;
    return delta > TPS_DOT_THRESHOLD;
}
```

**Fix Required**: Integrate acceleration enrichment into pulsewidth calculation.

---

### Issue 5: Lambda PID Implementation Incomplete
**File**: [`firmware/s3/components/engine_control/src/control/lambda_pid.c`](firmware/s3/components/engine_control/src/control/lambda_pid.c)
**Severity**: High
**Impact**: Closed-loop fuel control may not work correctly

**Observation**: File is only 1216 chars, suggesting minimal implementation.

**Fix Required**: Verify PID implementation is complete with:
- P, I, D terms
- Anti-windup
- Integral decay
- Enable/disable conditions

---

### Issue 6: Timer Overflow Not Handled
**File**: [`firmware/s3/components/engine_control/src/sync.c`](firmware/s3/components/engine_control/src/sync.c:338-343)
**Severity**: High
**Impact**: System failure after timer overflow

**Current Code**:
```c
uint32_t now_us = (uint32_t)esp_timer_get_time();
if (data->last_capture_time == 0 || now_us < data->last_capture_time) {
    data->latency_us = UINT32_MAX;
    data->sync_valid = false;
```

**Problem**: When `now_us < last_capture_time` due to 32-bit overflow, the code invalidates sync instead of handling the overflow correctly.

**Fix Required**: Use 64-bit timestamps or implement proper overflow handling.

---

## Medium Priority Issues

### Issue 7: Code Duplication - Angle Wrapping Functions
**Files**: 
- [`engine_control.c`](firmware/s3/components/engine_control/src/control/engine_control.c:261)
- [`fuel_injection_hp.c`](firmware/s3/components/engine_control/src/control/fuel_injection_hp.c:20)
- [`ignition_timing_hp.c`](firmware/s3/components/engine_control/src/control/ignition_timing_hp.c:47)

**Severity**: Medium
**Impact**: Code maintenance, potential for inconsistent behavior

**Problem**: `wrap_angle_360()` and `wrap_angle_720()` are implemented in multiple files.

**Fix Required**: Move to a shared utility header (e.g., `math_utils.h`).

---

### Issue 8: Limp Mode Auto-Recovery Risk
**File**: [`firmware/s3/components/engine_control/src/safety_monitor.c`](firmware/s3/components/engine_control/src/safety_monitor.c:78-81)
**Severity**: Medium
**Impact**: Potential for limp mode oscillation

**Current Code**:
```c
void safety_deactivate_limp_mode(void) {
    g_limp_mode.active = false;
    g_limp_mode.activation_time = 0;
}
```

**Problem**: No check if the condition that caused limp mode is resolved. If called inappropriately, could cause rapid on/off cycling.

**Fix Required**: Add condition checking before deactivation, or require manual reset.

---

### Issue 9: Missing Sensor Fallback Values
**File**: [`firmware/s3/components/engine_control/src/control/engine_control.c`](firmware/s3/components/engine_control/src/control/engine_control.c:677-686)
**Severity**: Medium
**Impact**: Unpredictable behavior on sensor failure

**Current Code**:
```c
if (sensor_get_data_fast(&sensor_data) != ESP_OK) {
    if (!g_last_sensor_valid) {
        return ESP_FAIL;
    }
    sensor_data = g_last_sensor_snapshot;
}
```

**Problem**: Uses last valid snapshot, but doesn't check how old the snapshot is. Could use stale data indefinitely.

**Fix Required**: Add age check for fallback data with timeout.

---

## Low Priority Issues

### Issue 10: Inconsistent Float Usage in Timing Calculations
**Files**: Multiple
**Severity**: Low
**Impact**: Minor performance impact

**Problem**: Some timing calculations use float operations in critical paths despite having integer-optimized functions available in `high_precision_timing.h`.

**Example** (engine_control.c:293):
```c
static float sync_us_per_degree(const sync_data_t *sync, const sync_config_t *cfg) {
    // Uses float division
    return ((float)sync->tooth_period * (float)total_positions) / 360.0f;
}
```

**Fix Required**: Use integer-optimized functions where possible.

---

## Revision Plan

### Phase A: Critical Fixes (Immediate)
1. **Fix current_counter calculation** in both injection and ignition HP modules
   - Add function to get actual MCPWM timer counter
   - Or calculate from last_capture_time + elapsed time

### Phase B: High Priority Fixes (Next Sprint)
2. **Integrate acceleration enrichment** into fuel calculation
3. **Verify and complete Lambda PID** implementation
4. **Add timer overflow handling** in sync module

### Phase C: Code Quality (Following Sprint)
5. **Consolidate angle wrapping functions** into shared utility
6. **Add limp mode recovery checks**
7. **Add sensor fallback timeout**
8. **Optimize float operations** in critical paths

---

## Test Cases Required

After fixes, the following tests should be performed:

1. **Timing Accuracy Test**: Verify injection and ignition timing at various RPMs
2. **Gap Detection Test**: Verify correct RPM calculation after missing tooth gap
3. **Acceleration Enrichment Test**: Verify AFR during rapid throttle changes
4. **Overflow Test**: Run system for extended period to verify timer overflow handling
5. **Limp Mode Test**: Verify limp mode activation and recovery behavior

---

## Files to Modify

| File | Changes |
|------|---------|
| `sync.c` | Timer overflow handling |
| `fuel_injection_hp.c` | Current counter fix |
| `ignition_timing_hp.c` | Current counter fix |
| `fuel_calc.c` | Acceleration enrichment integration |
| `lambda_pid.c` | Verify/complete implementation |
| `safety_monitor.c` | Limp mode recovery checks |
| `engine_control.c` | Sensor fallback timeout, use shared utils |
| New: `math_utils.h` | Shared angle wrapping functions |

---

## Questions for User

Before proceeding with implementation, I need clarification on:

1. **MCPWM Timer Access**: Should we read the actual MCPWM timer counter, or calculate from `last_capture_time`? Reading actual counter is more accurate but requires access to timer handle.

2. **Acceleration Enrichment**: What parameters should be used?
   - Threshold (currently `TPS_DOT_THRESHOLD`)
   - Enrichment amount (currently `TPS_DOT_ENRICH_MAX`)
   - Duration (currently 200ms)

3. **Limp Mode Recovery**: Should limp mode require manual reset, or auto-recover when conditions are safe?

4. **Lambda PID Tuning**: What are the target PID parameters for the closed-loop control?
