#include "../include/fuel_injection.h"
#include "../include/mcpwm_injection.h"
#include "../include/sync.h"

static fuel_injection_config_t g_fuel_cfg = {
    .cyl_tdc_deg = {0.0f, 180.0f, 360.0f, 540.0f},
};

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

void fuel_injection_init(const fuel_injection_config_t *config) {
    if (config) {
        g_fuel_cfg = *config;
    }
}

bool fuel_injection_schedule_eoi(uint8_t cylinder_id,
                                 float target_eoi_deg,
                                 uint32_t pulsewidth_us,
                                 const sync_data_t *sync) {
    if (!sync || cylinder_id < 1 || cylinder_id > 4) {
        return false;
    }

    sync_config_t sync_cfg = {0};
    if (sync_get_config(&sync_cfg) != ESP_OK || sync_cfg.tooth_count == 0) {
        return false;
    }
    if (sync->time_per_degree == 0) {
        return false;
    }

    float current_angle = compute_current_angle_deg(sync, sync_cfg.tooth_count);
    float eoi_deg = wrap_angle_720(target_eoi_deg + g_fuel_cfg.cyl_tdc_deg[cylinder_id - 1]);
    float pw_deg = (pulsewidth_us / (float)sync->time_per_degree);
    float soi_deg = wrap_angle_720(eoi_deg + pw_deg);

    float delta_deg = soi_deg - current_angle;
    if (delta_deg < 0.0f) {
        delta_deg += 720.0f;
    }

    uint32_t delay_us = (uint32_t)(delta_deg * sync->time_per_degree);
    return mcpwm_injection_schedule_one_shot((uint8_t)(cylinder_id - 1), delay_us, pulsewidth_us);
}
