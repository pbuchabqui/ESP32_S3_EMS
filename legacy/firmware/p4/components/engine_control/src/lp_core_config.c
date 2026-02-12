#include "../include/lp_core_config.h"
#include "../include/config_manager.h"
#include "../include/logger.h"
#include "esp_log.h"

// Function implementations

esp_err_t lp_core_load_config(lp_core_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Load configuration from NVS or defaults
    esp_err_t err = config_manager_load("lp_core_config", config, sizeof(lp_core_config_t));
    if (err != ESP_OK) {
        // Load failed, use default config
        lp_core_set_default_config(config);
        ESP_LOGW("LP_CORE_CONFIG", "Failed to load LP Core config, using defaults");
    }

    return ESP_OK;
}

esp_err_t lp_core_save_config(const lp_core_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Save configuration to NVS
    esp_err_t err = config_manager_save("lp_core_config", config, sizeof(lp_core_config_t));
    if (err != ESP_OK) {
        ESP_LOGE("LP_CORE_CONFIG", "Failed to save LP Core config");
        return err;
    }

    return ESP_OK;
}

esp_err_t lp_core_set_default_config(lp_core_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set default configuration values
    config->cranking_rpm_threshold = LP_CORE_DEFAULT_CRANKING_RPM_THRESHOLD;
    config->prime_pulse_duration_us = LP_CORE_DEFAULT_PRIME_PULSE_DURATION_US;
    config->prime_pulse_voltage = LP_CORE_DEFAULT_PRIME_PULSE_VOLTAGE;
    config->cranking_fuel_enrichment = LP_CORE_DEFAULT_CRANKING_FUEL_ENRICHMENT;
    config->cranking_timing_advance = LP_CORE_DEFAULT_CRANKING_TIMING_ADVANCE;
    config->cranking_rpm_limit = LP_CORE_DEFAULT_CRANKING_RPM_LIMIT;
    config->cranking_timeout_ms = LP_CORE_DEFAULT_CRANKING_TIMEOUT_MS;
    config->sync_timeout_ms = LP_CORE_DEFAULT_SYNC_TIMEOUT_MS;
    config->prime_pulse_count = LP_CORE_DEFAULT_PRIME_PULSE_COUNT;

    return ESP_OK;
}
