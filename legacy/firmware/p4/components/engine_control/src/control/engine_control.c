#include "../include/engine_control.h"
#include "../include/lp_core.h"
#include "../include/lp_core_config.h"
#include "../include/logger.h"
#include "../include/sensor_processing.h"
#include "../include/sync.h"
#include "../include/fuel_calc.h"
#include "../include/table_16x16.h"
#include "../include/lambda_pid.h"
#include "../include/p4_control_config.h"
#include "../include/fuel_injection.h"
#include "../include/ignition_timing.h"
#include "../include/config_manager.h"
#include "../include/map_storage.h"
#include "../include/safety_monitor.h"
#include "../include/sdio_link.h"
#include "../include/mcpwm_injection.h"
#include "../include/mcpwm_ignition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_crc.h"
#include <math.h>

// Static variables
static lp_core_config_t g_lp_core_config = {0};
static lp_core_state_t g_lp_core_state = {0};
static fuel_calc_maps_t g_maps = {0};
static lambda_pid_t g_lambda_pid = {0};
static bool g_engine_math_ready = false;
static float g_target_eoi_deg = 360.0f;
static float g_target_eoi_deg_fallback = 360.0f;
static TaskHandle_t g_engine_task_handle = NULL;
static float g_stft = 0.0f;
static float g_ltft = 0.0f;
static uint16_t g_last_rpm = 0;
static uint16_t g_last_load = 0;
static uint32_t g_stable_start_ms = 0;
static uint32_t g_last_map_save_ms = 0;
static bool g_map_dirty = false;
static bool g_closed_loop_enabled = true;

#define CLOSED_LOOP_CONFIG_KEY "closed_loop_cfg"
#define CLOSED_LOOP_CONFIG_VERSION 1U

#define STFT_LIMIT 0.25f
#define LTFT_LIMIT 0.20f
#define LTFT_ALPHA 0.01f
#define LTFT_STABLE_MS 500U
#define LTFT_RPM_DELTA_MAX 50U
#define LTFT_LOAD_DELTA_MAX 50U
#define LTFT_APPLY_THRESHOLD 0.03f
#define MAP_SAVE_INTERVAL_MS 5000U

#define EOI_CONFIG_KEY "eoi_config"
#define EOI_CONFIG_VERSION 1U

typedef struct {
    uint32_t version;
    float eoi_deg;
    float eoi_fallback_deg;
    uint32_t crc32;
} eoi_config_blob_t;

typedef struct {
    uint32_t version;
    uint8_t enabled;
    uint8_t reserved[3];
    uint32_t crc32;
} closed_loop_config_blob_t;

static uint32_t eoi_config_crc(const eoi_config_blob_t *cfg) {
    return esp_rom_crc32_le(0, (const uint8_t *)&cfg->eoi_deg, (uint32_t)(sizeof(float) * 2));
}

static void eoi_config_apply(const eoi_config_blob_t *cfg) {
    if (!cfg) {
        return;
    }
    g_target_eoi_deg = cfg->eoi_deg;
    g_target_eoi_deg_fallback = cfg->eoi_fallback_deg;
}

static void eoi_config_defaults(eoi_config_blob_t *cfg) {
    if (!cfg) {
        return;
    }
    cfg->version = EOI_CONFIG_VERSION;
    cfg->eoi_deg = 360.0f;
    cfg->eoi_fallback_deg = 360.0f;
    cfg->crc32 = eoi_config_crc(cfg);
}

