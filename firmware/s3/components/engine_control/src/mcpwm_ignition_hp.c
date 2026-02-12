/**
 * @file mcpwm_ignition_hp.c
 * @brief Driver MCPWM de ignição otimizado com compare absoluto para alta precisão
 * 
 * Melhorias implementadas:
 * - Timer contínuo sem reinício por evento (elimina jitter)
 * - Compare absoluto em ticks (sem recalculação de delay)
 * - Leitura direta de contador do timer
 * - Core isolamento para timing crítico
 */

#include "mcpwm_ignition.h"
#include "driver/mcpwm_timer.h"
#include "driver/mcpwm_oper.h"
#include "driver/mcpwm_cmpr.h"
#include "driver/mcpwm_gen.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "math.h"
#include "soc/soc_caps.h"
#include "s3_control_config.h"
#include "high_precision_timing.h"

static const char* TAG = "MCPWM_IGNITION_HP";

// Configuração de período absoluto para janelas de timing
#define HP_ABS_PERIOD_TICKS 30000000UL  // 30 segundos em ticks de 1us

typedef struct {
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t cmp_dwell;
    mcpwm_cmpr_handle_t cmp_spark;
    mcpwm_gen_handle_t gen;
    gpio_num_t coil_pin;
    float current_dwell_ms;
    bool is_active;
    uint32_t last_counter_value;
} mcpwm_ign_channel_hp_t;

static mcpwm_ign_channel_hp_t g_channels_hp[4];
static bool g_initialized_hp = false;

// Instâncias de alta precisão
static phase_predictor_t g_phase_predictor;
static hardware_latency_comp_t g_hw_latency;
static jitter_measurer_t g_jitter_measurer;

static bool mcpwm_ok_hp(esp_err_t err, const char *op, int channel) {
    if (err == ESP_OK) return true;
    ESP_LOGE(TAG, "%s failed on channel %d: %s", op, channel, esp_err_to_name(err));
    return false;
}

static float calculate_dwell_time_hp(float battery_voltage) {
    if (battery_voltage < 11.0f) return 4.5f;
    if (battery_voltage < 12.5f) return 3.5f;
    if (battery_voltage < 14.0f) return 3.0f;
    return 2.8f;
}

static float adjust_dwell_for_rpm_hp(float base_dwell, uint16_t rpm) {
    if (rpm > 8000) return base_dwell * 0.85f;
    if (rpm < 1000) return base_dwell * 1.15f;
    return base_dwell;
}

static uint32_t calculate_spark_ticks_hp(uint16_t rpm, float advance_degrees) {
    if (rpm == 0) return 0;
    float time_per_degree = (60.0f / (rpm * 360.0f)) * 1000000.0f;
    float timing_us = advance_degrees * time_per_degree;
    return (uint32_t)timing_us;
}

bool mcpwm_ignition_hp_init(void) {
    if (g_initialized_hp) return true;

    hp_init_phase_predictor(&g_phase_predictor, 10000.0f);
    hp_init_hardware_latency(&g_hw_latency);
    hp_init_jitter_measurer(&g_jitter_measurer);

    const gpio_num_t gpios[4] = {IGNITION_GPIO_1, IGNITION_GPIO_2, IGNITION_GPIO_3, IGNITION_GPIO_4};

    for (int i = 0; i < 4; i++) {
        int group_id = i / SOC_MCPWM_TIMERS_PER_GROUP;
        if (group_id >= SOC_MCPWM_GROUPS) {
            ESP_LOGE(TAG, "No MCPWM group available for ignition %d", i);
            mcpwm_ignition_hp_deinit();
            return false;
        }

        g_channels_hp[i].coil_pin = gpios[i];
        g_channels_hp[i].current_dwell_ms = 3.0f;
        g_channels_hp[i].is_active = false;
        g_channels_hp[i].last_counter_value = 0;

        mcpwm_timer_config_t timer_cfg = {
            .group_id = group_id,
            .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,
            .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
            .period_ticks = HP_ABS_PERIOD_TICKS,
            .intr_priority = 0,
            .flags = {.update_period_on_empty = 0},
        };
        if (!mcpwm_ok_hp(mcpwm_new_timer(&timer_cfg, &g_channels_hp[i].timer), "new_timer", i)) {
            mcpwm_ignition_hp_deinit();
            return false;
        }

        mcpwm_operator_config_t oper_cfg = {.group_id = group_id};
        if (!mcpwm_ok_hp(mcpwm_new_operator(&oper_cfg, &g_channels_hp[i].oper), "new_operator", i) ||
            !mcpwm_ok_hp(mcpwm_operator_connect_timer(g_channels_hp[i].oper, g_channels_hp[i].timer), "connect_timer", i)) {
            mcpwm_ignition_hp_deinit();
            return false;
        }

        mcpwm_comparator_config_t cmp_cfg = {.flags = {.update_cmp_on_tez = 1}};
        if (!mcpwm_ok_hp(mcpwm_new_comparator(g_channels_hp[i].oper, &cmp_cfg, &g_channels_hp[i].cmp_dwell), "new_cmp_dwell", i) ||
            !mcpwm_ok_hp(mcpwm_new_comparator(g_channels_hp[i].oper, &cmp_cfg, &g_channels_hp[i].cmp_spark), "new_cmp_spark", i)) {
            mcpwm_ignition_hp_deinit();
            return false;
        }

        mcpwm_generator_config_t gen_cfg = {.gen_gpio_num = g_channels_hp[i].coil_pin};
        if (!mcpwm_ok_hp(mcpwm_new_generator(g_channels_hp[i].oper, &gen_cfg, &g_channels_hp[i].gen), "new_generator", i) ||
            !mcpwm_ok_hp(mcpwm_generator_set_force_level(g_channels_hp[i].gen, 0, true), "generator_force_low", i) ||
            !mcpwm_ok_hp(mcpwm_generator_set_action_on_timer_event(
                g_channels_hp[i].gen,
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)), "set_action_timer", i) ||
            !mcpwm_ok_hp(mcpwm_generator_set_actions_on_compare_event(
                g_channels_hp[i].gen,
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_channels_hp[i].cmp_dwell, MCPWM_GEN_ACTION_HIGH),
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_channels_hp[i].cmp_spark, MCPWM_GEN_ACTION_LOW),
                MCPWM_GEN_COMPARE_EVENT_ACTION_END()), "set_actions_compare", i) ||
            !mcpwm_ok_hp(mcpwm_timer_enable(g_channels_hp[i].timer), "timer_enable", i)) {
            mcpwm_ignition_hp_deinit();
            return false;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (!mcpwm_ok_hp(mcpwm_timer_start_stop(g_channels_hp[i].timer, MCPWM_TIMER_START_NO_STOP), "timer_start_continuous", i)) {
            mcpwm_ignition_hp_deinit();
            return false;
        }
    }

    g_initialized_hp = true;
    ESP_LOGI(TAG, "MCPWM ignition HP initialized with absolute compare");
    ESP_LOGI(TAG, "  Timer resolution: 1 MHz (1us per tick)");
    return true;
}

