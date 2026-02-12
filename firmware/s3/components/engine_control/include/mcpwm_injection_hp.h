/**
 * @file mcpwm_injection_hp.h
 * @brief Header do driver MCPWM de injeção otimizado com compare absoluto
 */

#ifndef MCPWM_INJECTION_HP_H
#define MCPWM_INJECTION_HP_H

#include <stdbool.h>
#include <stdint.h>
#include "mcpwm_injection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa o driver de injeção de alta precisão
 * @return true se bem-sucedido
 */
bool mcpwm_injection_hp_init(void);

/**
 * @brief Configura parâmetros do driver
 */
bool mcpwm_injection_hp_configure(const mcpwm_injection_config_t *config);

/**
 * @brief Agenda evento de injeção com compare absoluto
 * 
 * Esta função usa compare absoluto em vez de recalcular delays.
 * O timer roda continuamente sem reinício por evento.
 * 
 * @param cylinder_id ID do injetor (0-3)
 * @param delay_us Delay absoluto desde o início da janela (em microssegundos)
 * @param pulsewidth_us Largura de pulso desejada
 * @param current_counter Valor atual do contador do timer
 * @return true se bem-sucedido
 */
bool mcpwm_injection_hp_schedule_one_shot_absolute(
    uint8_t cylinder_id,
    uint32_t delay_us,
    uint32_t pulsewidth_us,
    uint32_t current_counter);

/**
 * @brief Agenda múltiplos injetores sequencialmente
 */
bool mcpwm_injection_hp_schedule_sequential_absolute(
    uint32_t base_delay_us,
    uint32_t pulsewidth_us,
    uint32_t cylinder_offsets[4],
    uint32_t current_counter);

/**
 * @brief Para injetor específico
 */
bool mcpwm_injection_hp_stop(uint8_t cylinder_id);

/**
 * @brief Para todos os injetores
 */
bool mcpwm_injection_hp_stop_all(void);

/**
 * @brief Obtém status do canal de injeção
 */
bool mcpwm_injection_hp_get_status(uint8_t cylinder_id, mcpwm_injector_channel_t *status);

/**
 * @brief Obtém estatísticas de jitter
 */
void mcpwm_injection_hp_get_jitter_stats(float *avg_us, float *max_us, float *min_us);

/**
 * @brief Aplica compensação de latência física
 */
void mcpwm_injection_hp_apply_latency_compensation(float *pulsewidth_us, float battery_voltage, float temperature);

/**
 * @brief Obtém configuração atual
 */
const mcpwm_injection_config_t* mcpwm_injection_hp_get_config(void);

/**
 * @brief Desinicializa o driver
 */
bool mcpwm_injection_hp_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MCPWM_INJECTION_HP_H