static float wrap_angle_360(float angle_deg) {
    while (angle_deg >= 360.0f) {
        angle_deg -= 360.0f;
    }
    while (angle_deg < 0.0f) {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

static float compute_current_angle_360(const sync_data_t *sync, uint32_t tooth_count) {
    float degrees_per_tooth = 360.0f / (float)(tooth_count + 2);
    float current_angle = sync->tooth_index * degrees_per_tooth;
    return wrap_angle_360(current_angle);
}

static float clamp_float(float v, float min_v, float max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static uint32_t closed_loop_config_crc(const closed_loop_config_blob_t *cfg) {
    return esp_rom_crc32_le(0, (const uint8_t *)&cfg->enabled, (uint32_t)sizeof(cfg->enabled) + sizeof(cfg->reserved));
}

static void closed_loop_config_defaults(closed_loop_config_blob_t *cfg) {
    if (!cfg) {
        return;
    }
    cfg->version = CLOSED_LOOP_CONFIG_VERSION;
    cfg->enabled = 1U;
    cfg->reserved[0] = 0;
    cfg->reserved[1] = 0;
    cfg->reserved[2] = 0;
    cfg->crc32 = closed_loop_config_crc(cfg);
}

static void refresh_closed_loop_from_sdio(void) {
    bool remote_enabled = g_closed_loop_enabled;
    if (sdio_get_closed_loop_enabled(&remote_enabled) && remote_enabled != g_closed_loop_enabled) {
        engine_control_set_closed_loop_enabled(remote_enabled);
    }
}

static uint8_t find_bin_index(const uint16_t *bins, uint16_t value) {
    for (uint8_t i = 0; i < 15; i++) {
        if (value < bins[i + 1]) {
            return i;
        }
    }
    return 14;
}

static void apply_ltft_to_fuel_table(uint16_t rpm, uint16_t load) {
    table_16x16_t *table = &g_maps.fuel_table;
    uint8_t x = find_bin_index(table->rpm_bins, rpm);
    uint8_t y = find_bin_index(table->load_bins, load);

    float current = (float)table->values[y][x];
    float updated = current * (1.0f + g_ltft);
    if (updated < 0.0f) updated = 0.0f;
    if (updated > 65535.0f) updated = 65535.0f;
    table->values[y][x] = (uint16_t)(updated + 0.5f);
    table->checksum = table_16x16_checksum(table);
    g_map_dirty = true;
}

static void maybe_persist_maps(uint32_t now_ms) {
    if (!g_map_dirty) {
        return;
    }
    if ((now_ms - g_last_map_save_ms) < MAP_SAVE_INTERVAL_MS) {
        return;
    }
    if (map_storage_save(&g_maps) == ESP_OK) {
        g_last_map_save_ms = now_ms;
        g_map_dirty = false;
    }
}

static bool ltft_can_update(uint16_t rpm, uint16_t load, uint32_t now_ms) {
    uint16_t drpm = (rpm > g_last_rpm) ? (rpm - g_last_rpm) : (g_last_rpm - rpm);
    uint16_t dload = (load > g_last_load) ? (load - g_last_load) : (g_last_load - load);

    g_last_rpm = rpm;
    g_last_load = load;

    if (drpm <= LTFT_RPM_DELTA_MAX && dload <= LTFT_LOAD_DELTA_MAX) {
        if (g_stable_start_ms == 0) {
            g_stable_start_ms = now_ms;
        }
        return (now_ms - g_stable_start_ms) >= LTFT_STABLE_MS;
    }

    g_stable_start_ms = 0;
    return false;
}

static void engine_control_task(void *arg) {
    (void)arg;
    while (1) {
        if (engine_control_run_cycle() != ESP_OK) {
            ESP_LOGW("ENGINE_CONTROL", "Control cycle failed");
        }
        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_INTERVAL));
    }
}

static void schedule_semi_seq_injection(uint16_t rpm,
                                        uint16_t load,
                                        uint32_t pw_us,
                                        const sync_data_t *sync,
                                        float eoi_base_deg) {
    sync_config_t sync_cfg = {0};
    if (sync_get_config(&sync_cfg) != ESP_OK || sync_cfg.tooth_count == 0) {
        return;
    }

    float current_angle = compute_current_angle_360(sync, sync_cfg.tooth_count);
    float tpd = sync->time_per_degree;
    if (tpd <= 0.0f) {
        return;
    }

    float pw_deg = pw_us / tpd;
    // Pair 1: cylinders 1 & 4 at 0°
    float eoi0 = wrap_angle_360(eoi_base_deg);
    float soi0 = wrap_angle_360(eoi0 + pw_deg);
    float delta0 = soi0 - current_angle;
    if (delta0 < 0.0f) delta0 += 360.0f;
    uint32_t delay0 = (uint32_t)(delta0 * tpd);
    mcpwm_injection_schedule_one_shot(0, delay0, pw_us);
    mcpwm_injection_schedule_one_shot(3, delay0, pw_us);

    // Pair 2: cylinders 2 & 3 at 180°
    float eoi180 = wrap_angle_360(eoi_base_deg + 180.0f);
    float soi180 = wrap_angle_360(eoi180 + pw_deg);
    float delta180 = soi180 - current_angle;
    if (delta180 < 0.0f) delta180 += 360.0f;
    uint32_t delay180 = (uint32_t)(delta180 * tpd);
    mcpwm_injection_schedule_one_shot(1, delay180, pw_us);
    mcpwm_injection_schedule_one_shot(2, delay180, pw_us);
}

static void schedule_wasted_spark(uint16_t advance_deg10, uint16_t rpm, const sync_data_t *sync) {
    sync_config_t sync_cfg = {0};
    if (sync_get_config(&sync_cfg) != ESP_OK || sync_cfg.tooth_count == 0) {
        return;
    }

    float current_angle = compute_current_angle_360(sync, sync_cfg.tooth_count);
    float tpd = sync->time_per_degree;
    if (tpd <= 0.0f) {
        return;
    }

    float advance_deg = advance_deg10 / 10.0f;
    float spark0 = wrap_angle_360(0.0f - advance_deg);
    float delta0 = spark0 - current_angle;
    if (delta0 < 0.0f) delta0 += 360.0f;
    uint32_t delay0 = (uint32_t)(delta0 * tpd);
    mcpwm_ignition_schedule_one_shot(1, delay0, rpm, 13.5f);
    mcpwm_ignition_schedule_one_shot(4, delay0, rpm, 13.5f);

    float spark180 = wrap_angle_360(180.0f - advance_deg);
    float delta180 = spark180 - current_angle;
    if (delta180 < 0.0f) delta180 += 360.0f;
    uint32_t delay180 = (uint32_t)(delta180 * tpd);
    mcpwm_ignition_schedule_one_shot(2, delay180, rpm, 13.5f);
    mcpwm_ignition_schedule_one_shot(3, delay180, rpm, 13.5f);
}

// Initialize engine control system
esp_err_t engine_control_init(void) {
    ESP_LOGI("ENGINE_CONTROL", "Initializing engine control system");

    // Load LP Core configuration
    esp_err_t err = lp_core_load_config(&g_lp_core_config);
    if (err != ESP_OK) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to load LP Core config");
        return err;
    }

    err = config_manager_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to initialize config manager");
        return err;
    }

    // Initialize LP Core
    err = lp_core_init(&g_lp_core_config);
    if (err != ESP_OK) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to initialize LP Core");
        return err;
    }

    if (map_storage_load(&g_maps) != ESP_OK) {
        fuel_calc_init_defaults(&g_maps);
        map_storage_save(&g_maps);
    }
    lambda_pid_init(&g_lambda_pid, 0.6f, 0.08f, 0.01f, -0.25f, 0.25f);
    g_engine_math_ready = true;

    closed_loop_config_blob_t cl_cfg = {0};
    if (config_manager_load(CLOSED_LOOP_CONFIG_KEY, &cl_cfg, sizeof(cl_cfg)) != ESP_OK ||
        cl_cfg.version != CLOSED_LOOP_CONFIG_VERSION ||
        cl_cfg.crc32 != closed_loop_config_crc(&cl_cfg)) {
        closed_loop_config_defaults(&cl_cfg);
        config_manager_save(CLOSED_LOOP_CONFIG_KEY, &cl_cfg, sizeof(cl_cfg));
    }
    g_closed_loop_enabled = (cl_cfg.enabled != 0);

    // Initialize sensor, sync, SDIO link, injection, and ignition subsystems
    err = sensor_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to init sensors");
        return err;
    }
    err = sensor_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to start sensors");
        return err;
    }

    err = sync_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to init sync");
        return err;
    }
    err = sync_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to start sync");
        return err;
    }

    err = sdio_link_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to init SDIO link");
        return err;
    }

    fuel_injection_init(NULL);
    ignition_init();
    safety_monitor_init();
    safety_watchdog_init(1000);

    eoi_config_blob_t eoi_cfg = {0};
    if (config_manager_load(EOI_CONFIG_KEY, &eoi_cfg, sizeof(eoi_cfg)) != ESP_OK ||
        eoi_cfg.version != EOI_CONFIG_VERSION ||
        eoi_cfg.crc32 != eoi_config_crc(&eoi_cfg)) {
        eoi_config_defaults(&eoi_cfg);
        config_manager_save(EOI_CONFIG_KEY, &eoi_cfg, sizeof(eoi_cfg));
    }
    eoi_config_apply(&eoi_cfg);

    if (g_engine_task_handle == NULL) {
        BaseType_t task_ok = xTaskCreate(engine_control_task, "engine_ctrl",
                                         4096, NULL, 6, &g_engine_task_handle);
        if (task_ok != pdPASS) {
            ESP_LOGE("ENGINE_CONTROL", "Failed to create control task");
            return ESP_FAIL;
        }
    }

    ESP_LOGI("ENGINE_CONTROL", "Engine control system initialized");
    return ESP_OK;
}

