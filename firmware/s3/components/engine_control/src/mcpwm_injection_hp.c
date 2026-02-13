/**
 * @file mcpwm_injection_hp.c
 * @brief Driver MCPWM de injeção otimizado com compare absoluto para alta precisão
 * 
 * Melhorias implementadas:
 * - Timer contínuo sem reinício por evento (elimina jitter)
 * - Compare absoluto em ticks (sem recalculação de delay)
 * - Leitura direta de contador do timer
 */

#include "mcpwm_injection.h"
#include "driver/mcpwm_timer.h"
#include "driver/mcpwm_oper.h"
#include "driver/mcpwm_cmpr.h"
#include "driver/mcpwm_gen.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "soc/soc_caps.h"
#include "s3_control_config.h"
#include "high_precision_timing.h"

static const char* TAG = "MCPWM_INJECTION_HP";

#define HP_INJ_ABS_PERIOD_TICKS 30000000UL  // 30 segundos em ticks de 1us

typedef struct {
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t cmp_start;
    mcpwm_cmpr_handle_t cmp_end;
    mcpwm_gen_handle_t gen;
    gpio_num_t gpio;
    uint32_t pulsewidth_us;
    bool is_active;
    uint32_t last_counter_value;
} mcpwm_injection_channel_hp_t;

static mcpwm_injection_channel_hp_t g_channels_hp[4];
static bool g_initialized_hp = false;

// Instâncias de alta precisão
static hardware_latency_comp_t g_hw_latency_inj;
static jitter_measurer_t g_jitter_measurer_inj;

static mcpwm_injection_config_t g_cfg = {
    .base_frequency_hz = 1000000,
    .timer_resolution_bits = 20,
    .min_pulsewidth_us = 500,
    .max_pulsewidth_us = 18000,
    .deadtime_us = 200,
};

static bool mcpwm_ok_hp(esp_err_t err, const char *op, int channel) {
    if (err == ESP_OK) return true;
    ESP_LOGE(TAG, "%s failed on channel %d: %s", op, channel, esp_err_to_name(err));
    return false;
}

