#ifndef MCPWM_INJECTION_H
#define MCPWM_INJECTION_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Injection channel configuration
typedef struct {
    uint8_t channel_id;
    gpio_num_t gpio;
    uint32_t pulsewidth_us;  // Pulse width in microseconds
    bool is_active;
} mcpwm_injector_channel_t;

// Injection system configuration
typedef struct {
    uint32_t base_frequency_hz;      // Base frequency (1MHz for 1us resolution)
    uint32_t timer_resolution_bits;  // Timer resolution (kept for compatibility)
    uint32_t min_pulsewidth_us;      // Minimum pulse width
    uint32_t max_pulsewidth_us;      // Maximum pulse width
    uint32_t deadtime_us;            // Dead time between injections
} mcpwm_injection_config_t;

// Initialize MCPWM-based injection system
bool mcpwm_injection_init(void);

// Configure injection parameters
bool mcpwm_injection_configure(const mcpwm_injection_config_t *config);

// Apply injection pulse to specific cylinder
bool mcpwm_injection_apply(uint8_t cylinder_id, uint32_t pulsewidth_us);

// Apply injection pulses to all cylinders (sequential)
bool mcpwm_injection_apply_sequential(const uint32_t pulsewidth_us[4]);

// Apply injection pulses to all cylinders (simultaneous)
bool mcpwm_injection_apply_simultaneous(const uint32_t pulsewidth_us[4]);

// Schedule a one-shot injection pulse at a specific delay
bool mcpwm_injection_schedule_one_shot(uint8_t cylinder_id, uint32_t delay_us, uint32_t pulsewidth_us);

// Stop injection for specific cylinder
bool mcpwm_injection_stop(uint8_t cylinder_id);

// Stop all injections
bool mcpwm_injection_stop_all(void);

// Get injection status for specific cylinder
bool mcpwm_injection_get_status(uint8_t cylinder_id, mcpwm_injector_channel_t *status);

// Get current injection configuration
const mcpwm_injection_config_t* mcpwm_injection_get_config(void);

// Test injector (pulse for testing)
bool mcpwm_injection_test(uint8_t cylinder_id, uint32_t duration_ms);

// Deinitialize MCPWM injection system
bool mcpwm_injection_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MCPWM_INJECTION_H