// Start engine control
esp_err_t engine_control_start(void) {
    ESP_LOGI("ENGINE_CONTROL", "Starting engine control");

    // Start LP Core cranking
    esp_err_t err = lp_core_start_cranking();
    if (err != ESP_OK) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to start LP Core cranking");
        return err;
    }

    ESP_LOGI("ENGINE_CONTROL", "Engine control started");
    return ESP_OK;
}

// Stop engine control
esp_err_t engine_control_stop(void) {
    ESP_LOGI("ENGINE_CONTROL", "Stopping engine control");

    // Stop LP Core
    esp_err_t err = lp_core_stop_cranking();
    if (err != ESP_OK) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to stop LP Core");
        return err;
    }

    ESP_LOGI("ENGINE_CONTROL", "Engine control stopped");
    return ESP_OK;
}

// Handle sync events
esp_err_t engine_control_handle_sync_event(uint32_t tooth_period_us, bool is_gap, bool is_phase) {
    return lp_core_handle_sync_event(tooth_period_us, is_gap, is_phase);
}

// Get LP Core state
esp_err_t engine_control_get_lp_core_state(lp_core_state_t *state) {
    return lp_core_get_state(state);
}

// Perform handover
esp_err_t engine_control_perform_handover(void) {
    return lp_core_perform_handover();
}

