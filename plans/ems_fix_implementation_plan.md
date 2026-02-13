# EMS Logic Fixes - Implementation Plan

## Decision: Use MCPWM Timer (Lowest Latency)

Based on user requirement for lowest latency, we will use the **MCPWM Timer** approach.

### Latency Comparison
| Approach | Latency |
|----------|---------|
| **MCPWM Timer** | ~1-2 µs ✅ |
| esp_timer | ~5-10 µs |

---

## Implementation Plan

### Phase A: Critical Fixes

#### A1: Fix current_counter in MCPWM Injection HP
**File**: `firmware/s3/components/engine_control/src/mcpwm_injection_hp.c`

**Changes**:
1. Add function to read timer counter:
```c
IRAM_ATTR uint32_t mcpwm_injection_hp_get_counter(uint8_t channel) {
    uint32_t counter = 0;
    if (channel < 4 && g_channels_hp[channel].timer) {
        mcpwm_timer_get_counter_value(g_channels_hp[channel].timer, &counter);
    }
    return counter;
}
```

2. Export function in header `mcpwm_injection_hp.h`

#### A2: Fix current_counter in MCPWM Ignition HP
**File**: `firmware/s3/components/engine_control/src/mcpwm_ignition_hp.c`

**Changes**:
1. Add function to read timer counter:
```c
IRAM_ATTR uint32_t mcpwm_ignition_hp_get_counter(uint8_t channel) {
    uint32_t counter = 0;
    if (channel < 4 && g_channels_hp[channel].timer) {
        mcpwm_timer_get_counter_value(g_channels_hp[channel].timer, &counter);
    }
    return counter;
}
```

2. Export function in header `mcpwm_ignition_hp.h`

#### A3: Fix fuel_injection_hp.c to use actual counter
**File**: `firmware/s3/components/engine_control/src/control/fuel_injection_hp.c`

**Change** (line 93):
```c
// OLD:
uint32_t current_counter = sync->tooth_period * sync->tooth_index;

// NEW:
uint32_t current_counter = mcpwm_injection_hp_get_counter(cylinder_id - 1);
```

#### A4: Fix ignition_timing_hp.c to use actual counter
**File**: `firmware/s3/components/engine_control/src/control/ignition_timing_hp.c`

**Change** (line 126):
```c
// OLD:
current_counter = sync_data.tooth_period * sync_data.tooth_index;

// NEW:
current_counter = mcpwm_ignition_hp_get_counter(cylinder - 1);
```

---

### Phase B: High Priority Fixes

#### B1: Add Timer Overflow Handling in Sync Module
**File**: `firmware/s3/components/engine_control/src/sync.c`

**Changes**:
1. Use 64-bit timestamps internally
2. Handle 32-bit overflow in latency calculation:
```c
// Handle overflow correctly
uint32_t latency_us;
if (now_us >= data->last_capture_time) {
    latency_us = now_us - data->last_capture_time;
} else {
    // Overflow occurred
    latency_us = (UINT32_MAX - data->last_capture_time) + now_us;
}
```

#### B2: Integrate Acceleration Enrichment
**File**: `firmware/s3/components/engine_control/src/control/fuel_calc.c`

**Changes**:
1. Add acceleration enrichment factor to pulsewidth calculation
2. Track previous MAP value for delta calculation
3. Apply enrichment when MAP delta exceeds threshold

#### B3: Verify Lambda PID Implementation
**File**: `firmware/s3/components/engine_control/src/control/lambda_pid.c`

**Verify**:
- P, I, D terms implemented
- Anti-windup present
- Enable/disable conditions

---

### Phase C: Code Quality

#### C1: Create Shared Math Utilities
**New File**: `firmware/s3/components/engine_control/include/math_utils.h`

**Content**:
```c
IRAM_ATTR static inline float wrap_angle_360(float angle_deg);
IRAM_ATTR static inline float wrap_angle_720(float angle_deg);
IRAM_ATTR static inline float clamp_float(float v, float min_v, float max_v);
```

#### C2: Add Limp Mode Recovery Checks
**File**: `firmware/s3/components/engine_control/src/safety_monitor.c`

**Changes**:
1. Add condition check before deactivation
2. Add minimum time in limp mode before recovery allowed
3. Add recovery hysteresis

#### C3: Add Sensor Fallback Timeout
**File**: `firmware/s3/components/engine_control/src/control/engine_control.c`

**Changes**:
1. Add timestamp to sensor snapshot
2. Check age before using fallback data
3. Maximum age: 100ms

---

## Files to Modify Summary

| File | Phase | Changes |
|------|-------|---------|
| `mcpwm_injection_hp.c` | A1 | Add get_counter function |
| `mcpwm_injection_hp.h` | A1 | Export get_counter |
| `mcpwm_ignition_hp.c` | A2 | Add get_counter function |
| `mcpwm_ignition_hp.h` | A2 | Export get_counter |
| `fuel_injection_hp.c` | A3 | Use actual counter |
| `ignition_timing_hp.c` | A4 | Use actual counter |
| `sync.c` | B1 | Overflow handling |
| `fuel_calc.c` | B2 | Accel enrichment |
| `lambda_pid.c` | B3 | Verify implementation |
| `math_utils.h` | C1 | New file - shared utils |
| `safety_monitor.c` | C2 | Limp mode checks |
| `engine_control.c` | C3 | Sensor timeout |

---

## Test Plan

After implementation, verify:

1. **Timing Accuracy**
   - Injection timing within ±0.5° at all RPM
   - Ignition timing within ±0.5° at all RPM

2. **Overflow Handling**
   - Run for >72 minutes (32-bit µs overflow)
   - Verify no timing glitches

3. **Acceleration Enrichment**
   - Rapid throttle tip-in
   - Verify AFR stays within target

4. **Limp Mode**
   - Verify activation on fault
   - Verify no oscillation on recovery

---

## Ready for Implementation

This plan is ready for Code mode implementation. Switch to Code mode to begin Phase A fixes.
