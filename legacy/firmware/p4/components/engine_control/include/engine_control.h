#ifndef ENGINE_CONTROL_H
#define ENGINE_CONTROL_H

#include "esp_err.h"
#include "lp_core.h"
#include "sensor_processing.h"

// Engine control status
typedef enum {
    ENGINE_CONTROL_OK = 0,
    ENGINE_CONTROL_ERROR = -1,
    ENGINE_CONTROL_NOT_INITIALIZED = -2,
    ENGINE_CONTROL_ALREADY_RUNNING = -3,
    ENGINE_CONTROL_INVALID_STATE = -4
} engine_control_status_t;

// Core synchronization data
typedef struct {
    uint32_t heartbeat;
    uint32_t last_sync_time;
    uint32_t error_count;
    bool is_sync_acquired;
    uint32_t handover_rpm;
    uint32_t handover_advance;
} core_sync_data_t;

// Engine parameters
typedef struct {
    uint32_t rpm;
    uint32_t load;
    uint16_t advance_deg10;
    uint16_t fuel_enrichment;
    bool is_limp_mode;
} engine_params_t;

// Function prototypes
esp_err_t engine_control_init(void);
esp_err_t engine_control_start(void);
esp_err_t engine_control_stop(void);
esp_err_t engine_control_handle_sync_event(uint32_t tooth_period_us, bool is_gap, bool is_phase);
esp_err_t engine_control_get_lp_core_state(lp_core_state_t *state);
esp_err_t engine_control_perform_handover(void);
esp_err_t engine_control_deinit(void);
esp_err_t engine_control_get_sensor_data(sensor_data_t *data);
esp_err_t engine_control_get_engine_parameters(engine_params_t *params);
esp_err_t engine_control_get_core_sync_data(core_sync_data_t *sync_data);
esp_err_t engine_control_update_core_sync_data(const core_sync_data_t *sync_data);
esp_err_t engine_control_run_cycle(void);
esp_err_t engine_control_set_eoi_config(float eoi_deg, float eoi_fallback_deg);
esp_err_t engine_control_get_eoi_config(float *eoi_deg, float *eoi_fallback_deg);
bool engine_control_is_limp_mode(void);
void engine_control_set_closed_loop_enabled(bool enabled);
bool engine_control_get_closed_loop_enabled(void);

#endif // ENGINE_CONTROL_H