// Deinitialize engine control
esp_err_t engine_control_deinit(void) {
    ESP_LOGI("ENGINE_CONTROL", "Deinitializing engine control system");

    if (g_engine_task_handle != NULL) {
        vTaskDelete(g_engine_task_handle);
        g_engine_task_handle = NULL;
    }

    sensor_stop();
    sensor_deinit();
    sync_stop();
    sync_deinit();
    sdio_link_deinit();
    // Deinitialize LP Core
    esp_err_t err = lp_core_deinit();
    if (err != ESP_OK) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to deinitialize LP Core");
        return err;
    }

    config_manager_deinit();

    ESP_LOGI("ENGINE_CONTROL", "Engine control system deinitialized");
    return ESP_OK;
}

// Get engine RPM
uint32_t engine_control_get_rpm(void) {
    lp_core_state_t state = {0};
    if (lp_core_get_state(&state) == ESP_OK) {
        return state.current_rpm;
    }
    return 0;
}

// Get engine load
uint32_t engine_control_get_load(void) {
    // This would need to be implemented based on actual sensor readings
    return 0;
}

// Get sensor data
esp_err_t engine_control_get_sensor_data(sensor_data_t *data) {
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor_data_t sensor_data = {0};
    if (sensor_get_data(&sensor_data) != ESP_OK) {
        return ESP_FAIL;
    }

    *data = sensor_data;
    return ESP_OK;
}

