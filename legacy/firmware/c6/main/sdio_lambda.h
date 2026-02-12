#ifndef SDIO_LAMBDA_H
#define SDIO_LAMBDA_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sdio_lambda_init(void);
void sdio_lambda_deinit(void);
void sdio_lambda_publish(float lambda_value);
void sdio_lambda_set_closed_loop_enabled(bool enabled);
bool sdio_lambda_get_closed_loop_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // SDIO_LAMBDA_H
