#include "../include/ignition_timing.h"
#include "../include/logger.h"
#include "../include/mcpwm_ignition.h"
#include "../include/mcpwm_injection.h"
#include "../include/config_manager.h"
#include "../include/sync.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Static variables for ignition system
static ignition_config_t ignition_config = {
    .base_advance_deg10 = 150,    // 15.0 degrees base advance
    .max_advance_deg10 = 250,     // 25.0 degrees max advance
    .min_advance_deg10 = 50,      // 5.0 degrees min advance
    .knock_threshold = 100,       // Knock threshold
    .rpm_limit_rpm = 3000,        // RPM limit for advance
    .warmup_time_ms = 30000       // 30 seconds warmup time
};

static ignition_sync_t ignition_sync = {
    .tooth_period_us = 0,
    .rpm = 0,
    .current_tooth = 0,
    .sync_lost = true,
    .last_sync_time = 0
};

static const float g_cyl_tdc_deg[4] = {0.0f, 180.0f, 360.0f, 540.0f};

static float wrap_angle_720(float angle_deg) {
    while (angle_deg >= 720.0f) {
        angle_deg -= 720.0f;
    }
    while (angle_deg < 0.0f) {
        angle_deg += 720.0f;
    }
    return angle_deg;
}

static float compute_current_angle_deg(const sync_data_t *sync, uint32_t tooth_count) {
    float degrees_per_tooth = 360.0f / (float)(tooth_count + 2);
    float current_angle = (sync->revolution_index * 360.0f) + (sync->tooth_index * degrees_per_tooth);
    return wrap_angle_720(current_angle);
}

// Initialize ignition timing system
void ignition_init(void) {
    // Initialize MCPWM for precise ignition timing
    mcpwm_ignition_init();

    // Initialize MCPWM for injection control
    mcpwm_injection_init();

    // Reset synchronization
    ignition_reset_sync();

    // Log initialization
    LOG_IGNITION_I("Ignition timing system initialized");
}

// Calculate ignition timing advance
uint16_t ignition_calculate_advance(const sensor_data_t *sensors,
                                    uint16_t rpm,
                                    knock_protection_t *knock_protection,
                                    ignition_calc_t *calc) {
    if (!sensors || !calc || !knock_protection) {
        return 0;
    }

    // Calculate base advance based on RPM
    uint16_t base_advance = ignition_config.base_advance_deg10;

    if (rpm > ignition_config.rpm_limit_rpm) {
        // Reduce advance at high RPM
        base_advance = ignition_config.base_advance_deg10 - ((rpm - ignition_config.rpm_limit_rpm) / 100);
    }

    // Apply warmup retard
    uint32_t current_time = esp_timer_get_time() / 1000; // ms
    if (current_time < ignition_config.warmup_time_ms) {
        calc->warmup_retard_deg10 = (ignition_config.warmup_time_ms - current_time) / 100;
    } else {
        calc->warmup_retard_deg10 = 0;
    }

    // Apply knock retard
    if (knock_protection->knock_detected) {
        calc->knock_retard_deg10 = knock_protection->timing_retard;
    } else {
        calc->knock_retard_deg10 = 0;
    }

    // Calculate final advance
    int16_t final_advance = base_advance
                          - calc->warmup_retard_deg10
                          - calc->knock_retard_deg10
                          - calc->rpm_retard_deg10;

    // Clamp to min/max values
    if (final_advance < ignition_config.min_advance_deg10) {
        final_advance = ignition_config.min_advance_deg10;
    } else if (final_advance > ignition_config.max_advance_deg10) {
        final_advance = ignition_config.max_advance_deg10;
    }

    calc->final_advance_deg10 = final_advance;
    calc->base_advance_deg10 = base_advance;

    return final_advance;
}

