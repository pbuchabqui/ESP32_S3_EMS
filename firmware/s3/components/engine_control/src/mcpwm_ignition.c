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

static const char* TAG = "MCPWM_IGNITION";

typedef struct {
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t cmp_dwell;
    mcpwm_cmpr_handle_t cmp_spark;
    mcpwm_gen_handle_t gen;
    gpio_num_t coil_pin;
    float current_dwell_ms;
    bool is_active;
} mcpwm_ign_channel_t;

static mcpwm_ign_channel_t g_channels[4];
static bool g_initialized = false;

static bool mcpwm_ok(esp_err_t err, const char *op, int channel) {
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "%s failed on channel %d: %s", op, channel, esp_err_to_name(err));
    return false;
}

static float calculate_dwell_time(float battery_voltage) {
    if (battery_voltage < 11.0f) return 4.5f;
    if (battery_voltage < 12.5f) return 3.5f;
    if (battery_voltage < 14.0f) return 3.0f;
    return 2.8f;
}

static float adjust_dwell_for_rpm(float base_dwell, uint16_t rpm) {
    if (rpm > 8000) return base_dwell * 0.85f;
    if (rpm < 1000) return base_dwell * 1.15f;
    return base_dwell;
}

static uint32_t calculate_spark_ticks(uint16_t rpm, float advance_degrees) {
    if (rpm == 0) {
        return 0;
    }
    float time_per_degree = (60.0f / (rpm * 360.0f)) * 1000000.0f;
    float timing_us = advance_degrees * time_per_degree;
    return (uint32_t)timing_us;
}

bool mcpwm_ignition_init(void) {
    if (g_initialized) {
        return true;
    }

    const gpio_num_t gpios[4] = {
        IGNITION_GPIO_1,
        IGNITION_GPIO_2,
        IGNITION_GPIO_3,
        IGNITION_GPIO_4
    };

    for (int i = 0; i < 4; i++) {
        int group_id = i / SOC_MCPWM_TIMERS_PER_GROUP;
        if (group_id >= SOC_MCPWM_GROUPS) {
            ESP_LOGE(TAG, "No MCPWM group available for ignition %d", i);
            mcpwm_ignition_deinit();
            return false;
        }

        g_channels[i].coil_pin = gpios[i];
        g_channels[i].current_dwell_ms = 3.0f;
        g_channels[i].is_active = false;

        mcpwm_timer_config_t timer_cfg = {
            .group_id = group_id,
            .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,
            .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
            .period_ticks = 30000,
            .intr_priority = 0,
            .flags = {
                .update_period_on_empty = 1,
            },
        };
        if (!mcpwm_ok(mcpwm_new_timer(&timer_cfg, &g_channels[i].timer), "new_timer", i)) {
            mcpwm_ignition_deinit();
            return false;
        }

        mcpwm_operator_config_t oper_cfg = {
            .group_id = group_id,
        };
        if (!mcpwm_ok(mcpwm_new_operator(&oper_cfg, &g_channels[i].oper), "new_operator", i) ||
            !mcpwm_ok(mcpwm_operator_connect_timer(g_channels[i].oper, g_channels[i].timer), "connect_timer", i)) {
            mcpwm_ignition_deinit();
            return false;
        }

        mcpwm_comparator_config_t cmp_cfg = {
            .flags = {
                .update_cmp_on_tez = 1,
            },
        };
        if (!mcpwm_ok(mcpwm_new_comparator(g_channels[i].oper, &cmp_cfg, &g_channels[i].cmp_dwell), "new_cmp_dwell", i) ||
            !mcpwm_ok(mcpwm_new_comparator(g_channels[i].oper, &cmp_cfg, &g_channels[i].cmp_spark), "new_cmp_spark", i)) {
            mcpwm_ignition_deinit();
            return false;
        }

        mcpwm_generator_config_t gen_cfg = {
            .gen_gpio_num = g_channels[i].coil_pin,
        };
        if (!mcpwm_ok(mcpwm_new_generator(g_channels[i].oper, &gen_cfg, &g_channels[i].gen), "new_generator", i) ||
            !mcpwm_ok(mcpwm_generator_set_force_level(g_channels[i].gen, 0, true), "generator_force_low", i) ||
            !mcpwm_ok(mcpwm_generator_set_action_on_timer_event(
                g_channels[i].gen,
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)), "set_action_timer", i) ||
            !mcpwm_ok(mcpwm_generator_set_actions_on_compare_event(
                g_channels[i].gen,
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_channels[i].cmp_dwell, MCPWM_GEN_ACTION_HIGH),
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_channels[i].cmp_spark, MCPWM_GEN_ACTION_LOW),
                MCPWM_GEN_COMPARE_EVENT_ACTION_END()), "set_actions_compare", i) ||
            !mcpwm_ok(mcpwm_timer_enable(g_channels[i].timer), "timer_enable", i)) {
            mcpwm_ignition_deinit();
            return false;
        }
    }

    g_initialized = true;
    ESP_LOGI(TAG, "MCPWM ignition system initialized");
    return true;
}

