#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lambda_can.h"
#include "sdio_lambda.h"

static const char* TAG = "C6_MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "Starting ECU P4 Pro-Spec C6 firmware");

    if (sdio_lambda_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SDIO slave");
    }
    if (lambda_can_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init CAN lambda RX");
    }
    
    // Main loop - just keep the system running
    while (1) {
        // System is controlled by FreeRTOS tasks
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Log system status periodically
        ESP_LOGI(TAG, "C6 firmware running");
    }
}
