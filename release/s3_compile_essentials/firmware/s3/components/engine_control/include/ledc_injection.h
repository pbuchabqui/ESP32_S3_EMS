#ifndef LEDC_INJECTION_H
#define LEDC_INJECTION_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/ledc.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Injection channel configuration
typedef struct {
    ledc_channel_t channel;
    gpio_num_t gpio;
    uint32_t pulsewidth_us;  // Pulse width in microseconds
    bool is_active;
} injector_channel_t;

// Injection system configuration
typedef struct {
    uint32_t base_frequency_hz;  // Base frequency (1MHz for 1us resolution)
    uint32_t timer_resolution_bits;  // Timer resolution (20-bit for 1us)
    uint32_t min_pulsewidth_us;  // Minimum pulse width
    uint32_t max_pulsewidth_us;  // Maximum pulse width
    uint32_t deadtime_us;  // Dead time between injections
} injection_config_t;

// Initialize LEDC-based injection system
bool ledc_injection_init(void);

// Configure injection parameters
bool ledc_injection_configure(const injection_config_t *config);

// Apply injection pulse to specific cylinder
bool ledc_injection_apply(uint8_t cylinder_id, uint32_t pulsewidth_us);

// Apply injection pulses to all cylinders (sequential)
bool ledc_injection_apply_sequential(const uint32_t pulsewidth_us[4]);

// Apply injection pulses to all cylinders (simultaneous)
bool ledc_injection_apply_simultaneous(const uint32_t pulsewidth_us[4]);

// Stop injection for specific cylinder
bool ledc_injection_stop(uint8_t cylinder_id);

// Stop all injections
bool ledc_injection_stop_all(void);

// Get injection status for specific cylinder
bool ledc_injection_get_status(uint8_t cylinder_id, injector_channel_t *status);

// Get current injection configuration
const injection_config_t* ledc_injection_get_config(void);

// Test injector (pulse for testing)
bool ledc_injection_test(uint8_t cylinder_id, uint32_t duration_ms);

// Deinitialize LEDC injection system
bool ledc_injection_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // LEDC_INJECTION_H