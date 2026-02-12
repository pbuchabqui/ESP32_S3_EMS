#include "lambda_can.h"
#include "c6_config.h"
#include "sdio_lambda.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum {
    PROTOCOL_UNKNOWN = 0,
    PROTOCOL_FUELTEC_NANO_V1,
    PROTOCOL_FUELTEC_NANO_V2,
    PROTOCOL_GENERIC_WIDEBAND,
    PROTOCOL_MAX
} protocol_type_t;

typedef struct {
    uint32_t can_id;
    uint8_t data_length;
    uint8_t afr_offset;
    uint8_t status_offset;
} lambda_protocol_t;

static const char *TAG = "LAMBDA_CAN";

static const lambda_protocol_t g_protocols[PROTOCOL_MAX] = {
    [PROTOCOL_UNKNOWN] = {0},
    [PROTOCOL_FUELTEC_NANO_V1] = { .can_id = 0x7E8, .data_length = 3, .afr_offset = 0, .status_offset = 2 },
    [PROTOCOL_FUELTEC_NANO_V2] = { .can_id = 0x7E9, .data_length = 4, .afr_offset = 0, .status_offset = 2 },
    [PROTOCOL_GENERIC_WIDEBAND] = { .can_id = 0x7E0, .data_length = 3, .afr_offset = 0, .status_offset = 2 },
};

static TaskHandle_t g_can_task = NULL;
static bool g_can_running = false;
static bool g_can_initialized = false;

static void can_rx_task(void *arg);
static protocol_type_t detect_protocol(const twai_message_t *msg);

esp_err_t lambda_can_init(void) {
    if (g_can_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(C6_CAN_TX_GPIO, C6_CAN_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
        return err;
    }
    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return err;
    }

    g_can_running = true;
    BaseType_t ok = xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 6, &g_can_task);
    if (ok != pdPASS) {
        g_can_running = false;
        twai_stop();
        twai_driver_uninstall();
        return ESP_FAIL;
    }

    g_can_initialized = true;
    ESP_LOGI(TAG, "CAN lambda RX started");
    return ESP_OK;
}

void lambda_can_deinit(void) {
    if (!g_can_initialized) {
        return;
    }
    g_can_running = false;
    if (g_can_task) {
        vTaskDelete(g_can_task);
        g_can_task = NULL;
    }
    twai_stop();
    twai_driver_uninstall();
    g_can_initialized = false;
}

static void can_rx_task(void *arg) {
    (void)arg;
    while (g_can_running) {
        twai_message_t msg = {0};
        if (twai_receive(&msg, pdMS_TO_TICKS(100)) != ESP_OK) {
            continue;
        }

        protocol_type_t proto = detect_protocol(&msg);
        if (proto == PROTOCOL_UNKNOWN) {
            continue;
        }

        const lambda_protocol_t *p = &g_protocols[proto];
        if (msg.data_length_code < p->data_length) {
            continue;
        }

        uint16_t afr_raw = ((uint16_t)msg.data[p->afr_offset] << 8) |
                           (uint16_t)msg.data[p->afr_offset + 1];
        uint8_t status = msg.data[p->status_offset];
        bool valid = (status & 0x01) != 0;
        if (!valid) {
            continue;
        }

        float lambda = (afr_raw / 14.7f);
        sdio_lambda_publish(lambda);
    }
    vTaskDelete(NULL);
}

static protocol_type_t detect_protocol(const twai_message_t *msg) {
    if (!msg) {
        return PROTOCOL_UNKNOWN;
    }
    for (int i = 1; i < PROTOCOL_MAX; i++) {
        if (msg->identifier == g_protocols[i].can_id &&
            msg->data_length_code == g_protocols[i].data_length) {
            return (protocol_type_t)i;
        }
    }
    return PROTOCOL_UNKNOWN;
}
