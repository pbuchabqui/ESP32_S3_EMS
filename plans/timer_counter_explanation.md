# Timer Counter: Two Approaches Explained

## The Problem

The current code calculates `current_counter` incorrectly:

```c
// Current (WRONG) - in fuel_injection_hp.c:93 and ignition_timing_hp.c:126
uint32_t current_counter = sync->tooth_period * sync->tooth_index;
```

This creates a **synthetic** counter value that doesn't represent actual time.

### Example of the Problem

At 6000 RPM with a 60-2 wheel:
- `tooth_period` ≈ 170 µs (time between teeth)
- `tooth_index` = 30 (halfway through the wheel)
- `current_counter` = 170 × 30 = **5100 µs**

But the **actual** time since the engine started might be **15000 µs**!

This causes injection and ignition timing to be completely wrong.

---

## Approach 1: Read Actual MCPWM Timer Counter

### How It Works

The MCPWM hardware has a timer that runs continuously. We can read its current value directly.

```c
// Read the actual hardware timer
uint32_t current_counter;
mcpwm_timer_get_counter_value(g_channels_hp[cylinder].timer, &current_counter);
```

### Advantages
- **Most accurate** - uses actual hardware time
- **No calculation drift** - hardware timer is always correct
- **Handles overflow automatically** - timer wraps at 30 seconds

### Disadvantages
- Requires access to timer handle (may need to pass through function calls)
- Slightly more complex code structure
- Timer read has small latency (~1-2 µs)

### Implementation

```c
// In mcpwm_injection_hp.c - add helper function
IRAM_ATTR uint32_t mcpwm_injection_hp_get_counter(uint8_t channel) {
    uint32_t counter = 0;
    if (channel < 4 && g_channels_hp[channel].timer) {
        mcpwm_timer_get_counter_value(g_channels_hp[channel].timer, &counter);
    }
    return counter;
}

// In fuel_injection_hp.c - use actual counter
uint32_t current_counter = mcpwm_injection_hp_get_counter(cylinder_id - 1);
```

---

## Approach 2: Calculate from Last Capture Time

### How It Works

Use the `esp_timer_get_time()` function to get current time, then calculate elapsed time since last tooth capture.

```c
// Get current time
uint64_t now_us = esp_timer_get_time();

// Get last tooth capture time from sync module
uint64_t last_capture_us = sync->last_capture_time;

// Calculate elapsed time since last tooth
uint32_t elapsed_since_tooth = (uint32_t)(now_us - last_capture_us);

// Current position = last tooth time + elapsed
uint32_t current_counter = last_capture_us + elapsed_since_tooth;
```

Wait, that's just `now_us`. Let me reconsider...

Actually, the approach would be:

```c
// Current time in microseconds
uint32_t now_us = (uint32_t)esp_timer_get_time();

// Use this as the "counter" for absolute timing
uint32_t current_counter = now_us;
```

### Advantages
- **Simpler** - no need to access MCPWM timer handles
- **Consistent** - same time base across all modules
- **Already available** - `esp_timer_get_time()` is always available

### Disadvantages
- **Less accurate** - `esp_timer_get_time()` has higher latency (~5-10 µs)
- **Potential drift** - if esp_timer and MCPWM timer have slight frequency differences
- **Overflow handling** - need to handle 32-bit overflow explicitly

### Implementation

```c
// In fuel_injection_hp.c
uint32_t current_counter = (uint32_t)esp_timer_get_time();
```

---

## Comparison Table

| Aspect | MCPWM Timer | esp_timer |
|--------|-------------|-----------|
| **Accuracy** | ±1 µs | ±5-10 µs |
| **Latency** | ~1-2 µs | ~5-10 µs |
| **Complexity** | Medium | Low |
| **Overflow** | Auto (30s) | Manual (72 min) |
| **Consistency** | Per-channel | Global |
| **ISR Safe** | Yes | Yes |

---

## My Recommendation: Hybrid Approach

Use **MCPWM timer** for the actual scheduling (which is already done correctly), but use **esp_timer** for calculating the current position when sync data is needed.

```c
// For scheduling - already correct in mcpwm_injection_hp.c
mcpwm_timer_get_counter_value(timer, &current_counter);

// For angle calculations - use sync data + elapsed time
uint32_t now_us = (uint32_t)esp_timer_get_time();
uint32_t elapsed_since_last_tooth = now_us - sync->last_capture_time;
float additional_angle = (elapsed_since_last_tooth / us_per_deg);
float current_angle = base_angle + additional_angle;
```

---

## Why the Current Code is Wrong

The current code:
```c
uint32_t current_counter = sync->tooth_period * sync->tooth_index;
```

Assumes:
1. All teeth have the same period (not true - varies with RPM changes)
2. The counter starts at 0 at tooth 0 (not true - timer runs continuously)
3. No time has passed since the last tooth (not true - there's always delay)

### Real-World Impact

At 6000 RPM:
- Target ignition advance: 30° BTDC
- Current code error: Could be ±50° or more
- Result: **Engine could fire at wrong time, potentially causing damage**

---

## Questions?

Do you want me to:
1. **Use MCPWM timer** - more accurate, requires code restructuring
2. **Use esp_timer** - simpler, slightly less accurate
3. **Hybrid approach** - best of both worlds

Let me know and I'll implement the fix.
