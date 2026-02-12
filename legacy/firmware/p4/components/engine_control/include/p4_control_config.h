#ifndef P4_CONTROL_CONFIG_H
#define P4_CONTROL_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

// Engine configuration
#define ENGINE_TYPE "4-cylinder"
#define MAX_RPM 8000
#define IDLE_RPM 800
#define FUEL_CUTOFF_RPM 7500

// Sensor configuration
#define MAP_SENSOR_MIN 0.0    // kPa
#define MAP_SENSOR_MAX 250.0  // kPa
#define CLT_SENSOR_MIN -40.0  // °C
#define CLT_SENSOR_MAX 120.0  // °C
#define TPS_SENSOR_MIN 0.0    // %
#define TPS_SENSOR_MAX 100.0  // %
#define IAT_SENSOR_MIN -40.0  // °C
#define IAT_SENSOR_MAX 120.0  // °C
#define O2_SENSOR_MIN 0.0     // V
#define O2_SENSOR_MAX 5.0     // V
#define VBAT_SENSOR_MIN 7.0   // V
#define VBAT_SENSOR_MAX 17.0  // V

// Injection configuration
#define INJECTOR_FLOW_RATE 420.0  // cc/min
#define INJECTOR_PULSE_WIDTH_MIN 500  // microseconds
#define INJECTOR_PULSE_WIDTH_MAX 20000  // microseconds

// Ignition configuration
#define IGNITION_ADVANCE_BASE 10  // degrees
#define MAX_IGNITION_ADVANCE 35   // degrees
#define MIN_IGNITION_ADVANCE -5   // degrees

// Timing configuration
#define RPM_UPDATE_INTERVAL 100   // milliseconds
#define SENSOR_READ_INTERVAL 10    // milliseconds
#define CONTROL_LOOP_INTERVAL 1    // milliseconds
#define HEARTBEAT_INTERVAL 100     // milliseconds
#define LIMP_TIMEOUT 2000          // milliseconds

/* ==========================================================================
   ESTRUTURA DE TABELAS 16x16
   ========================================================================== */

/**
 * @brief Tabela 2D 16x16 para mapas de VE, Ignição, Lambda Target
 */
#pragma pack(push, 1)
typedef struct {
    uint16_t rpm_bins[16];
    uint16_t load_bins[16];
    uint16_t values[16][16];
    uint16_t checksum;
} table_16x16_t;
#pragma pack(pop)

/* ==========================================================================
   BINS PADRÃO PARA TABELAS 16x16
   ========================================================================== */

// Bins de RPM padrão (500-8000 RPM)
static const uint16_t DEFAULT_RPM_BINS[16] = {
    500, 800, 1200, 1600, 2000, 2500, 3000, 3500,
    4000, 4500, 5000, 5500, 6000, 6500, 7000, 8000
};

// Bins de carga padrão (MAP kPa * 10)
static const uint16_t DEFAULT_LOAD_BINS[16] = {
    200, 300, 400, 500, 600, 650, 700, 750,
    800, 850, 900, 950, 1000, 1020, 1050, 1100
};

/* ==========================================================================
   CONSTANTES DE ESCALA
   ========================================================================== */
#define FIXED_POINT_SCALE       10      // Escala para graus e VE (x10)
#define LAMBDA_SCALE            1000    // Escala para Lambda (x1000)
#define MAP_FILTER_ALPHA        3       // Shift para filtro EMA do MAP

/* ==========================================================================
   CONSTANTES DE CÁLCULO
   ========================================================================== */
#define REQ_FUEL_US             7730    // Pulse width base (microsegundos)
#define IAT_REF_K10             2931    // Temperatura de referência (20°C * 10)
#define WARMUP_TEMP_MAX         70      // Temperatura máxima para warmup (°C)
#define WARMUP_TEMP_MIN         0       // Temperatura mínima para warmup (°C)
#define WARMUP_ENRICH_MAX       140     // Enriquecimento máximo em partida fria (%)
#define TPS_DOT_THRESHOLD       5       // Limiar de variação de TPS para enriquecimento
#define TPS_DOT_ENRICH_MAX      150     // Enriquecimento máximo de aceleração (%)
#define PW_MAX_US               18000   // Pulse width máximo (microsegundos)
#define PW_MIN_US               500     // Pulse width mínimo (microsegundos)
#define RPM_MAX_SAFE            12000   // RPM máximo seguro para cálculo

/* ==========================================================================
   VALIDAÇÃO DE LIMITES
   ========================================================================== */
#define CLAMP(val, min, max)    ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))

// GPIO configuration
#define CKP_GPIO GPIO_NUM_34
#define CMP_GPIO GPIO_NUM_35

// System configuration
#define DEBUG_MODE true
#define SERIAL_BAUD_RATE 115200

#endif // P4_CONTROL_CONFIG_H
