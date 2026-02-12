#ifndef S3_CONTROL_CONFIG_H
#define S3_CONTROL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"

// Engine configuration
#define ENGINE_TYPE "4-cylinder"
#define MAX_RPM 8000
#define IDLE_RPM 800
#define FUEL_CUTOFF_RPM 7500

// Sensor configuration
#define MAP_SENSOR_MIN 0.0f
#define MAP_SENSOR_MAX 250.0f
#define CLT_SENSOR_MIN -40.0f
#define CLT_SENSOR_MAX 120.0f
#define TPS_SENSOR_MIN 0.0f
#define TPS_SENSOR_MAX 100.0f
#define IAT_SENSOR_MIN -40.0f
#define IAT_SENSOR_MAX 120.0f
#define O2_SENSOR_MIN 0.0f
#define O2_SENSOR_MAX 5.0f
#define VBAT_SENSOR_MIN 7.0f
#define VBAT_SENSOR_MAX 17.0f

// Injection configuration
#define INJECTOR_FLOW_RATE 420.0f
#define INJECTOR_PULSE_WIDTH_MIN 500
#define INJECTOR_PULSE_WIDTH_MAX 20000

// Ignition configuration
#define IGNITION_ADVANCE_BASE 10
#define MAX_IGNITION_ADVANCE 35
#define MIN_IGNITION_ADVANCE -5

// Timing configuration
#define RPM_UPDATE_INTERVAL 100
#define SENSOR_READ_INTERVAL 10
#define CONTROL_LOOP_INTERVAL 1
#define HEARTBEAT_INTERVAL 100
#define LIMP_TIMEOUT 2000

// Task priorities (FreeRTOS)
#define CONTROL_TASK_PRIORITY 10
#define SENSOR_TASK_PRIORITY 9
#define COMM_TASK_PRIORITY 8
#define MONITOR_TASK_PRIORITY 7

// Task stack sizes
#define CONTROL_TASK_STACK 4096
#define SENSOR_TASK_STACK 4096
#define COMM_TASK_STACK 4096
#define MONITOR_TASK_STACK 3072

// Core affinity (-1 means no pinning)
#define CONTROL_TASK_CORE 1
#define SENSOR_TASK_CORE 0
#define COMM_TASK_CORE 0
#define MONITOR_TASK_CORE 0

// Interpolation cache tuning (steady-state reuse window)
#define INTERP_CACHE_RPM_DEADBAND 50
#define INTERP_CACHE_LOAD_DEADBAND 20

// 16x16 map structure
#pragma pack(push, 1)
typedef struct {
    uint16_t rpm_bins[16];
    uint16_t load_bins[16];
    uint16_t values[16][16];
    uint16_t checksum;
} table_16x16_t;
#pragma pack(pop)

// Default bins
static const uint16_t DEFAULT_RPM_BINS[16] = {
    500, 800, 1200, 1600, 2000, 2500, 3000, 3500,
    4000, 4500, 5000, 5500, 6000, 6500, 7000, 8000
};

static const uint16_t DEFAULT_LOAD_BINS[16] = {
    200, 300, 400, 500, 600, 650, 700, 750,
    800, 850, 900, 950, 1000, 1020, 1050, 1100
};

// Scale constants
#define FIXED_POINT_SCALE 10
#define LAMBDA_SCALE 1000
#define MAP_FILTER_ALPHA 3

// Calculation constants
#define REQ_FUEL_US 7730
#define IAT_REF_K10 2931
#define WARMUP_TEMP_MAX 70
#define WARMUP_TEMP_MIN 0
#define WARMUP_ENRICH_MAX 140
#define TPS_DOT_THRESHOLD 5
#define TPS_DOT_ENRICH_MAX 150
#define PW_MAX_US 18000
#define PW_MIN_US 500
#define RPM_MAX_SAFE 12000

// Clamp helpers
#define CLAMP(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// GPIO configuration for ESP32-S3
#define CKP_GPIO GPIO_NUM_34
#define CMP_GPIO GPIO_NUM_35

// TWAI configuration
#define CAN_SPEED 500000
#define CAN_RX_QUEUE_SIZE 10
#define CAN_TX_GPIO GPIO_NUM_4
#define CAN_RX_GPIO GPIO_NUM_5

// Injector GPIOs
#define INJECTOR_GPIO_1 GPIO_NUM_12
#define INJECTOR_GPIO_2 GPIO_NUM_13
#define INJECTOR_GPIO_3 GPIO_NUM_15
#define INJECTOR_GPIO_4 GPIO_NUM_2

// Ignition GPIOs
#define IGNITION_GPIO_1 GPIO_NUM_16
#define IGNITION_GPIO_2 GPIO_NUM_17
#define IGNITION_GPIO_3 GPIO_NUM_18
#define IGNITION_GPIO_4 GPIO_NUM_21

// System configuration
#define DEBUG_MODE true
#define SERIAL_BAUD_RATE 115200

#endif // S3_CONTROL_CONFIG_H