IRAM_ATTR static uint32_t clamp_u32_hp(uint32_t v, uint32_t min_v, uint32_t max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

bool mcpwm_injection_hp_init(void) {
    if (g_initialized_hp) return true;

    hp_init_hardware_latency(&g_hw_latency_inj);
    hp_init_jitter_measurer(&g_jitter_measurer_inj);

    const gpio_num_t gpios[4] = {INJECTOR_GPIO_1, INJECTOR_GPIO_2, INJECTOR_GPIO_3, INJECTOR_GPIO_4};

    for (int i = 0; i < 4; i++) {
        int group_id = i / SOC_MCPWM_TIMERS_PER_GROUP;
        if (group_id >= SOC_MCPWM_GROUPS) {
            ESP_LOGE(TAG, "No MCPWM group available for injector %d", i);
            mcpwm_injection_hp_deinit();
            return false;
        }

        g_channels_hp[i].gpio = gpios[i];
        g_channels_hp[i].pulsewidth_us = 0;
        g_channels_hp[i].is_active = false;
        g_channels_hp[i].last_counter_value = 0;

        // Timer contínuo - SEM START_STOP_FULL por evento
        mcpwm_timer_config_t timer_cfg = {
            .group_id = group_id,
            .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,  // 1 MHz = 1us por tick
            .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
            .period_ticks = HP_INJ_ABS_PERIOD_TICKS,
            .intr_priority = 0,
            .flags = {.update_period_on_empty = 0},  // NÃO atualizar período
        };
        if (!mcpwm_ok_hp(mcpwm_new_timer(&timer_cfg, &g_channels_hp[i].timer), "new_timer", i)) {
            mcpwm_injection_hp_deinit();
            return false;
        }

        mcpwm_operator_config_t oper_cfg = {.group_id = group_id};
        if (!mcpwm_ok_hp(mcpwm_new_operator(&oper_cfg, &g_channels_hp[i].oper), "new_operator", i) ||
            !mcpwm_ok_hp(mcpwm_operator_connect_timer(g_channels_hp[i].oper, g_channels_hp[i].timer), "connect_timer", i)) {
            mcpwm_injection_hp_deinit();
            return false;
        }

        mcpwm_comparator_config_t cmpr_cfg = {.flags = {.update_cmp_on_tez = 1}};
        if (!mcpwm_ok_hp(mcpwm_new_comparator(g_channels_hp[i].oper, &cmpr_cfg, &g_channels_hp[i].cmp_start), "new_cmp_start", i) ||
            !mcpwm_ok_hp(mcpwm_new_comparator(g_channels_hp[i].oper, &cmpr_cfg, &g_channels_hp[i].cmp_end), "new_cmp_end", i)) {
            mcpwm_injection_hp_deinit();
            return false;
        }

        mcpwm_generator_config_t gen_cfg = {.gen_gpio_num = g_channels_hp[i].gpio};
        if (!mcpwm_ok_hp(mcpwm_new_generator(g_channels_hp[i].oper, &gen_cfg, &g_channels_hp[i].gen), "new_generator", i) ||
            !mcpwm_ok_hp(mcpwm_generator_set_force_level(g_channels_hp[i].gen, 0, true), "generator_force_low", i) ||
            !mcpwm_ok_hp(mcpwm_generator_set_actions_on_timer_event(
                g_channels_hp[i].gen,
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW),
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_FULL, MCPWM_GEN_ACTION_LOW),
                MCPWM_GEN_TIMER_EVENT_ACTION_END()), "set_actions_timer", i) ||
            !mcpwm_ok_hp(mcpwm_generator_set_actions_on_compare_event(
                g_channels_hp[i].gen,
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_channels_hp[i].cmp_start, MCPWM_GEN_ACTION_HIGH),
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_channels_hp[i].cmp_end, MCPWM_GEN_ACTION_LOW),
                MCPWM_GEN_COMPARE_EVENT_ACTION_END()), "set_actions_compare", i) ||
            !mcpwm_ok_hp(mcpwm_timer_enable(g_channels_hp[i].timer), "timer_enable", i)) {
            mcpwm_injection_hp_deinit();
            return false;
        }
    }

    // Iniciar todos os timers em modo contínuo
    for (int i = 0; i < 4; i++) {
        if (!mcpwm_ok_hp(mcpwm_timer_start_stop(g_channels_hp[i].timer, MCPWM_TIMER_START_NO_STOP), "timer_start_continuous", i)) {
            mcpwm_injection_hp_deinit();
            return false;
        }
    }

    g_initialized_hp = true;
    ESP_LOGI(TAG, "MCPWM injection HP initialized with absolute compare");
    ESP_LOGI(TAG, "  Timer resolution: 1 MHz (1us per tick)");
    return true;
}

bool mcpwm_injection_hp_configure(const mcpwm_injection_config_t *config) {
    if (config == NULL) return false;
    g_cfg = *config;
    return true;
}

/**
 * @brief Agenda evento de injeção com compare absoluto
 * @note IRAM_ATTR - função crítica de timing chamada em contexto de ISR
 */
IRAM_ATTR bool mcpwm_injection_hp_schedule_one_shot_absolute(
    uint8_t cylinder_id,
    uint32_t delay_us,
    uint32_t pulsewidth_us,
    uint32_t current_counter)
{
    if (!g_initialized_hp || cylinder_id >= 4) return false;

    mcpwm_injection_channel_hp_t *ch = &g_channels_hp[cylinder_id];
    uint32_t pw = clamp_u32_hp(pulsewidth_us, g_cfg.min_pulsewidth_us, g_cfg.max_pulsewidth_us);

    // Calcular valores ABSOLUTOS
    uint32_t start_ticks = delay_us;
    uint32_t end_ticks = delay_us + pw;

    // Verificar se o target já passou
    if (delay_us <= current_counter) {
        return false;
    }

    mcpwm_comparator_set_compare_value(ch->cmp_start, start_ticks);
    mcpwm_comparator_set_compare_value(ch->cmp_end, end_ticks);
    mcpwm_generator_set_force_level(ch->gen, -1, false);

    // NÃO reiniciar timer - usar timer contínuo!

    ch->pulsewidth_us = pw;
    ch->is_active = true;
    ch->last_counter_value = current_counter;

    hp_record_jitter(&g_jitter_measurer_inj, delay_us, delay_us);

    return true;
}

/**
 * @brief Agenda múltiplos injetores sequencialmente
 * @note IRAM_ATTR - função crítica de timing
 */
