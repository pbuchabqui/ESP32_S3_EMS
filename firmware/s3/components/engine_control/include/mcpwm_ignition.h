#ifndef MCPWM_IGNITION_H
#define MCPWM_IGNITION_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ignition status structure
typedef struct {
    bool is_active;
    float current_dwell_ms;
    gpio_num_t coil_pin;
} mcpwm_ignition_status_t;

// Initialize MCPWM-based ignition system
bool mcpwm_ignition_init(void);

// Start ignition for a specific cylinder
bool mcpwm_ignition_start_cylinder(uint8_t cylinder_id, uint16_t rpm, float advance_degrees, float battery_voltage);

// Schedule a one-shot ignition event (spark delay in microseconds)
bool mcpwm_ignition_schedule_one_shot(uint8_t cylinder_id, uint32_t spark_delay_us, uint16_t rpm, float battery_voltage);

// Stop ignition for a specific cylinder
bool mcpwm_ignition_stop_cylinder(uint8_t cylinder_id);

// Get ignition status for a cylinder
bool mcpwm_ignition_get_status(uint8_t cylinder_id, mcpwm_ignition_status_t *status);

// Deinitialize MCPWM-based ignition system
bool mcpwm_ignition_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MCPWM_IGNITION_H
