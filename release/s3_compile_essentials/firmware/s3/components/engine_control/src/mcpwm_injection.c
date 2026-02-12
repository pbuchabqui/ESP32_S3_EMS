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

static const char* TAG = "MCPWM_INJECTION";

typedef struct {
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t cmp_start;
    mcpwm_cmpr_handle_t cmp_end;
    mcpwm_gen_handle_t gen;
    gpio_num_t gpio;
    uint32_t pulsewidth_us;
    bool is_active;
} mcpwm_injection_channel_t;

static mcpwm_injection_channel_t g_channels[4];

static mcpwm_injection_config_t g_cfg = {
    .base_frequency_hz = 1000000,
    .timer_resolution_bits = 20,
    .min_pulsewidth_us = 500,
    .max_pulsewidth_us = 18000,
    .deadtime_us = 200,
};

static bool g_initialized = false;

static uint32_t clamp_u32(uint32_t v, uint32_t min_v, uint32_t max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

bool mcpwm_injection_init(void) {
    if (g_initialized) {
        return true;
    }

    // GPIO mapping (from board config)
    const gpio_num_t gpios[4] = {
        INJECTOR_GPIO_1,
        INJECTOR_GPIO_2,
        INJECTOR_GPIO_3,
        INJECTOR_GPIO_4
    };

    for (int i = 0; i < 4; i++) {
        int group_id = i / SOC_MCPWM_TIMERS_PER_GROUP;
        if (group_id >= SOC_MCPWM_GROUPS) {
            ESP_LOGE(TAG, "No MCPWM group available for injector %d", i);
            return false;
        }

        g_channels[i].gpio = gpios[i];
        g_channels[i].pulsewidth_us = 0;
        g_channels[i].is_active = false;

        mcpwm_timer_config_t timer_cfg = {
            .group_id = group_id,
            .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
            .resolution_hz = g_cfg.base_frequency_hz,
            .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
            .period_ticks = g_cfg.max_pulsewidth_us + g_cfg.deadtime_us + 10,
            .intr_priority = 0,
            .flags = {
                .update_period_on_empty = 1,
            },
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_timer(&timer_cfg, &g_channels[i].timer));

        mcpwm_operator_config_t oper_cfg = {
            .group_id = group_id,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_operator(&oper_cfg, &g_channels[i].oper));
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_operator_connect_timer(g_channels[i].oper, g_channels[i].timer));

        mcpwm_comparator_config_t cmpr_cfg = {
            .flags = {
                .update_cmp_on_tez = 1,
            },
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_comparator(g_channels[i].oper, &cmpr_cfg, &g_channels[i].cmp_start));
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_comparator(g_channels[i].oper, &cmpr_cfg, &g_channels[i].cmp_end));

        mcpwm_generator_config_t gen_cfg = {
            .gen_gpio_num = g_channels[i].gpio,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_generator(g_channels[i].oper, &gen_cfg, &g_channels[i].gen));

        // Default: keep low when idle
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_generator_set_force_level(g_channels[i].gen, 0, true));

        // Actions: keep LOW at timer events, use compare for delayed start/end
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_generator_set_actions_on_timer_event(
            g_channels[i].gen,
            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW),
            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_FULL, MCPWM_GEN_ACTION_LOW),
            MCPWM_GEN_TIMER_EVENT_ACTION_END()
        ));

        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_generator_set_actions_on_compare_event(
            g_channels[i].gen,
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_channels[i].cmp_start, MCPWM_GEN_ACTION_HIGH),
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_channels[i].cmp_end, MCPWM_GEN_ACTION_LOW),
            MCPWM_GEN_COMPARE_EVENT_ACTION_END()
        ));

        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_enable(g_channels[i].timer));
    }

    g_initialized = true;
    ESP_LOGI(TAG, "MCPWM injection system initialized");
    return true;
}

bool mcpwm_injection_configure(const mcpwm_injection_config_t *config) {
    if (config == NULL) {
        return false;
    }

    g_cfg = *config;
    return true;
}

bool mcpwm_injection_apply(uint8_t cylinder_id, uint32_t pulsewidth_us) {
    if (!g_initialized || cylinder_id >= 4) {
        return false;
    }

    mcpwm_injection_channel_t *ch = &g_channels[cylinder_id];
    uint32_t pw = clamp_u32(pulsewidth_us, g_cfg.min_pulsewidth_us, g_cfg.max_pulsewidth_us);
    uint32_t period = pw + g_cfg.deadtime_us + 1;

    if (mcpwm_timer_set_period(ch->timer, period) != ESP_OK) {
        return false;
    }
    if (mcpwm_comparator_set_compare_value(ch->cmp_start, 0) != ESP_OK) {
        return false;
    }
    if (mcpwm_comparator_set_compare_value(ch->cmp_end, pw) != ESP_OK) {
        return false;
    }

    // Release force level so PWM actions take effect
    mcpwm_generator_set_force_level(ch->gen, -1, false);

    if (mcpwm_timer_start_stop(ch->timer, MCPWM_TIMER_START_STOP_FULL) != ESP_OK) {
        return false;
    }

    ch->pulsewidth_us = pw;
    ch->is_active = true;
    return true;
}