// Get engine parameters
esp_err_t engine_control_get_engine_parameters(engine_params_t *params) {
    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }

    refresh_closed_loop_from_sdio();

    sensor_data_t sensor_data = {0};
    if (sensor_get_data(&sensor_data) != ESP_OK) {
        return ESP_FAIL;
    }

    sync_data_t sync_data = {0};
    if (sync_get_data(&sync_data) != ESP_OK) {
        return ESP_FAIL;
    }

    uint16_t rpm = (uint16_t)sync_data.rpm;
    uint16_t load = sensor_data.map_kpa10;
    uint16_t ve_x10 = fuel_calc_lookup_ve(&g_maps, rpm, load);
    uint16_t ign_deg10 = fuel_calc_lookup_ignition(&g_maps, rpm, load);
    uint16_t lambda_target = fuel_calc_lookup_lambda(&g_maps, rpm, load);

    float lambda_measured = 1.0f;
    bool lambda_valid = false;
    uint32_t lambda_age_ms = 0;
    if (sdio_get_latest_lambda(&lambda_measured, &lambda_age_ms) && lambda_age_ms < 200) {
        lambda_valid = true;
    } else if (sensor_data.o2_mv > 0) {
        lambda_measured = (sensor_data.o2_mv / 1000.0f) / 0.45f;
        lambda_valid = true;
    }
    lambda_measured = clamp_float(lambda_measured, 0.7f, 1.3f);

    float lambda_target_f = lambda_target / 1000.0f;
    float lambda_corr = 0.0f;
    if (g_engine_math_ready && lambda_valid) {
        if (!g_closed_loop_enabled) {
            lambda_corr = 0.0f;
            return ESP_OK;
        }
        float stft = lambda_pid_update(&g_lambda_pid, lambda_target_f, lambda_measured, 0.01f);
        g_stft = clamp_float(stft, -STFT_LIMIT, STFT_LIMIT);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (ltft_can_update(rpm, load, now_ms)) {
            g_ltft += LTFT_ALPHA * (g_stft - g_ltft);
            g_ltft = clamp_float(g_ltft, -LTFT_LIMIT, LTFT_LIMIT);
            if (fabsf(g_ltft) >= LTFT_APPLY_THRESHOLD) {
                apply_ltft_to_fuel_table(rpm, load);
                g_ltft = 0.0f;
            }
        }
        maybe_persist_maps(now_ms);
        lambda_corr = clamp_float(g_stft + g_ltft, -STFT_LIMIT, STFT_LIMIT);
    }

    uint32_t pw_us = fuel_calc_pulsewidth_us(&sensor_data, rpm, ve_x10, lambda_corr);

    params->rpm = rpm;
    params->load = load;
    params->advance_deg10 = ign_deg10;
    params->fuel_enrichment = (uint16_t)((pw_us * 100U) / (uint32_t)REQ_FUEL_US);
    params->is_limp_mode = engine_control_is_limp_mode();

    return ESP_OK;
}

esp_err_t engine_control_run_cycle(void) {
    engine_params_t params = {0};
    if (engine_control_get_engine_parameters(&params) != ESP_OK) {
        return ESP_FAIL;
    }

    sync_data_t sync_data = {0};
    if (sync_get_data(&sync_data) != ESP_OK || !sync_data.sync_valid) {
        return ESP_FAIL;
    }

    sensor_data_t sensor_data = {0};
    if (sensor_get_data(&sensor_data) != ESP_OK) {
        return ESP_FAIL;
    }

    if (safety_check_over_rev((uint16_t)params.rpm) ||
        safety_check_overheat(sensor_data.clt_c) ||
        safety_check_battery_voltage(sensor_data.vbat_dv)) {
        return ESP_FAIL;
    }

    uint16_t ve_x10 = fuel_calc_lookup_ve(&g_maps, (uint16_t)params.rpm, (uint16_t)params.load);
    float lambda_corr = 0.0f;
    if (g_engine_math_ready) {
        if (!g_closed_loop_enabled) {
            lambda_corr = 0.0f;
            goto compute_pw;
        }
        float lambda_target = fuel_calc_lookup_lambda(&g_maps, (uint16_t)params.rpm, (uint16_t)params.load) / 1000.0f;
        float lambda_measured = 1.0f;
        bool lambda_valid = false;
        uint32_t lambda_age_ms = 0;
        if (sdio_get_latest_lambda(&lambda_measured, &lambda_age_ms) && lambda_age_ms < 200) {
            lambda_valid = true;
        } else if (sensor_data.o2_mv > 0) {
            lambda_measured = (sensor_data.o2_mv / 1000.0f) / 0.45f;
            lambda_valid = true;
        }
        lambda_measured = clamp_float(lambda_measured, 0.7f, 1.3f);
        if (lambda_valid) {
            float stft = lambda_pid_update(&g_lambda_pid, lambda_target, lambda_measured, 0.01f);
            g_stft = clamp_float(stft, -STFT_LIMIT, STFT_LIMIT);
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (ltft_can_update((uint16_t)params.rpm, (uint16_t)params.load, now_ms)) {
                g_ltft += LTFT_ALPHA * (g_stft - g_ltft);
                g_ltft = clamp_float(g_ltft, -LTFT_LIMIT, LTFT_LIMIT);
                if (fabsf(g_ltft) >= LTFT_APPLY_THRESHOLD) {
                    apply_ltft_to_fuel_table((uint16_t)params.rpm, (uint16_t)params.load);
                    g_ltft = 0.0f;
                }
            }
            maybe_persist_maps(now_ms);
            lambda_corr = clamp_float(g_stft + g_ltft, -STFT_LIMIT, STFT_LIMIT);
        }
    }

compute_pw:
    uint32_t pw_us = fuel_calc_pulsewidth_us(&sensor_data, (uint16_t)params.rpm, ve_x10, lambda_corr);

    if (sync_data.sync_acquired) {
        for (uint8_t cyl = 1; cyl <= 4; cyl++) {
            fuel_injection_schedule_eoi(cyl, g_target_eoi_deg, pw_us, &sync_data);
        }
        ignition_apply_timing(params.advance_deg10, (uint16_t)params.rpm);
    } else {
        LOG_SAFETY_W("Sync partial: fallback to semi-sequential + wasted spark");
        schedule_semi_seq_injection((uint16_t)params.rpm, (uint16_t)params.load, pw_us, &sync_data, g_target_eoi_deg_fallback);
        schedule_wasted_spark(params.advance_deg10, (uint16_t)params.rpm, &sync_data);
    }
    safety_watchdog_feed();
    return ESP_OK;
}