bool mcpwm_ignition_start_cylinder(uint8_t cylinder_id, uint16_t rpm, float advance_degrees, float battery_voltage) {
    if (!g_initialized || cylinder_id < 1 || cylinder_id > 4 || rpm == 0) {
        return false;
    }

    mcpwm_ign_channel_t *ch = &g_channels[cylinder_id - 1];

    float dwell_time_ms = calculate_dwell_time(battery_voltage);
    dwell_time_ms = adjust_dwell_for_rpm(dwell_time_ms, rpm);

    uint32_t dwell_ticks = (uint32_t)(dwell_time_ms * 1000.0f);
    uint32_t spark_ticks = calculate_spark_ticks(rpm, advance_degrees);
    if (spark_ticks == 0) {
        return false;
    }

    uint32_t dwell_start_delay = 0;
    if (spark_ticks > dwell_ticks) {
        dwell_start_delay = spark_ticks - dwell_ticks;
    }

    uint32_t period = spark_ticks + 10;
    if (period <= spark_ticks) {
        period = spark_ticks + 10;
    }

    if (!mcpwm_ok(mcpwm_timer_set_period(ch->timer, period), "timer_set_period", cylinder_id - 1)) {
        return false;
    }
    if (!mcpwm_ok(mcpwm_comparator_set_compare_value(ch->cmp_dwell, dwell_start_delay), "set_compare_dwell", cylinder_id - 1)) {
        return false;
    }
    if (!mcpwm_ok(mcpwm_comparator_set_compare_value(ch->cmp_spark, spark_ticks), "set_compare_spark", cylinder_id - 1)) {
        return false;
    }

    if (!mcpwm_ok(mcpwm_generator_set_force_level(ch->gen, -1, false), "generator_set_force_level", cylinder_id - 1)) {
        return false;
    }

    if (!mcpwm_ok(mcpwm_timer_start_stop(ch->timer, MCPWM_TIMER_START_STOP_FULL), "timer_start_stop", cylinder_id - 1)) {
        return false;
    }

    ch->current_dwell_ms = dwell_time_ms;
    ch->is_active = true;
    return true;
}

bool mcpwm_ignition_schedule_one_shot(uint8_t cylinder_id, uint32_t spark_delay_us, uint16_t rpm, float battery_voltage) {
    if (!g_initialized || cylinder_id < 1 || cylinder_id > 4 || rpm == 0) {
        return false;
    }

    mcpwm_ign_channel_t *ch = &g_channels[cylinder_id - 1];

    float dwell_time_ms = calculate_dwell_time(battery_voltage);
    dwell_time_ms = adjust_dwell_for_rpm(dwell_time_ms, rpm);
    uint32_t dwell_ticks = (uint32_t)(dwell_time_ms * 1000.0f);

    uint32_t dwell_start_delay = 0;
    if (spark_delay_us > dwell_ticks) {
        dwell_start_delay = spark_delay_us - dwell_ticks;
    }

    if (spark_delay_us >= (UINT32_MAX - 1U)) {
        return false;
    }
    uint32_t period = spark_delay_us + 1U;

    if (!mcpwm_ok(mcpwm_timer_set_period(ch->timer, period), "timer_set_period", cylinder_id - 1)) {
        return false;
    }
    if (!mcpwm_ok(mcpwm_comparator_set_compare_value(ch->cmp_dwell, dwell_start_delay), "set_compare_dwell", cylinder_id - 1)) {
        return false;
    }
    if (!mcpwm_ok(mcpwm_comparator_set_compare_value(ch->cmp_spark, spark_delay_us), "set_compare_spark", cylinder_id - 1)) {
        return false;
    }

    if (!mcpwm_ok(mcpwm_generator_set_force_level(ch->gen, -1, false), "generator_set_force_level", cylinder_id - 1)) {
        return false;
    }

    if (!mcpwm_ok(mcpwm_timer_start_stop(ch->timer, MCPWM_TIMER_START_STOP_FULL), "timer_start_stop", cylinder_id - 1)) {
        return false;
    }

    ch->current_dwell_ms = dwell_time_ms;
    ch->is_active = true;
    return true;
}

bool mcpwm_ignition_stop_cylinder(uint8_t cylinder_id) {
    if (!g_initialized || cylinder_id < 1 || cylinder_id > 4) {
        return false;
    }

    mcpwm_ign_channel_t *ch = &g_channels[cylinder_id - 1];
    if (!mcpwm_ok(mcpwm_timer_start_stop(ch->timer, MCPWM_TIMER_STOP_EMPTY), "timer_stop", cylinder_id - 1)) {
        return false;
    }
    if (!mcpwm_ok(mcpwm_generator_set_force_level(ch->gen, 0, true), "generator_force_low", cylinder_id - 1)) {
        return false;
    }
    ch->is_active = false;
    return true;
}

bool mcpwm_ignition_get_status(uint8_t cylinder_id, mcpwm_ignition_status_t *status) {
    if (!g_initialized || cylinder_id < 1 || cylinder_id > 4 || status == NULL) {
        return false;
    }

    mcpwm_ign_channel_t *ch = &g_channels[cylinder_id - 1];
    status->is_active = ch->is_active;
    status->current_dwell_ms = ch->current_dwell_ms;
    status->coil_pin = ch->coil_pin;
    return true;
}

bool mcpwm_ignition_deinit(void) {
    for (int i = 0; i < 4; i++) {
        if (g_channels[i].timer) {
            mcpwm_timer_disable(g_channels[i].timer);
            mcpwm_del_timer(g_channels[i].timer);
            g_channels[i].timer = NULL;
        }
        if (g_channels[i].gen) {
            mcpwm_del_generator(g_channels[i].gen);
            g_channels[i].gen = NULL;
        }
        if (g_channels[i].cmp_dwell) {
            mcpwm_del_comparator(g_channels[i].cmp_dwell);
            g_channels[i].cmp_dwell = NULL;
        }
        if (g_channels[i].cmp_spark) {
            mcpwm_del_comparator(g_channels[i].cmp_spark);
            g_channels[i].cmp_spark = NULL;
        }
        if (g_channels[i].oper) {
            mcpwm_del_operator(g_channels[i].oper);
            g_channels[i].oper = NULL;
        }
    }

    g_initialized = false;
    for (int i = 0; i < 4; i++) {
        g_channels[i].current_dwell_ms = 0.0f;
        g_channels[i].is_active = false;
    }
    return true;
}
