/**
 * @file mcpwm_ignition_hp.h
 * @brief Header do driver MCPWM de ignição otimizado com compare absoluto
 */

#ifndef MCPWM_IGNITION_HP_H
#define MCPWM_IGNITION_HP_H

#include <stdbool.h>
#include <stdint.h>
#include "mcpwm_ignition.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa o driver de ignição de alta precisão
 * @return true se bem-sucedido
 */
bool mcpwm_ignition_hp_init(void);

/**
 * @brief Agenda evento de ignição com compare absoluto
 * 
 * Esta função usa compare absoluto em vez de recalcular delays.
 * O timer roda continuamente sem reinício por evento.
 * 
 * @param cylinder_id ID do cilindro (1-4)
 * @param target_us Tempo absoluto desde o início da janela (em microssegundos)
 * @param rpm RPM atual para cálculo de dwell
 * @param battery_voltage Tensão da bateria para cálculo de dwell
 * @param current_counter Valor atual do contador do timer
 * @return true se bem-sucedido
 */
bool mcpwm_ignition_hp_schedule_one_shot_absolute(
    uint8_t cylinder_id,
    uint32_t target_us,
    uint16_t rpm,
    float battery_voltage,
    uint32_t current_counter);

/**
 * @brief Agenda múltiploscilindros sequencialmente
 */
bool mcpwm_ignition_hp_schedule_sequential_absolute(
    uint16_t rpm,
    float battery_voltage,
    uint32_t base_target_us,
    uint32_t cylinder_offsets[4]);

/**
 * @brief Para cilindro específico
 */
bool mcpwm_ignition_hp_stop_cylinder(uint8_t cylinder_id);

/**
 * @brief Obtém status do canal de ignição
 */
bool mcpwm_ignition_hp_get_status(uint8_t cylinder_id, mcpwm_ignition_status_t *status);

/**
 * @brief Atualiza preditor de fase com medição
 */
void mcpwm_ignition_hp_update_phase_predictor(float measured_period_us, uint32_t timestamp);

/**
 * @brief Obtém estatísticas de jitter
 */
void mcpwm_ignition_hp_get_jitter_stats(float *avg_us, float *max_us, float *min_us);

/**
 * @brief Aplica compensação de latência física
 */
void mcpwm_ignition_hp_apply_latency_compensation(float *timing_us, float battery_voltage, float temperature);

/**
 * @brief Desinicializa o driver
 */
bool mcpwm_ignition_hp_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MCPWM_IGNITION_HP_H
