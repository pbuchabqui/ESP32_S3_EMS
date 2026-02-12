#include "../include/lp_core.h"
#include "../include/logger.h"
#include "../include/config_manager.h"
#include "../include/sensor_processing.h"
#include "../include/ledc_injection.h"
#include "esp_attr.h"
#include "esp_rom_crc.h"
#include <stddef.h>

static uint32_t calculate_prime_pulse_duration(uint32_t battery_voltage);
static uint16_t calculate_rpm(uint64_t tooth_period_us);

// Static variables
static lp_core_config_t g_lp_config = {0};
static lp_core_state_t g_lp_state = {0};
static lp_core_handover_data_t g_handover_data = {0};

#define LP_CORE_RTC_MAGIC 0x4C50434FU
#define LP_CORE_RTC_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    lp_core_handover_data_t data;
    uint32_t crc32;
} lp_core_rtc_data_t;

static RTC_DATA_ATTR lp_core_rtc_data_t g_lp_rtc_data;

static uint32_t lp_core_crc32(const void *data, size_t len) {
    return esp_rom_crc32_le(0, (const uint8_t *)data, (uint32_t)len);
}

static bool lp_core_rtc_valid(void) {
    if (g_lp_rtc_data.magic != LP_CORE_RTC_MAGIC || g_lp_rtc_data.version != LP_CORE_RTC_VERSION) {
        return false;
    }
    uint32_t crc = lp_core_crc32(&g_lp_rtc_data.data, sizeof(g_lp_rtc_data.data));
    return crc == g_lp_rtc_data.crc32;
}

static void lp_core_rtc_write(const lp_core_handover_data_t *data) {
    if (!data) {
        return;
    }
    g_lp_rtc_data.magic = LP_CORE_RTC_MAGIC;
    g_lp_rtc_data.version = LP_CORE_RTC_VERSION;
    g_lp_rtc_data.data = *data;
    g_lp_rtc_data.crc32 = lp_core_crc32(&g_lp_rtc_data.data, sizeof(g_lp_rtc_data.data));
}

static void lp_core_rtc_invalidate(void) {
    g_lp_rtc_data.magic = 0;
    g_lp_rtc_data.version = 0;
    g_lp_rtc_data.crc32 = 0;
}

// Function implementations

esp_err_t lp_core_init(const lp_core_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize state
    g_lp_state.is_cranking = false;
    g_lp_state.is_prime_pulse_active = false;
    g_lp_state.is_sync_acquired = false;
    g_lp_state.is_handover_complete = false;
    g_lp_state.current_rpm = 0;
    g_lp_state.prime_pulse_counter = 0;
    g_lp_state.cranking_start_time = 0;
    g_lp_state.last_sync_time = 0;
    g_lp_state.tooth_counter = 0;
    g_lp_state.gap_detected = 0;
    g_lp_state.phase_detected = 0;

    // Create mutex
    g_lp_state.state_mutex = xSemaphoreCreateMutex();
    if (g_lp_state.state_mutex == NULL) {
        return ESP_FAIL;
    }

    // Copy configuration
    g_lp_config = *config;

    // Initialize RTC memory for state persistence
    esp_err_t err = esp_sleep_enable_timer_wakeup(1000000); // 1 second
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI("LP_CORE", "LP Core initialized");
    return ESP_OK;
}

