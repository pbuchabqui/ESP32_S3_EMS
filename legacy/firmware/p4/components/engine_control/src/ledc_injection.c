#include "ledc_injection.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "LEDC_INJECTION";

// Injection channels configuration
static injector_channel_t injectors[4] = {
    {LEDC_CHANNEL_0, GPIO_NUM_20, 0, false},
    {LEDC_CHANNEL_1, GPIO_NUM_21, 0, false},
    {LEDC_CHANNEL_2, GPIO_NUM_22, 0, false},
    {LEDC_CHANNEL_3, GPIO_NUM_23, 0, false},
};

// Injection system configuration
static injection_config_t injection_config = {
    .base_frequency_hz = 1000000,  // 1MHz for 1us resolution
    .timer_resolution_bits = 20,   // 20-bit resolution
    .min_pulsewidth_us = 500,      // Minimum pulse width
    .max_pulsewidth_us = 18000,    // Maximum pulse width
    .deadtime_us = 200,            // Dead time between injections
};

// Initialize LEDC-based injection system
bool ledc_injection_init(void) {
    ESP_LOGI(TAG, "Initializing LEDC-based injection system");

    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = injection_config.timer_resolution_bits,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = injection_config.base_frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer");
        return false;
    }

    // Configure LEDC channels for each injector
    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t ledc_channel = {
            .gpio_num = injectors[i].gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = injectors[i].channel,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,  // Initial duty = 0 (injector closed)
            .hpoint = 0,
        };

        if (ledc_channel_config(&ledc_channel) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure LEDC channel %d", i);
            return false;
        }

        injectors[i].is_active = false;
    }

    ESP_LOGI(TAG, "LEDC injection system initialized successfully");
    return true;
}

// Configure injection parameters
bool ledc_injection_configure(const injection_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration pointer");
        return false;
    }

    // Update configuration
    injection_config = *config;

    // Reconfigure timer if frequency changed
    if (injection_config.base_frequency_hz != 1000000) {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = injection_config.timer_resolution_bits,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = injection_config.base_frequency_hz,
            .clk_cfg = LEDC_AUTO_CLK,
        };

        if (ledc_timer_config(&ledc_timer) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reconfigure LEDC timer");
            return false;
        }
    }

    ESP_LOGI(TAG, "Injection configuration updated");
    return true;
}

// Apply injection pulse to specific cylinder
bool ledc_injection_apply(uint8_t cylinder_id, uint32_t pulsewidth_us) {
    if (cylinder_id >= 4) {
        ESP_LOGE(TAG, "Invalid cylinder ID: %d", cylinder_id);
        return false;
    }

    // Clamp pulse width to valid range
    if (pulsewidth_us < injection_config.min_pulsewidth_us) {
        pulsewidth_us = injection_config.min_pulsewidth_us;
    } else if (pulsewidth_us > injection_config.max_pulsewidth_us) {
        pulsewidth_us = injection_config.max_pulsewidth_us;
    }

    // Calculate duty cycle (with 1MHz base frequency, duty = pulsewidth in us)
    uint32_t duty = pulsewidth_us;

    if (ledc_set_duty(LEDC_LOW_SPEED_MODE, injectors[cylinder_id].channel, duty) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty for cylinder %d", cylinder_id);
        return false;
    }

    if (ledc_update_duty(LEDC_LOW_SPEED_MODE, injectors[cylinder_id].channel) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty for cylinder %d", cylinder_id);
        return false;
    }

    injectors[cylinder_id].pulsewidth_us = pulsewidth_us;
    injectors[cylinder_id].is_active = true;

    ESP_LOGV(TAG, "Applied injection to cylinder %d: %d us", cylinder_id, pulsewidth_us);
    return true;
}

// Apply injection pulses to all cylinders (sequential)
bool ledc_injection_apply_sequential(const uint32_t pulsewidth_us[4]) {
    for (int i = 0; i < 4; i++) {
        if (!ledc_injection_apply(i, pulsewidth_us[i])) {
            ESP_LOGE(TAG, "Failed to apply sequential injection to cylinder %d", i);
            return false;
        }

        // Add dead time between injections
        if (i < 3) {
            esp_rom_delay_us(injection_config.deadtime_us);
        }
    }

    ESP_LOGI(TAG, "Applied sequential injection to all cylinders");
    return true;
}

// Apply injection pulses to all cylinders (simultaneous)
bool ledc_injection_apply_simultaneous(const uint32_t pulsewidth_us[4]) {
    for (int i = 0; i < 4; i++) {
        if (!ledc_injection_apply(i, pulsewidth_us[i])) {
            ESP_LOGE(TAG, "Failed to apply simultaneous injection to cylinder %d", i);
            return false;
        }
    }

    ESP_LOGI(TAG, "Applied simultaneous injection to all cylinders");
    return true;
}

// Stop injection for specific cylinder
bool ledc_injection_stop(uint8_t cylinder_id) {
    if (cylinder_id >= 4) {
        ESP_LOGE(TAG, "Invalid cylinder ID: %d", cylinder_id);
        return false;
    }

    if (ledc_set_duty(LEDC_LOW_SPEED_MODE, injectors[cylinder_id].channel, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop injection for cylinder %d", cylinder_id);
        return false;
    }

    if (ledc_update_duty(LEDC_LOW_SPEED_MODE, injectors[cylinder_id].channel) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty for cylinder %d", cylinder_id);
        return false;
    }

    injectors[cylinder_id].pulsewidth_us = 0;
    injectors[cylinder_id].is_active = false;

    ESP_LOGV(TAG, "Stopped injection for cylinder %d", cylinder_id);
    return true;
}

// Stop all injections
bool ledc_injection_stop_all(void) {
    for (int i = 0; i < 4; i++) {
        if (!ledc_injection_stop(i)) {
            ESP_LOGE(TAG, "Failed to stop injection for cylinder %d", i);
            return false;
        }
    }

    ESP_LOGI(TAG, "Stopped all injections");
    return true;
}

// Get injection status for specific cylinder
bool ledc_injection_get_status(uint8_t cylinder_id, injector_channel_t *status) {
    if (cylinder_id >= 4 || status == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    *status = injectors[cylinder_id];
    return true;
}

// Get current injection configuration
const injection_config_t* ledc_injection_get_config(void) {
    return &injection_config;
}

// Test injector (pulse for testing)
bool ledc_injection_test(uint8_t cylinder_id, uint32_t duration_ms) {
    if (cylinder_id >= 4) {
        ESP_LOGE(TAG, "Invalid cylinder ID: %d", cylinder_id);
        return false;
    }

    // Apply test pulse (1ms for testing)
    if (!ledc_injection_apply(cylinder_id, 1000)) {
        ESP_LOGE(TAG, "Failed to apply test pulse to cylinder %d", cylinder_id);
        return false;
    }

    // Wait for duration
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    // Stop injection
    if (!ledc_injection_stop(cylinder_id)) {
        ESP_LOGE(TAG, "Failed to stop test pulse for cylinder %d", cylinder_id);
        return false;
    }

    ESP_LOGI(TAG, "Test pulse applied to cylinder %d for %d ms", cylinder_id, duration_ms);
    return true;
}

// Deinitialize LEDC injection system
bool ledc_injection_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing LEDC injection system");

    // Stop all injections
    if (!ledc_injection_stop_all()) {
        ESP_LOGE(TAG, "Failed to stop all injections during deinitialization");
    }

    // LEDC deinitialization is handled automatically
    ESP_LOGI(TAG, "LEDC injection system deinitialized");
    return true;
}
