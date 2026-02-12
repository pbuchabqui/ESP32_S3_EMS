#ifndef FUEL_CALC_H
#define FUEL_CALC_H

#include <stdint.h>
#include <stdbool.h>
#include "p4_control_config.h"
#include "sensor_processing.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    table_16x16_t fuel_table;
    table_16x16_t ignition_table;
    table_16x16_t lambda_table;
} fuel_calc_maps_t;

void fuel_calc_init_defaults(fuel_calc_maps_t *maps);

uint16_t fuel_calc_lookup_ve(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load);
uint16_t fuel_calc_lookup_ignition(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load);
uint16_t fuel_calc_lookup_lambda(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load);

uint32_t fuel_calc_pulsewidth_us(const sensor_data_t *sensors,
                                 uint16_t rpm,
                                 uint16_t ve_x10,
                                 float lambda_correction);

uint16_t fuel_calc_warmup_enrichment(const sensor_data_t *sensors);

#ifdef __cplusplus
}
#endif

#endif // FUEL_CALC_H
