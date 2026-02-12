#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

#define SIM_DURATION_SEC         30U
#define SIM_TOOTH_COUNT          58U    // 60-2
#define PLAN_RING_SIZE           32U
#define PERF_WINDOW              512U
#define DEADLINE_US              700U
#define HARD_TARGET_US           600U

#define LOAD_TASK_ENABLED        1
#define LOAD_TASK_DUTY_US        220U

typedef struct {
    uint32_t rpm;
    uint16_t map_kpa10;
    uint16_t tps_percent;
    uint16_t o2_mv;
    int16_t clt_c;
} sim_sensor_t;

typedef struct {
    uint32_t rpm;
    uint16_t load_kpa10;
    uint16_t advance_deg10;
    uint32_t pulsewidth_us;
    uint32_t planned_at_us;
} sim_plan_t;

typedef struct {
    sim_plan_t items[PLAN_RING_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
    volatile uint32_t overruns;
    portMUX_TYPE mux;
} sim_ring_t;

typedef struct {
    uint32_t planner_samples[PERF_WINDOW];
    uint32_t executor_samples[PERF_WINDOW];
    uint16_t idx;
    uint16_t count;
    uint32_t planner_max;
    uint32_t executor_max;
    uint32_t planner_miss_700;
    uint32_t planner_miss_600;
    uint32_t executor_miss_700;
    uint32_t executor_miss_600;
    uint32_t queue_age_max;
    uint32_t queue_age_miss_700;
    uint32_t queue_age_miss_600;
} sim_perf_t;

static const char *TAG = "S3_SIM";
static volatile bool g_sim_running = true;
static volatile uint32_t g_sensor_seq = 0;
static sim_sensor_t g_sensor = {0};

static TaskHandle_t g_planner_task = NULL;
static TaskHandle_t g_executor_task = NULL;
static esp_timer_handle_t g_tooth_timer = NULL;
static volatile uint32_t g_tooth_index = 0;
static volatile uint32_t g_rev_count = 0;

static sim_ring_t g_ring = {
    .head = 0,
    .tail = 0,
    .overruns = 0,
    .mux = portMUX_INITIALIZER_UNLOCKED,
};
static sim_perf_t g_perf = {0};

static inline uint32_t abs_u32_diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static uint32_t percentile_u32(const uint32_t *arr, uint16_t n, uint8_t pct) {
    if (!arr || n == 0) {
        return 0;
    }
    uint32_t copy[PERF_WINDOW] = {0};
    if (n > PERF_WINDOW) {
        n = PERF_WINDOW;
    }
    memcpy(copy, arr, sizeof(uint32_t) * n);
    for (uint16_t i = 0; i < n; i++) {
        for (uint16_t j = i + 1; j < n; j++) {
            if (copy[j] < copy[i]) {
                uint32_t t = copy[i];
                copy[i] = copy[j];
                copy[j] = t;
            }
        }
    }
    uint16_t idx = (uint16_t)(((uint32_t)(n - 1U) * pct) / 100U);
    return copy[idx];
}

static uint32_t tooth_period_us_at_rpm(uint32_t rpm) {
    if (rpm == 0) {
        return 0;
    }
    return (uint32_t)(60000000UL / (rpm * 60UL));
}

static void sim_sensor_write(const sim_sensor_t *s) {
    __atomic_fetch_add(&g_sensor_seq, 1U, __ATOMIC_RELEASE);
    g_sensor = *s;
    __atomic_fetch_add(&g_sensor_seq, 1U, __ATOMIC_RELEASE);
}

static bool sim_sensor_read(sim_sensor_t *out) {
    if (!out) {
        return false;
    }
    for (uint8_t i = 0; i < 8; i++) {
        uint32_t seq1 = __atomic_load_n(&g_sensor_seq, __ATOMIC_ACQUIRE);
        if (seq1 & 1U) {
            continue;
        }
        *out = g_sensor;
        uint32_t seq2 = __atomic_load_n(&g_sensor_seq, __ATOMIC_ACQUIRE);
        if (seq1 == seq2) {
            return true;
        }
    }
    return false;
}

static void ring_push(const sim_plan_t *cmd) {
    portENTER_CRITICAL(&g_ring.mux);
    uint8_t next = (uint8_t)((g_ring.head + 1U) % PLAN_RING_SIZE);
    if (next == g_ring.tail) {
        g_ring.tail = (uint8_t)((g_ring.tail + 1U) % PLAN_RING_SIZE);
        g_ring.overruns++;
    }
    g_ring.items[g_ring.head] = *cmd;
    g_ring.head = next;
    portEXIT_CRITICAL(&g_ring.mux);
}

static bool ring_pop(sim_plan_t *cmd) {
    bool ok = false;
    portENTER_CRITICAL(&g_ring.mux);
    if (g_ring.tail != g_ring.head) {
        *cmd = g_ring.items[g_ring.tail];
        g_ring.tail = (uint8_t)((g_ring.tail + 1U) % PLAN_RING_SIZE);
        ok = true;
    }
    portEXIT_CRITICAL(&g_ring.mux);
    return ok;
}

static void perf_record_planner(uint32_t us) {
    uint16_t idx = g_perf.idx % PERF_WINDOW;
    g_perf.planner_samples[idx] = us;
    if (us > g_perf.planner_max) {
        g_perf.planner_max = us;
    }
    if (us > DEADLINE_US) {
        g_perf.planner_miss_700++;
    }
    if (us > HARD_TARGET_US) {
        g_perf.planner_miss_600++;
    }
}

static void perf_record_executor(uint32_t exec_us, uint32_t queue_age_us) {
    uint16_t idx = g_perf.idx % PERF_WINDOW;
    g_perf.executor_samples[idx] = exec_us;
    if (exec_us > g_perf.executor_max) {
        g_perf.executor_max = exec_us;
    }
    if (queue_age_us > g_perf.queue_age_max) {
        g_perf.queue_age_max = queue_age_us;
    }
    if (exec_us > DEADLINE_US) {
        g_perf.executor_miss_700++;
    }
    if (exec_us > HARD_TARGET_US) {
        g_perf.executor_miss_600++;
    }
    if (queue_age_us > DEADLINE_US) {
        g_perf.queue_age_miss_700++;
    }
    if (queue_age_us > HARD_TARGET_US) {
        g_perf.queue_age_miss_600++;
    }
    g_perf.idx = (uint16_t)((g_perf.idx + 1U) % PERF_WINDOW);
    if (g_perf.count < PERF_WINDOW) {
        g_perf.count++;
    }
}

static void simulate_cpu_work_us(uint32_t budget_us) {
    int64_t t0 = esp_timer_get_time();
    while ((uint32_t)(esp_timer_get_time() - t0) < budget_us) {
        __asm__ volatile("nop");
    }
}

static void tooth_timer_cb(void *arg) {
    (void)arg;
    if (!g_sim_running) {
        return;
    }

    sim_sensor_t s = {0};
    if (!sim_sensor_read(&s)) {
        return;
    }

    uint32_t period_us = tooth_period_us_at_rpm(s.rpm);
    if (period_us == 0U) {
        period_us = 1000U;
    }

    g_tooth_index++;
    if (g_tooth_index >= (SIM_TOOTH_COUNT + 2U)) {
        g_tooth_index = 0;
        g_rev_count++;
    }

    uint32_t next_period = period_us;
    if (g_tooth_index == SIM_TOOTH_COUNT) {
        next_period = period_us * 3U;
    }

    if (g_planner_task != NULL) {
        xTaskNotifyGive(g_planner_task);
    }
    esp_timer_start_once(g_tooth_timer, next_period);
}

static void input_sim_task(void *arg) {
    (void)arg;
    int64_t t0 = esp_timer_get_time();
    while (g_sim_running) {
        uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000ULL);
        sim_sensor_t s = {0};

        if (ms < 10000U) {
            s.rpm = 900U;
            s.map_kpa10 = 350U;
            s.tps_percent = 4U;
            s.o2_mv = 450U;
            s.clt_c = 75;
        } else if (ms < 20000U) {
            uint32_t p = ms - 10000U;
            s.rpm = 900U + (p * 36U) / 10U;      // ~900 -> 4500
            s.map_kpa10 = 350U + (p * 55U) / 100U; // ~35 -> 90 kPa
            s.tps_percent = 4U + (p * 56U) / 1000U;
            s.o2_mv = (p & 0x100U) ? 420U : 480U;
            s.clt_c = 82;
        } else {
            s.rpm = 5200U + ((ms / 200U) & 0x1U) * 300U;
            s.map_kpa10 = 1100U + ((ms / 100U) & 0x1U) * 120U;
            s.tps_percent = 72U + ((ms / 150U) & 0x1U) * 8U;
            s.o2_mv = ((ms / 120U) & 0x1U) ? 430U : 470U;
            s.clt_c = 90;
        }

        sim_sensor_write(&s);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}

static void planner_task(void *arg) {
    (void)arg;
    while (g_sim_running) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) == 0U) {
            continue;
        }
        uint32_t t0 = (uint32_t)esp_timer_get_time();

        sim_sensor_t s = {0};
        if (!sim_sensor_read(&s)) {
            continue;
        }

        // Simplified ECU planning model.
        sim_plan_t cmd = {0};
        cmd.rpm = s.rpm;
        cmd.load_kpa10 = s.map_kpa10;
        cmd.advance_deg10 = (uint16_t)(100U + (s.rpm / 200U)); // coarse advance curve
        uint32_t ve = 850U + (s.map_kpa10 / 4U);               // pseudo VE x10
        uint32_t pw = (ve * s.map_kpa10) / 1000U;
        if (pw < 500U) pw = 500U;
        if (pw > 18000U) pw = 18000U;
        cmd.pulsewidth_us = pw;
        cmd.planned_at_us = (uint32_t)esp_timer_get_time();

        // Simulate math + map lookup + corrections workload.
        simulate_cpu_work_us(180U + abs_u32_diff(s.rpm, 3000U) / 40U);

        ring_push(&cmd);
        if (g_executor_task != NULL) {
            xTaskNotifyGive(g_executor_task);
        }

        uint32_t elapsed = (uint32_t)esp_timer_get_time() - t0;
        perf_record_planner(elapsed);
    }
    vTaskDelete(NULL);
}