bool mcpwm_injection_apply_sequential(const uint32_t pulsewidth_us[4]) {
    if (pulsewidth_us == NULL) {
        return false;
    }

    for (int i = 0; i < 4; i++) {
        if (!mcpwm_injection_apply((uint8_t)i, pulsewidth_us[i])) {
            return false;
        }
        // Basic dead time between sequential injections
        esp_rom_delay_us(g_cfg.deadtime_us);
    }
    return true;
}

bool mcpwm_injection_apply_simultaneous(const uint32_t pulsewidth_us[4]) {
    if (pulsewidth_us == NULL) {
        return false;
    }

    for (int i = 0; i < 4; i++) {
        if (!mcpwm_injection_apply((uint8_t)i, pulsewidth_us[i])) {
            return false;
        }
    }
    return true;
}

bool mcpwm_injection_schedule_one_shot(uint8_t cylinder_id, uint32_t delay_us, uint32_t pulsewidth_us) {
    if (!g_initialized || cylinder_id >= 4) {
        return false;
    }

    mcpwm_injection_channel_t *ch = &g_channels[cylinder_id];
    uint32_t pw = clamp_u32(pulsewidth_us, g_cfg.min_pulsewidth_us, g_cfg.max_pulsewidth_us);
    uint32_t start_ticks = delay_us;
    uint32_t end_ticks = delay_us + pw;
    uint32_t period = end_ticks + 1;

    if (mcpwm_timer_set_period(ch->timer, period) != ESP_OK) {
        return false;
    }
    if (mcpwm_comparator_set_compare_value(ch->cmp_start, start_ticks) != ESP_OK) {
        return false;
    }
    if (mcpwm_comparator_set_compare_value(ch->cmp_end, end_ticks) != ESP_OK) {
        return false;
    }

    mcpwm_generator_set_force_level(ch->gen, -1, false);

    if (mcpwm_timer_start_stop(ch->timer, MCPWM_TIMER_START_STOP_FULL) != ESP_OK) {
        return false;
    }

    ch->pulsewidth_us = pw;
    ch->is_active = true;
    return true;
}

bool mcpwm_injection_stop(uint8_t cylinder_id) {
    if (!g_initialized || cylinder_id >= 4) {
        return false;
    }

    mcpwm_injection_channel_t *ch = &g_channels[cylinder_id];
    mcpwm_timer_start_stop(ch->timer, MCPWM_TIMER_STOP_EMPTY);
    mcpwm_generator_set_force_level(ch->gen, 0, true);
    ch->pulsewidth_us = 0;
    ch->is_active = false;
    return true;
}

bool mcpwm_injection_stop_all(void) {
    for (int i = 0; i < 4; i++) {
        if (!mcpwm_injection_stop((uint8_t)i)) {
            return false;
        }
    }
    return true;
}

bool mcpwm_injection_get_status(uint8_t cylinder_id, mcpwm_injector_channel_t *status) {
    if (!g_initialized || cylinder_id >= 4 || status == NULL) {
        return false;
    }

    mcpwm_injection_channel_t *ch = &g_channels[cylinder_id];
    status->channel_id = cylinder_id;
    status->gpio = ch->gpio;
    status->pulsewidth_us = ch->pulsewidth_us;
    status->is_active = ch->is_active;
    return true;
}

const mcpwm_injection_config_t* mcpwm_injection_get_config(void) {
    return &g_cfg;
}

bool mcpwm_injection_test(uint8_t cylinder_id, uint32_t duration_ms) {
    uint32_t pw = duration_ms * 1000;
    return mcpwm_injection_apply(cylinder_id, pw);
}

bool mcpwm_injection_deinit(void) {
    if (!g_initialized) {
        return true;
    }

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
    if (g_channels[i].cmp_start) {
        mcpwm_del_comparator(g_channels[i].cmp_start);
        g_channels[i].cmp_start = NULL;
    }
    if (g_channels[i].cmp_end) {
        mcpwm_del_comparator(g_channels[i].cmp_end);
        g_channels[i].cmp_end = NULL;
    }
        if (g_channels[i].oper) {
            mcpwm_del_operator(g_channels[i].oper);
            g_channels[i].oper = NULL;
        }
    }

    g_initialized = false;
    return true;
}
