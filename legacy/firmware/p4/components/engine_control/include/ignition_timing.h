#ifndef IGNITION_TIMING_H
#define IGNITION_TIMING_H

#include <stdint.h>
#include <stdbool.h>

#include "sensor_processing.h"
#include "safety_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

// Estrutura de cálculo de ignição
typedef struct {
    uint16_t base_advance_deg10;   // Avanço base em 0.1 graus
    int16_t knock_retard_deg10;    // Retardo por knock em 0.1 graus
    int16_t warmup_retard_deg10;   // Retardo por aquecimento em 0.1 graus
    int16_t rpm_retard_deg10;      // Retardo por RPM em 0.1 graus
    uint16_t final_advance_deg10;  // Avanço final em 0.1 graus
    bool limp_mode;                // Modo limp ativo
} ignition_calc_t;

// Estrutura de configuração de ignição
typedef struct {
    uint16_t base_advance_deg10;   // Avanço base em 0.1 graus
    uint16_t max_advance_deg10;    // Avanço máximo em 0.1 graus
    uint16_t min_advance_deg10;    // Avanço mínimo em 0.1 graus
    uint16_t knock_threshold;      // Limiar de detecção de knock
    uint16_t rpm_limit_rpm;        // Limite de RPM para avanço
    uint16_t warmup_time_ms;       // Tempo de aquecimento em ms
} ignition_config_t;

// Estrutura de sincronização de ignição
typedef struct {
    uint16_t tooth_period_us;      // Período do dente em microssegundos
    uint16_t rpm;                  // RPM calculado
    uint8_t current_tooth;         // Dente atual
    bool sync_lost;                // Perda de sincronismo
    uint32_t last_sync_time;       // Último tempo de sincronismo
} ignition_sync_t;

/**
 * @brief Initialize ignition timing system
 */
void ignition_init(void);

/**
 * @brief Calculate ignition timing advance
 * @param sensors Sensor data
 * @param rpm Engine RPM
 * @param knock_protection Knock protection structure
 * @param calc Calculation structure (output)
 * @return Calculated advance in 0.1 degrees
 */
uint16_t ignition_calculate_advance(const sensor_data_t *sensors,
                                    uint16_t rpm,
                                    knock_protection_t *knock_protection,
                                    ignition_calc_t *calc);

/**
 * @brief Apply ignition timing
 * @param advance_deg10 Advance in 0.1 degrees
 * @param rpm Engine RPM
 */
void ignition_apply_timing(uint16_t advance_deg10, uint16_t rpm);

/**
 * @brief Get current ignition configuration
 * @return Pointer to ignition configuration
 */
const ignition_config_t* ignition_get_config(void);

/**
 * @brief Set ignition configuration
 * @param config Pointer to ignition configuration
 */
void ignition_set_config(const ignition_config_t *config);

/**
 * @brief Reset ignition configuration to defaults
 */
void ignition_reset_config(void);

/**
 * @brief Get ignition synchronization status
 * @return Pointer to synchronization structure
 */
const ignition_sync_t* ignition_get_sync_status(void);

/**
 * @brief Reset ignition synchronization
 */
void ignition_reset_sync(void);

/**
 * @brief Check for ignition safety conditions
 * @param rpm Engine RPM
 * @param advance_deg10 Advance in 0.1 degrees
 * @return true if safe to fire
 */
bool ignition_check_safety(uint16_t rpm, uint16_t advance_deg10);

/**
 * @brief Test ignition coil
 * @param coil_id Coil ID (0-3)
 * @param duration_ms Duration in milliseconds
 */
void ignition_test_coil(uint8_t coil_id, uint32_t duration_ms);

/**
 * @brief Get ignition timing status
 * @param coil_id Coil ID (0-3)
 * @return true if coil is active
 */
bool ignition_get_status(uint8_t coil_id);

/**
 * @brief Handle knock detection and retard
 * @param knock_protection Knock protection structure
 * @param detected True if knock detected
 */
void ignition_handle_knock(knock_protection_t *knock_protection, bool detected);

#ifdef __cplusplus
}
#endif

#endif // IGNITION_TIMING_H
