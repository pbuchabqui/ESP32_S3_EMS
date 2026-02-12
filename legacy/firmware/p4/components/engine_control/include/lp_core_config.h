#ifndef LP_CORE_CONFIG_H
#define LP_CORE_CONFIG_H

#include "esp_system.h"
#include "esp_err.h"

// LP Core configuration defaults
#define LP_CORE_DEFAULT_CRANKING_RPM_THRESHOLD    500     // RPM threshold for cranking detection
#define LP_CORE_DEFAULT_PRIME_PULSE_DURATION_US   2000    // Prime pulse duration in microseconds
#define LP_CORE_DEFAULT_PRIME_PULSE_VOLTAGE       135     // Battery voltage for prime pulse calculation (13.5V)
#define LP_CORE_DEFAULT_CRANKING_FUEL_ENRICHMENT  140     // Fuel enrichment during cranking (%)
#define LP_CORE_DEFAULT_CRANKING_TIMING_ADVANCE   100     // Timing advance during cranking (degrees * 10)
#define LP_CORE_DEFAULT_CRANKING_RPM_LIMIT        3000    // RPM limit during cranking
#define LP_CORE_DEFAULT_CRANKING_TIMEOUT_MS       5000    // Maximum cranking time before timeout
#define LP_CORE_DEFAULT_SYNC_TIMEOUT_MS           2000    // Maximum time to wait for sync
#define LP_CORE_DEFAULT_PRIME_PULSE_COUNT         3       // Number of prime pulses to deliver

// LP Core configuration structure
typedef struct {
    uint32_t cranking_rpm_threshold;    // RPM threshold for cranking detection
    uint32_t prime_pulse_duration_us;   // Prime pulse duration in microseconds
    uint32_t prime_pulse_voltage;       // Battery voltage for prime pulse calculation
    uint32_t cranking_fuel_enrichment;  // Fuel enrichment during cranking (%)
    uint32_t cranking_timing_advance;   // Timing advance during cranking (degrees * 10)
    uint32_t cranking_rpm_limit;        // RPM limit during cranking
    uint32_t cranking_timeout_ms;       // Maximum cranking time before timeout
    uint32_t sync_timeout_ms;           // Maximum time to wait for sync
    uint32_t prime_pulse_count;         // Number of prime pulses to deliver
} lp_core_config_t;

// Function prototypes
esp_err_t lp_core_load_config(lp_core_config_t *config);
esp_err_t lp_core_save_config(const lp_core_config_t *config);
esp_err_t lp_core_set_default_config(lp_core_config_t *config);

#endif // LP_CORE_CONFIG_H
