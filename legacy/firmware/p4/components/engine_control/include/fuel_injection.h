#ifndef FUEL_INJECTION_H
#define FUEL_INJECTION_H

#include <stdint.h>
#include <stdbool.h>
#include "sync.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float cyl_tdc_deg[4];      // TDC angle for each cylinder (0-720)
} fuel_injection_config_t;

// Initialize fuel injection scheduling
void fuel_injection_init(const fuel_injection_config_t *config);

// Schedule injection using EOI (End of Injection) logic
bool fuel_injection_schedule_eoi(uint8_t cylinder_id,
                                 float target_eoi_deg,
                                 uint32_t pulsewidth_us,
                                 const sync_data_t *sync);

#ifdef __cplusplus
}
#endif

#endif // FUEL_INJECTION_H
