#include "../include/ignition_timing.h"
#include "../include/logger.h"
#include "../include/mcpwm_ignition.h"
#include "../include/mcpwm_injection.h"
#include "../include/sensor_processing.h"
#include "../include/sync.h"

static const float g_cyl_tdc_deg[4] = {0.0f, 180.0f, 360.0f, 540.0f};

static float clampf_local(float v, float min_v, float max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static float apply_temp_dwell_bias(float battery_voltage, int16_t clt_c) {
    if (clt_c >= 105) {
        battery_voltage += 1.0f;
    } else if (clt_c >= 95) {
        battery_voltage += 0.5f;
    } else if (clt_c <= 0) {
        battery_voltage -= 0.7f;
    } else if (clt_c <= 20) {
        battery_voltage -= 0.4f;
    }
    return clampf_local(battery_voltage, 8.0f, 16.5f);
}

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

static float sync_us_per_degree(const sync_data_t *sync, const sync_config_t *cfg) {
    if (!sync || !cfg || sync->tooth_period == 0U || cfg->tooth_count == 0U) {
        return 0.0f;
    }
    uint32_t total_positions = cfg->tooth_count + 2U;
    return ((float)sync->tooth_period * (float)total_positions) / 360.0f;
}

bool ignition_init(void) {
    bool ign_ok = mcpwm_ignition_init();
    bool inj_ok = mcpwm_injection_init();
    if (ign_ok && inj_ok) {
        LOG_IGNITION_I("Ignition timing system initialized");
        return true;
    }

    LOG_IGNITION_E("Ignition timing init failed (ign=%d, inj=%d)", ign_ok, inj_ok);
    return false;
}

void ignition_apply_timing(uint16_t advance_deg10, uint16_t rpm) {
    float advance_degrees = advance_deg10 / 10.0f;
    float battery_voltage = 13.5f;

    sensor_data_t sensors = {0};
    if (sensor_get_data_fast(&sensors) == ESP_OK) {
        if (sensors.vbat_dv > 0) {
            battery_voltage = sensors.vbat_dv / 10.0f;
        }
        battery_voltage = apply_temp_dwell_bias(battery_voltage, sensors.clt_c);
    } else {
        battery_voltage = clampf_local(battery_voltage, 8.0f, 16.5f);
    }

    sync_data_t sync_data = {0};
    sync_config_t sync_cfg = {0};
    bool have_sync = (sync_get_data(&sync_data) == ESP_OK) &&
                     (sync_get_config(&sync_cfg) == ESP_OK) &&
                     sync_data.sync_valid &&
                     sync_data.sync_acquired &&
                     (sync_cfg.tooth_count > 0);
    float us_per_deg = 0.0f;

    if (have_sync) {
        us_per_deg = sync_us_per_degree(&sync_data, &sync_cfg);
        if (us_per_deg <= 0.0f) {
            have_sync = false;
        }
    }

    if (have_sync) {
        float current_angle = compute_current_angle_deg(&sync_data, sync_cfg.tooth_count);
        for (uint8_t cylinder = 1; cylinder <= 4; cylinder++) {
            float spark_deg = wrap_angle_720(g_cyl_tdc_deg[cylinder - 1] - advance_degrees);
            float delta_deg = spark_deg - current_angle;
            if (delta_deg < 0.0f) {
                delta_deg += 720.0f;
            }
            uint32_t delay_us = (uint32_t)((delta_deg * us_per_deg) + 0.5f);
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
