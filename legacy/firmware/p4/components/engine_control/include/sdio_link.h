#ifndef SDIO_LINK_H
#define SDIO_LINK_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sdio_link_init(void);
esp_err_t sdio_link_deinit(void);
bool sdio_get_latest_lambda(float *out_lambda, uint32_t *out_age_ms);
bool sdio_get_closed_loop_enabled(bool *out_enabled);

#ifdef __cplusplus
}
#endif

#endif // SDIO_LINK_H
