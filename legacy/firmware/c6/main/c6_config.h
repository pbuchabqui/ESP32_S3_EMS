#ifndef C6_CONFIG_H
#define C6_CONFIG_H

#include "driver/gpio.h"

// TODO: adjust CAN pins to match the C6 board wiring.
#define C6_CAN_TX_GPIO GPIO_NUM_4
#define C6_CAN_RX_GPIO GPIO_NUM_5

#define C6_CAN_SPEED 500000

#endif // C6_CONFIG_H