esp_err_t engine_control_set_eoi_config(float eoi_deg, float eoi_fallback_deg) {
    eoi_config_blob_t cfg = {0};
    cfg.version = EOI_CONFIG_VERSION;
    cfg.eoi_deg = eoi_deg;
    cfg.eoi_fallback_deg = eoi_fallback_deg;
    cfg.crc32 = eoi_config_crc(&cfg);

    esp_err_t err = config_manager_save(EOI_CONFIG_KEY, &cfg, sizeof(cfg));
    if (err != ESP_OK) {
        return err;
    }
    eoi_config_apply(&cfg);
    return ESP_OK;
}

esp_err_t engine_control_get_eoi_config(float *eoi_deg, float *eoi_fallback_deg) {
    if (!eoi_deg || !eoi_fallback_deg) {
        return ESP_ERR_INVALID_ARG;
    }
    *eoi_deg = g_target_eoi_deg;
    *eoi_fallback_deg = g_target_eoi_deg_fallback;
    return ESP_OK;
}

// Get core synchronization data
esp_err_t engine_control_get_core_sync_data(core_sync_data_t *sync_data) {
    if (!sync_data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get sync data from LP Core
    if (xSemaphoreTake(g_lp_core_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        sync_data->heartbeat = esp_timer_get_time() / 1000;
        sync_data->last_sync_time = g_lp_core_state.last_sync_time;
        sync_data->error_count = 0; // Would need to be implemented
        sync_data->is_sync_acquired = g_lp_core_state.is_sync_acquired;
        sync_data->handover_rpm = g_lp_core_state.current_rpm;
        sync_data->handover_advance = 100; // Default value
        xSemaphoreGive(g_lp_core_state.state_mutex);
        return ESP_OK;
    }
    return ESP_FAIL;
}

// Update core synchronization data
esp_err_t engine_control_update_core_sync_data(const core_sync_data_t *sync_data) {
    if (!sync_data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Update sync data (would be used by HP cores to update LP core)
    // This would need to be implemented
    return ESP_OK;
}

// Check if in limp mode
bool engine_control_is_limp_mode(void) {
    return safety_is_limp_mode_active();
}

void engine_control_set_closed_loop_enabled(bool enabled) {
    if (g_closed_loop_enabled == enabled) {
        return;
    }
    g_closed_loop_enabled = enabled;

    closed_loop_config_blob_t cfg = {0};
    cfg.version = CLOSED_LOOP_CONFIG_VERSION;
    cfg.enabled = enabled ? 1U : 0U;
    cfg.reserved[0] = 0;
    cfg.reserved[1] = 0;
    cfg.reserved[2] = 0;
    cfg.crc32 = closed_loop_config_crc(&cfg);
    config_manager_save(CLOSED_LOOP_CONFIG_KEY, &cfg, sizeof(cfg));
}

bool engine_control_get_closed_loop_enabled(void) {
    return g_closed_loop_enabled;
}