esp_err_t lp_core_start_cranking(void) {
    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        if (g_lp_state.is_cranking) {
            xSemaphoreGive(g_lp_state.state_mutex);
            return ESP_ERR_INVALID_STATE;
        }

        // Reset state
        g_lp_state.is_cranking = true;
        g_lp_state.is_prime_pulse_active = false;
        g_lp_state.is_sync_acquired = false;
        g_lp_state.is_handover_complete = false;
        g_lp_state.current_rpm = 0;
        g_lp_state.prime_pulse_counter = 0;
        g_lp_state.cranking_start_time = esp_timer_get_time() / 1000;
        g_lp_state.last_sync_time = 0;
        g_lp_state.tooth_counter = 0;
        g_lp_state.gap_detected = 0;
        g_lp_state.phase_detected = 0;

        // Deliver prime pulses
        for (uint32_t i = 0; i < g_lp_config.prime_pulse_count; i++) {
            // Calculate prime pulse duration
            uint32_t pw_us = calculate_prime_pulse_duration(g_lp_config.prime_pulse_voltage);

            // Apply prime pulse to all injectors
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pw_us);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, pw_us);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, pw_us);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, pw_us);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);

            // Wait between pulses
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        g_lp_state.is_prime_pulse_active = true;
        xSemaphoreGive(g_lp_state.state_mutex);

        ESP_LOGI("LP_CORE", "Cranking started with %u prime pulses", g_lp_config.prime_pulse_count);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t lp_core_stop_cranking(void) {
    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        if (!g_lp_state.is_cranking) {
            xSemaphoreGive(g_lp_state.state_mutex);
            return ESP_ERR_INVALID_STATE;
        }

        // Stop all injectors
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);

        g_lp_state.is_cranking = false;
        g_lp_state.is_prime_pulse_active = false;
        g_lp_state.is_sync_acquired = false;
        g_lp_state.is_handover_complete = false;
        g_lp_state.current_rpm = 0;
        g_lp_state.prime_pulse_counter = 0;
        g_lp_state.cranking_start_time = 0;
        g_lp_state.last_sync_time = 0;
        g_lp_state.tooth_counter = 0;
        g_lp_state.gap_detected = 0;
        g_lp_state.phase_detected = 0;

        lp_core_rtc_invalidate();

        xSemaphoreGive(g_lp_state.state_mutex);

        ESP_LOGI("LP_CORE", "Cranking stopped");
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t lp_core_handle_sync_event(uint32_t tooth_period_us, bool is_gap, bool is_phase) {
    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        if (!g_lp_state.is_cranking) {
            xSemaphoreGive(g_lp_state.state_mutex);
            return ESP_ERR_INVALID_STATE;
        }

        // Update RPM
        g_lp_state.current_rpm = calculate_rpm(tooth_period_us);

        // Update sync status
        g_lp_state.last_sync_time = esp_timer_get_time() / 1000;
        g_lp_state.tooth_counter++;

        if (is_gap) {
            g_lp_state.gap_detected = 1;
            g_lp_state.tooth_counter = 0; // Reset counter at gap
        }

        if (is_phase) {
            g_lp_state.phase_detected = 1;
        }

        // Check if sync is acquired
        if (g_lp_state.tooth_counter >= 10 && g_lp_state.gap_detected && g_lp_state.phase_detected) {
            g_lp_state.is_sync_acquired = true;
            ESP_LOGI("LP_CORE", "Sync acquired at %u RPM", g_lp_state.current_rpm);
        }

        xSemaphoreGive(g_lp_state.state_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t lp_core_get_state(lp_core_state_t *state) {
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        *state = g_lp_state;
        xSemaphoreGive(g_lp_state.state_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t lp_core_get_handover_data(lp_core_handover_data_t *handover_data) {
    if (!handover_data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        if (g_lp_state.is_handover_complete) {
            *handover_data = g_handover_data;
        } else if (lp_core_rtc_valid()) {
            *handover_data = g_lp_rtc_data.data;
        } else {
            handover_data->handover_rpm = g_lp_state.current_rpm;
            handover_data->handover_timing_advance = g_lp_config.cranking_timing_advance;
            handover_data->handover_fuel_enrichment = g_lp_config.cranking_fuel_enrichment;
            handover_data->handover_sync_status = g_lp_state.is_sync_acquired;
            handover_data->handover_tooth_counter = g_lp_state.tooth_counter;
            handover_data->handover_gap_detected = g_lp_state.gap_detected;
            handover_data->handover_phase_detected = g_lp_state.phase_detected;
        }

        xSemaphoreGive(g_lp_state.state_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t lp_core_perform_handover(void) {
    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        if (!g_lp_state.is_cranking || !g_lp_state.is_sync_acquired) {
            xSemaphoreGive(g_lp_state.state_mutex);
            return ESP_ERR_INVALID_STATE;
        }

        // Prepare handover data
        g_handover_data.handover_rpm = g_lp_state.current_rpm;
        g_handover_data.handover_timing_advance = g_lp_config.cranking_timing_advance;
        g_handover_data.handover_fuel_enrichment = g_lp_config.cranking_fuel_enrichment;
        g_handover_data.handover_sync_status = g_lp_state.is_sync_acquired;
        g_handover_data.handover_tooth_counter = g_lp_state.tooth_counter;
        g_handover_data.handover_gap_detected = g_lp_state.gap_detected;
        g_handover_data.handover_phase_detected = g_lp_state.phase_detected;

        lp_core_rtc_write(&g_handover_data);

        // Stop LP Core operations
        g_lp_state.is_cranking = false;
        g_lp_state.is_handover_complete = true;

        xSemaphoreGive(g_lp_state.state_mutex);

        ESP_LOGI("LP_CORE", "Handover performed at %u RPM", g_handover_data.handover_rpm);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t lp_core_check_cranking_timeout(void) {
    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        if (!g_lp_state.is_cranking) {
            xSemaphoreGive(g_lp_state.state_mutex);
            return ESP_ERR_INVALID_STATE;
        }

        uint32_t current_time = esp_timer_get_time() / 1000;
        if (current_time - g_lp_state.cranking_start_time > g_lp_config.cranking_timeout_ms) {
            ESP_LOGE("LP_CORE", "Cranking timeout! Duration: %u ms", current_time - g_lp_state.cranking_start_time);
            xSemaphoreGive(g_lp_state.state_mutex);
            return ESP_FAIL;
        }

        xSemaphoreGive(g_lp_state.state_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t lp_core_reset_state(void) {
    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        g_lp_state.is_cranking = false;
        g_lp_state.is_prime_pulse_active = false;
        g_lp_state.is_sync_acquired = false;
        g_lp_state.is_handover_complete = false;
        g_lp_state.current_rpm = 0;
        g_lp_state.prime_pulse_counter = 0;
        g_lp_state.cranking_start_time = 0;
        g_lp_state.last_sync_time = 0;
        g_lp_state.tooth_counter = 0;
        g_lp_state.gap_detected = 0;
        g_lp_state.phase_detected = 0;

        lp_core_rtc_invalidate();

        xSemaphoreGive(g_lp_state.state_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t lp_core_deinit(void) {
    if (xSemaphoreTake(g_lp_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        // Delete mutex
        SemaphoreHandle_t mutex = g_lp_state.state_mutex;
        g_lp_state.state_mutex = NULL;
        vSemaphoreDelete(mutex);

        // Disable sleep wakeup
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

        lp_core_rtc_invalidate();

        return ESP_OK;
    }

    return ESP_FAIL;
}

// Helper functions
static uint32_t calculate_prime_pulse_duration(uint32_t battery_voltage) {
    // Simple calculation based on battery voltage
    // Adjust based on actual injector characteristics
    return 2000 + (battery_voltage * 10); // 2ms base + voltage compensation
}

static uint16_t calculate_rpm(uint64_t tooth_period_us) {
    if (tooth_period_us == 0) return 0;

    // RPM = 60.000.000 / (period_us * teeth_per_revolution)
    // For 60-2 wheel: teeth_per_revolution = 60
    uint32_t rpm = 60000000UL / (tooth_period_us * 60);

    if (rpm > 15000) rpm = 0; // Invalid value
    return (uint16_t)rpm;
}