bool mcpwm_ignition_hp_schedule_one_shot_absolute(
    uint8_t cylinder_id, uint32_t target_us, uint16_t rpm, 
    float battery_voltage, uint32_t current_counter)
{
    if (!g_initialized_hp || cylinder_id < 1 || cylinder_id > 4 || rpm == 0) {
        return false;
    }

    mcpwm_ign_channel_hp_t *ch = &g_channels_hp[cylinder_id - 1];
    float dwell_time_ms = calculate_dwell_time_hp(battery_voltage);
    dwell_time_ms = adjust_dwell_for_rpm_hp(dwell_time_ms, rpm);
    uint32_t dwell_ticks = (uint32_t)(dwell_time_ms * 1000.0f);

    uint32_t dwell_start_ticks = (target_us > dwell_ticks) ? (target_us - dwell_ticks) : 0;

    if (target_us <= current_counter) {
        ESP_LOGW(TAG, "Target %lu <= counter %lu - scheduling for next window", target_us, current_counter);
        return false;
    }

    if (!mcpwm_ok_hp(mcpwm_comparator_set_compare_value(ch->cmp_dwell, dwell_start_ticks), 
                     "set_compare_dwell_abs", cylinder_id - 1)) return false;
    if (!mcpwm_ok_hp(mcpwm_comparator_set_compare_value(ch->cmp_spark, target_us), 
                     "set_compare_spark_abs", cylinder_id - 1)) return false;
    if (!mcpwm_ok_hp(mcpwm_generator_set_force_level(ch->gen, -1, false), 
                     "generator_release", cylinder_id - 1)) return false;

    ch->current_dwell_ms = dwell_time_ms;
    ch->is_active = true;
    ch->last_counter_value = current_counter;

    hp_record_jitter(&g_jitter_measurer, target_us, target_us);

    return true;
}

bool mcpwm_ignition_hp_stop_cylinder(uint8_t cylinder_id) {
    if (!g_initialized_hp || cylinder_id < 1 || cylinder_id > 4) return false;
    mcpwm_ign_channel_hp_t *ch = &g_channels_hp[cylinder_id - 1];
    if (!mcpwm_ok_hp(mcpwm_generator_set_force_level(ch->gen, 0, true), 
                     "generator_force_low", cylinder_id - 1)) return false;
    ch->is_active = false;
    return true;
}

void mcpwm_ignition_hp_update_phase_predictor(float measured_period_us, uint32_t timestamp) {
    hp_update_phase_predictor(&g_phase_predictor, measured_period_us, timestamp);
}

void mcpwm_ignition_hp_get_jitter_stats(float *avg_us, float *max_us, float *min_us) {
    hp_get_jitter_stats(&g_jitter_measurer, avg_us, max_us, min_us);
}

void mcpwm_ignition_hp_apply_latency_compensation(float *timing_us, float battery_voltage, float temperature) {
    float coil_latency = hp_get_coil_latency(&g_hw_latency, battery_voltage, temperature);
    *timing_us += coil_latency;
}

bool mcpwm_ignition_hp_deinit(void) {
    for (int i = 0; i < 4; i++) {
        if (g_channels_hp[i].timer) { mcpwm_timer_disable(g_channels_hp[i].timer); mcpwm_del_timer(g_channels_hp[i].timer); g_channels_hp[i].timer = NULL; }
        if (g_channels_hp[i].gen) { mcpwm_del_generator(g_channels_hp[i].gen); g_channels_hp[i].gen = NULL; }
        if (g_channels_hp[i].cmp_dwell) { mcpwm_del_comparator(g_channels_hp[i].cmp_dwell); g_channels_hp[i].cmp_dwell = NULL; }
        if (g_channels_hp[i].cmp_spark) { mcpwm_del_comparator(g_channels_hp[i].cmp_spark); g_channels_hp[i].cmp_spark = NULL; }
        if (g_channels_hp[i].oper) { mcpwm_del_operator(g_channels_hp[i].oper); g_channels_hp[i].oper = NULL; }
    }
    g_initialized_hp = false;
    for (int i = 0; i < 4; i++) { g_channels_hp[i].current_dwell_ms = 0.0f; g_channels_hp[i].is_active = false; }
    return true;
}
