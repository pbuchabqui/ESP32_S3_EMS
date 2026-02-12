#include "../include/sdio_link.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define SDIO_FUNC_NUM 1
#define SDIO_REG_STATUS 0x00
#define SDIO_REG_LAMBDA_L 0x01
#define SDIO_REG_LAMBDA_H 0x02
#define SDIO_REG_TS0 0x03
#define SDIO_REG_TS1 0x04
#define SDIO_REG_TS2 0x05
#define SDIO_REG_TS3 0x06
#define SDIO_STATUS_VALID 0x01
#define SDIO_STATUS_CLOSED_LOOP 0x02

static const char *TAG = "SDIO_LINK";

static bool g_sdio_initialized = false;
static sdmmc_card_t g_card;
static TaskHandle_t g_sdio_task = NULL;
static float g_latest_lambda = 1.0f;
static uint32_t g_latest_lambda_timestamp_ms = 0;
static uint32_t g_last_remote_timestamp = 0;
static bool g_remote_closed_loop_enabled = true;

static void sdio_poll_task(void *arg);
static bool sdio_read_reg(uint32_t reg, uint8_t *out);
static bool sdio_read_u32(uint32_t reg, uint32_t *out);

esp_err_t sdio_link_init(void) {
    if (g_sdio_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init_slot failed: %s", esp_err_to_name(err));
        sdmmc_host_deinit();
        return err;
    }

    err = sdmmc_card_init(&host, &g_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init failed: %s", esp_err_to_name(err));
        sdmmc_host_deinit();
        return err;
    }

    g_sdio_initialized = true;
    g_latest_lambda = 1.0f;
    g_latest_lambda_timestamp_ms = 0;
    g_last_remote_timestamp = 0;
    g_remote_closed_loop_enabled = true;

    if (g_sdio_task == NULL) {
        BaseType_t ok = xTaskCreate(sdio_poll_task, "sdio_poll", 4096, NULL, 6, &g_sdio_task);
        if (ok != pdPASS) {
            g_sdio_task = NULL;
            g_sdio_initialized = false;
            sdmmc_host_deinit();
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t sdio_link_deinit(void) {
    if (!g_sdio_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_sdio_task != NULL) {
        vTaskDelete(g_sdio_task);
        g_sdio_task = NULL;
    }

    sdmmc_host_deinit();
    g_sdio_initialized = false;
    return ESP_OK;
}

bool sdio_get_latest_lambda(float *out_lambda, uint32_t *out_age_ms) {
    if (!g_sdio_initialized || !out_lambda) {
        return false;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (out_age_ms) {
        *out_age_ms = (g_latest_lambda_timestamp_ms == 0) ? 0 : (now_ms - g_latest_lambda_timestamp_ms);
    }
    if (g_latest_lambda_timestamp_ms == 0) {
        return false;
    }

    *out_lambda = g_latest_lambda;
    return true;
}

bool sdio_get_closed_loop_enabled(bool *out_enabled) {
    if (!g_sdio_initialized || !out_enabled) {
        return false;
    }
    *out_enabled = g_remote_closed_loop_enabled;
    return true;
}

static void sdio_poll_task(void *arg) {
    (void)arg;
    while (1) {
        uint8_t status = 0;
        if (!sdio_read_reg(SDIO_REG_STATUS, &status)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        g_remote_closed_loop_enabled = (status & SDIO_STATUS_CLOSED_LOOP) != 0;
        if (status & SDIO_STATUS_VALID) {
            uint8_t l = 0;
            uint8_t h = 0;
            uint32_t remote_ts = 0;
            if (sdio_read_reg(SDIO_REG_LAMBDA_L, &l) &&
                sdio_read_reg(SDIO_REG_LAMBDA_H, &h) &&
                sdio_read_u32(SDIO_REG_TS0, &remote_ts)) {
                if (remote_ts != g_last_remote_timestamp) {
                    g_last_remote_timestamp = remote_ts;
                    uint16_t lambda_x1000 = (uint16_t)((h << 8) | l);
                    g_latest_lambda = lambda_x1000 / 1000.0f;
                    g_latest_lambda_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool sdio_read_reg(uint32_t reg, uint8_t *out) {
    if (!out) {
        return false;
    }
    esp_err_t err = sdmmc_io_read_byte(&g_card, SDIO_FUNC_NUM, reg, out);
    return err == ESP_OK;
}

static bool sdio_read_u32(uint32_t reg, uint32_t *out) {
    if (!out) {
        return false;
    }
    uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    if (!sdio_read_reg(reg + 0, &b0) ||
        !sdio_read_reg(reg + 1, &b1) ||
        !sdio_read_reg(reg + 2, &b2) ||
        !sdio_read_reg(reg + 3, &b3)) {
        return false;
    }
    *out = (uint32_t)b0 |
           ((uint32_t)b1 << 8) |
           ((uint32_t)b2 << 16) |
           ((uint32_t)b3 << 24);
    return true;
}
