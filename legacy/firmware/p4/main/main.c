#include "engine_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "P4_MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "Starting ECU P4 Pro-Spec Engine Control");
    
    // Initialize engine control system
    engine_control_init();
    
    ESP_LOGI(TAG, "Engine control system initialized successfully");
    
    // Main loop - just keep the system running
    while (1) {
        // System is controlled by FreeRTOS tasks
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Log system status periodically
        engine_params_t params = {0};
        if (engine_control_get_engine_parameters(&params) == ESP_OK) {
            ESP_LOGI(TAG, "System running - RPM: %u, Load: %u kPa, Limp: %s",
                     params.rpm,
                     params.load / 10,
                     params.is_limp_mode ? "YES" : "NO");
        } else {
            ESP_LOGW(TAG, "System running - engine parameters unavailable");
        }
    }
}
