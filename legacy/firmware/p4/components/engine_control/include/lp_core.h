#ifndef LP_CORE_H
#define LP_CORE_H

#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lp_core_config.h"
#include "p4_control_config.h"

// LP Core state
typedef struct {
    bool is_cranking;                   // Cranking state
    bool is_prime_pulse_active;         // Prime pulse active state
    bool is_sync_acquired;              // Sync acquisition state
    bool is_handover_complete;          // Handover completion state
    uint32_t current_rpm;               // Current RPM during cranking
    uint32_t prime_pulse_counter;       // Prime pulse counter
    uint32_t cranking_start_time;       // Cranking start timestamp
    uint32_t last_sync_time;            // Last sync timestamp
    uint32_t tooth_counter;             // Tooth counter for sync
    uint32_t gap_detected;              // Gap detection flag
    uint32_t phase_detected;            // Phase detection flag
    SemaphoreHandle_t state_mutex;      // Mutex for state protection
} lp_core_state_t;

// LP Core handover data
typedef struct {
    uint32_t handover_rpm;              // RPM at handover
    uint32_t handover_timing_advance;   // Timing advance at handover
    uint32_t handover_fuel_enrichment;  // Fuel enrichment at handover
    uint32_t handover_sync_status;      // Sync status at handover
    uint32_t handover_tooth_counter;    // Tooth counter at handover
    uint32_t handover_gap_detected;     // Gap detection at handover
    uint32_t handover_phase_detected;   // Phase detection at handover
} lp_core_handover_data_t;

// Function prototypes
esp_err_t lp_core_init(const lp_core_config_t *config);
esp_err_t lp_core_start_cranking(void);
esp_err_t lp_core_stop_cranking(void);
esp_err_t lp_core_handle_sync_event(uint32_t tooth_period_us, bool is_gap, bool is_phase);
esp_err_t lp_core_get_state(lp_core_state_t *state);
esp_err_t lp_core_get_handover_data(lp_core_handover_data_t *handover_data);
esp_err_t lp_core_perform_handover(void);
esp_err_t lp_core_check_cranking_timeout(void);
esp_err_t lp_core_reset_state(void);
esp_err_t lp_core_deinit(void);

#endif // LP_CORE_H