static void executor_task(void *arg) {
    (void)arg;
    while (g_sim_running) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) == 0U) {
            continue;
        }

        sim_plan_t cmd = {0};
        while (ring_pop(&cmd)) {
            uint32_t t0 = (uint32_t)esp_timer_get_time();
            uint32_t queue_age = t0 - cmd.planned_at_us;

            // Simulate scheduling actuators in HW timing layer.
            simulate_cpu_work_us(45U + (cmd.rpm > 4500U ? 15U : 0U));

            uint32_t elapsed = (uint32_t)esp_timer_get_time() - t0;
            perf_record_executor(elapsed, queue_age);
        }
    }
    vTaskDelete(NULL);
}

#if LOAD_TASK_ENABLED
static void load_task(void *arg) {
    (void)arg;
    while (g_sim_running) {
        simulate_cpu_work_us(LOAD_TASK_DUTY_US);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}
#endif

static void report_task(void *arg) {
    (void)arg;
    int64_t t0 = esp_timer_get_time();
    while ((uint32_t)((esp_timer_get_time() - t0) / 1000000ULL) < SIM_DURATION_SEC) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        sim_sensor_t s = {0};
        (void)sim_sensor_read(&s);
        ESP_LOGI(TAG, "sim t=%u s rpm=%u map=%.1f tps=%u%% queue_ovr=%u",
                 (uint32_t)((esp_timer_get_time() - t0) / 1000000ULL),
                 s.rpm, s.map_kpa10 / 10.0f, s.tps_percent, g_ring.overruns);
    }

    g_sim_running = false;
    esp_timer_stop(g_tooth_timer);

    uint16_t n = g_perf.count;
    uint32_t p95_plan = percentile_u32(g_perf.planner_samples, n, 95);
    uint32_t p99_plan = percentile_u32(g_perf.planner_samples, n, 99);
    uint32_t p95_exec = percentile_u32(g_perf.executor_samples, n, 95);
    uint32_t p99_exec = percentile_u32(g_perf.executor_samples, n, 99);

    ESP_LOGI(TAG, "----- Simulation Summary (%u s) -----", SIM_DURATION_SEC);
    ESP_LOGI(TAG, "Samples=%u rev_count=%u tooth_idx=%u", n, g_rev_count, g_tooth_index);
    ESP_LOGI(TAG, "Planner(us): p95=%u p99=%u max=%u miss700=%u miss600=%u",
             p95_plan, p99_plan, g_perf.planner_max, g_perf.planner_miss_700, g_perf.planner_miss_600);
    ESP_LOGI(TAG, "Executor(us): p95=%u p99=%u max=%u miss700=%u miss600=%u",
             p95_exec, p99_exec, g_perf.executor_max, g_perf.executor_miss_700, g_perf.executor_miss_600);
    ESP_LOGI(TAG, "Queue age(us): max=%u miss700=%u miss600=%u overruns=%u",
             g_perf.queue_age_max, g_perf.queue_age_miss_700, g_perf.queue_age_miss_600, g_ring.overruns);

    bool pass_700 = (g_perf.planner_miss_700 == 0U) &&
                    (g_perf.executor_miss_700 == 0U) &&
                    (g_perf.queue_age_miss_700 == 0U);
    bool pass_600 = (g_perf.planner_miss_600 == 0U) &&
                    (g_perf.executor_miss_600 == 0U) &&
                    (g_perf.queue_age_miss_600 == 0U);
    ESP_LOGI(TAG, "Verdict <=700us: %s | <=600us: %s",
             pass_700 ? "PASS" : "FAIL",
             pass_600 ? "PASS" : "FAIL");

    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting S3 virtual input simulation");

    sim_sensor_t init = {
        .rpm = 900U,
        .map_kpa10 = 350U,
        .tps_percent = 4U,
        .o2_mv = 450U,
        .clt_c = 75,
    };
    sim_sensor_write(&init);

    esp_timer_create_args_t t_args = {
        .callback = tooth_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "tooth_sim",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&t_args, &g_tooth_timer));

    xTaskCreatePinnedToCore(input_sim_task, "input_sim", 4096, NULL, 7, NULL, 0);
    xTaskCreatePinnedToCore(planner_task, "planner", 4096, NULL, 10, &g_planner_task, 1);
    xTaskCreatePinnedToCore(executor_task, "executor", 4096, NULL, 10, &g_executor_task, 1);
#if LOAD_TASK_ENABLED
    xTaskCreatePinnedToCore(load_task, "load", 3072, NULL, 8, NULL, 0);
#endif
    xTaskCreatePinnedToCore(report_task, "report", 4096, NULL, 5, NULL, 0);

    uint32_t period_us = tooth_period_us_at_rpm(init.rpm);
    ESP_ERROR_CHECK(esp_timer_start_once(g_tooth_timer, period_us));
}
