#ifndef LAMBDA_CAN_H
#define LAMBDA_CAN_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lambda_can_init(void);
void lambda_can_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_CAN_H