IRAM_ATTR bool mcpwm_injection_hp_schedule_sequential_absolute(
    uint32_t base_delay_us,
    uint32_t pulsewidth_us,
    uint32_t cylinder_offsets[4],
    uint32_t current_counter)
{
    if (!g_initialized_hp) return false;

    bool all_success = true;
    for (int i = 0; i < 4; i++) {
        uint32_t delay_us = base_delay_us + cylinder_offsets[i];
        if (!mcpwm_injection_hp_schedule_one_shot_absolute((uint8_t)i, delay_us, pulsewidth_us, current_counter)) {
            all_success = false;
        }
    }
    return all_success;
}

bool mcpwm_injection_hp_stop(uint8_t cylinder_id) {
    if (!g_initialized_hp || cylinder_id >= 4) return false;
    mcpwm_injection_channel_hp_t *ch = &g_channels_hp[cylinder_id];
    if (!mcpwm_ok_hp(mcpwm_generator_set_force_level(ch->gen, 0, true), "generator_force_low", cylinder_id)) return false;
    ch->pulsewidth_us = 0;
    ch->is_active = false;
    return true;
}

bool mcpwm_injection_hp_stop_all(void) {
    for (int i = 0; i < 4; i++) {
        if (!mcpwm_injection_hp_stop((uint8_t)i)) return false;
    }
    return true;
}

bool mcpwm_injection_hp_get_status(uint8_t cylinder_id, mcpwm_injector_channel_t *status) {
    if (!g_initialized_hp || cylinder_id >= 4 || status == NULL) return false;
    mcpwm_injection_channel_hp_t *ch = &g_channels_hp[cylinder_id];
    status->channel_id = cylinder_id;
    status->gpio = ch->gpio;
    status->pulsewidth_us = ch->pulsewidth_us;
    status->is_active = ch->is_active;
    return true;
}

/**
 * @brief Obtém estatísticas de jitter de injeção
 */
void mcpwm_injection_hp_get_jitter_stats(float *avg_us, float *max_us, float *min_us) {
    hp_get_jitter_stats(&g_jitter_measurer_inj, avg_us, max_us, min_us);
}

/**
 * @brief Aplica compensação de latência física de injetor
 */
void mcpwm_injection_hp_apply_latency_compensation(float *pulsewidth_us, float battery_voltage, float temperature) {
    float injector_latency = hp_get_injector_latency(&g_hw_latency_inj, battery_voltage, temperature);
    *pulsewidth_us += injector_latency;
}

IRAM_ATTR uint32_t mcpwm_injection_hp_get_counter(uint8_t cylinder_id) {
    if (cylinder_id >= 4 || !g_initialized_hp || !g_channels_hp[cylinder_id].timer) {
        return 0;
    }
    uint32_t counter = 0;
    esp_err_t err = mcpwm_timer_get_counter_value(g_channels_hp[cylinder_id].timer, &counter);
    if (err != ESP_OK) {
        return 0;
    }
    return counter;
}

const mcpwm_injection_config_t* mcpwm_injection_hp_get_config(void) {
    return &g_cfg;
}

bool mcpwm_injection_hp_deinit(void) {
    for (int i = 0; i < 4; i++) {
        if (g_channels_hp[i].timer) { mcpwm_timer_disable(g_channels_hp[i].timer); mcpwm_del_timer(g_channels_hp[i].timer); g_channels_hp[i].timer = NULL; }
        if (g_channels_hp[i].gen) { mcpwm_del_generator(g_channels_hp[i].gen); g_channels_hp[i].gen = NULL; }
        if (g_channels_hp[i].cmp_start) { mcpwm_del_comparator(g_channels_hp[i].cmp_start); g_channels_hp[i].cmp_start = NULL; }
        if (g_channels_hp[i].cmp_end) { mcpwm_del_comparator(g_channels_hp[i].cmp_end); g_channels_hp[i].cmp_end = NULL; }
        if (g_channels_hp[i].oper) { mcpwm_del_operator(g_channels_hp[i].oper); g_channels_hp[i].oper = NULL; }
    }
    g_initialized_hp = false;
    for (int i = 0; i < 4; i++) { g_channels_hp[i].pulsewidth_us = 0; g_channels_hp[i].is_active = false; }
    return true;
}
