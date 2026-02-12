#ifndef IDF_CONFIG_H
#define IDF_CONFIG_H

// ESP32-S3 target configuration
#define ESP32_S3_TARGET "esp32s3"
#define ESP32_S3_REV_MIN 0

// Build profile
#define COMPILER_OPTIMIZATION_LEVEL "release"

// Runtime defaults
#define CAN_BITRATE 500000
#define LOG_DEFAULT_LEVEL 3
#define SERIAL_BAUD_RATE 115200

#endif // IDF_CONFIG_H