#include "sdio_lambda.h"
#include "driver/sdio_slave.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#define SDIO_REG_STATUS 0x00
#define SDIO_REG_LAMBDA_L 0x01
#define SDIO_REG_LAMBDA_H 0x02
#define SDIO_REG_TS0 0x03
#define SDIO_REG_TS1 0x04
#define SDIO_REG_TS2 0x05
#define SDIO_REG_TS3 0x06
#define SDIO_STATUS_VALID 0x01

#define SDIO_RX_BUFFER_SIZE 128
#define SDIO_RX_BUFFER_COUNT 4

static const char *TAG = "SDIO_LAMBDA";
static bool g_sdio_ready = false;
static uint8_t *g_rx_buffers[SDIO_RX_BUFFER_COUNT] = {0};
static sdio_slave_buf_handle_t g_rx_handles[SDIO_RX_BUFFER_COUNT] = {0};
static uint8_t g_status = 0;
static bool g_closed_loop_enabled = true;

esp_err_t sdio_lambda_init(void) {
    if (g_sdio_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    sdio_slave_config_t config = {
        .timing = SDIO_SLAVE_TIMING_NSEND_PSAMPLE,
        .sending_mode = SDIO_SLAVE_SEND_PACKET,
        .send_queue_size = 4,
        .recv_buffer_size = SDIO_RX_BUFFER_SIZE,
        .event_cb = NULL,
        .flags = 0,
    };

    esp_err_t err = sdio_slave_initialize(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdio_slave_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < SDIO_RX_BUFFER_COUNT; i++) {
        g_rx_buffers[i] = (uint8_t *)heap_caps_malloc(SDIO_RX_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!g_rx_buffers[i]) {
            ESP_LOGE(TAG, "RX buffer alloc failed");
            return ESP_ERR_NO_MEM;
        }
        g_rx_handles[i] = sdio_slave_recv_register_buf(g_rx_buffers[i]);
        if (!g_rx_handles[i]) {
            ESP_LOGE(TAG, "RX buffer register failed");
            return ESP_FAIL;
        }
        sdio_slave_recv_load_buf(g_rx_handles[i]);
    }

    sdio_slave_write_reg(SDIO_REG_STATUS, 0);
    g_status = 0;
    sdio_slave_start();
    g_sdio_ready = true;
    ESP_LOGI(TAG, "SDIO slave ready");
    return ESP_OK;
}

void sdio_lambda_deinit(void) {
    if (!g_sdio_ready) {
        return;
    }
    sdio_slave_stop();
    sdio_slave_reset();
    sdio_slave_deinit();
    for (int i = 0; i < SDIO_RX_BUFFER_COUNT; i++) {
        if (g_rx_handles[i]) {
            sdio_slave_recv_unregister_buf(g_rx_handles[i]);
            g_rx_handles[i] = NULL;
        }
        if (g_rx_buffers[i]) {
            free(g_rx_buffers[i]);
            g_rx_buffers[i] = NULL;
        }
    }
    g_sdio_ready = false;
}

void sdio_lambda_publish(float lambda_value) {
    if (!g_sdio_ready) {
        return;
    }

    if (lambda_value < 0.7f) lambda_value = 0.7f;
    if (lambda_value > 1.3f) lambda_value = 1.3f;

    uint16_t lambda_x1000 = (uint16_t)(lambda_value * 1000.0f + 0.5f);
    uint32_t ts_ms = (uint32_t)(esp_timer_get_time() / 1000);

    sdio_slave_write_reg(SDIO_REG_LAMBDA_L, (uint8_t)(lambda_x1000 & 0xFF));
    sdio_slave_write_reg(SDIO_REG_LAMBDA_H, (uint8_t)((lambda_x1000 >> 8) & 0xFF));
    sdio_slave_write_reg(SDIO_REG_TS0, (uint8_t)(ts_ms & 0xFF));
    sdio_slave_write_reg(SDIO_REG_TS1, (uint8_t)((ts_ms >> 8) & 0xFF));
    sdio_slave_write_reg(SDIO_REG_TS2, (uint8_t)((ts_ms >> 16) & 0xFF));
    sdio_slave_write_reg(SDIO_REG_TS3, (uint8_t)((ts_ms >> 24) & 0xFF));
    if (g_closed_loop_enabled) {
        g_status |= SDIO_STATUS_CLOSED_LOOP;
    } else {
        g_status &= (uint8_t)~SDIO_STATUS_CLOSED_LOOP;
    }
    g_status |= SDIO_STATUS_VALID;
    sdio_slave_write_reg(SDIO_REG_STATUS, g_status);
    sdio_slave_send_host_int(0);
}

void sdio_lambda_set_closed_loop_enabled(bool enabled) {
    g_closed_loop_enabled = enabled;
    if (!g_sdio_ready) {
        return;
    }
    if (g_closed_loop_enabled) {
        g_status |= SDIO_STATUS_CLOSED_LOOP;
    } else {
        g_status &= (uint8_t)~SDIO_STATUS_CLOSED_LOOP;
    }
    sdio_slave_write_reg(SDIO_REG_STATUS, g_status);
    sdio_slave_send_host_int(0);
}

bool sdio_lambda_get_closed_loop_enabled(void) {
    return g_closed_loop_enabled;
}