// Apply ignition timing
void ignition_apply_timing(uint16_t advance_deg10, uint16_t rpm) {
    float advance_degrees = advance_deg10 / 10.0f;
    float battery_voltage = 13.5f; // Default battery voltage

    sync_data_t sync_data = {0};
    sync_config_t sync_cfg = {0};
    bool have_sync = (sync_get_data(&sync_data) == ESP_OK) &&
                     (sync_get_config(&sync_cfg) == ESP_OK) &&
                     sync_data.sync_valid &&
                     sync_data.sync_acquired &&
                     (sync_cfg.tooth_count > 0);

    if (have_sync) {
        float current_angle = compute_current_angle_deg(&sync_data, sync_cfg.tooth_count);
        for (uint8_t cylinder = 1; cylinder <= 4; cylinder++) {
            float tdc_deg = g_cyl_tdc_deg[cylinder - 1];
            float spark_deg = wrap_angle_720(tdc_deg - advance_degrees);
            float delta_deg = spark_deg - current_angle;
            if (delta_deg < 0.0f) {
                delta_deg += 720.0f;
            }
            uint32_t delay_us = (uint32_t)(delta_deg * sync_data.time_per_degree);
            mcpwm_ignition_schedule_one_shot(cylinder, delay_us, rpm, battery_voltage);
        }
        LOG_IGNITION_D("Scheduled ignition (sync): %u deg10, %u RPM", advance_deg10, rpm);
        return;
    }

    for (uint8_t cylinder = 1; cylinder <= 4; cylinder++) {
        mcpwm_ignition_start_cylinder(cylinder, rpm, advance_degrees, battery_voltage);
    }
    LOG_IGNITION_D("Applied ignition timing (fallback): %u deg10, %u RPM", advance_deg10, rpm);
}

// Get current ignition configuration
const ignition_config_t* ignition_get_config(void) {
    return &ignition_config;
}

// Set ignition configuration
void ignition_set_config(const ignition_config_t *config) {
    if (config) {
        ignition_config = *config;
        LOG_IGNITION_I("Ignition configuration updated");
    }
}

// Reset ignition configuration to defaults
void ignition_reset_config(void) {
    ignition_config.base_advance_deg10 = 150;
    ignition_config.max_advance_deg10 = 250;
    ignition_config.min_advance_deg10 = 50;
    ignition_config.knock_threshold = 100;
    ignition_config.rpm_limit_rpm = 3000;
    ignition_config.warmup_time_ms = 30000;
    LOG_IGNITION_I("Ignition configuration reset to defaults");
}

// Get ignition synchronization status
const ignition_sync_t* ignition_get_sync_status(void) {
    return &ignition_sync;
}

// Reset ignition synchronization
void ignition_reset_sync(void) {
    ignition_sync.tooth_period_us = 0;
    ignition_sync.rpm = 0;
    ignition_sync.current_tooth = 0;
    ignition_sync.sync_lost = true;
    ignition_sync.last_sync_time = esp_timer_get_time();
    LOG_IGNITION_I("Ignition synchronization reset");
}

// Check for ignition safety conditions
bool ignition_check_safety(uint16_t rpm, uint16_t advance_deg10) {
    // Check RPM limits
    if (rpm < 500 || rpm > 8000) {
        return false;
    }

    // Check advance limits
    if (advance_deg10 < ignition_config.min_advance_deg10 || advance_deg10 > ignition_config.max_advance_deg10) {
        return false;
    }

    // Check synchronization
    if (ignition_sync.sync_lost) {
        return false;
    }

    return true;
}

// Test ignition coil
void ignition_test_coil(uint8_t coil_id, uint32_t duration_ms) {
    if (coil_id >= 4) {
        return;
    }

    // Activate coil for testing
    mcpwm_ignition_start_cylinder(coil_id + 1, 1000, 15.0f, 13.5f);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    mcpwm_ignition_stop_cylinder(coil_id + 1);

    LOG_IGNITION_I("Ignition coil %u tested for %u ms", coil_id, duration_ms);
}

// Get ignition timing status
bool ignition_get_status(uint8_t coil_id) {
    if (coil_id >= 4) {
        return false;
    }

    mcpwm_ignition_status_t status;
    return mcpwm_ignition_get_status(coil_id + 1, &status);
}

// Handle knock detection and retard
void ignition_handle_knock(knock_protection_t *knock_protection, bool detected) {
    if (!knock_protection) {
        return;
    }

    if (detected) {
        // Apply knock retard
        knock_protection->knock_detected = true;
        knock_protection->timing_retard = 50; // 5.0 degrees retard

        // Log knock event
        LOG_IGNITION_W("Knock detected! Applying 5.0 degrees retard");
    } else {
        // Reset knock retard
        knock_protection->knock_detected = false;
        knock_protection->timing_retard = 0;
    }
}

// Helper function to calculate timing from advance
static uint32_t calculate_timing_from_advance(uint16_t advance_deg10, uint16_t rpm) {
    // Calculate degrees per cycle (4-stroke engine: 720 degrees per cycle)
    uint32_t degrees_per_cycle = 720;

    // Calculate time per degree
    uint32_t time_per_degree_us = (60 * 1000000) / (rpm * degrees_per_cycle);

    // Calculate timing based on advance
    uint32_t timing_us = (degrees_per_cycle - advance_deg10 / 10) * time_per_degree_us;

    return timing_us;
}
