#include "../include/fuel_calc.h"
#include "../include/table_16x16.h"
#include <math.h>

void fuel_calc_init_defaults(fuel_calc_maps_t *maps) {
    if (!maps) {
        return;
    }

    table_16x16_init(&maps->fuel_table, NULL, NULL, 1000);    // 100.0% VE
    table_16x16_init(&maps->ignition_table, NULL, NULL, 150); // 15.0 deg
    table_16x16_init(&maps->lambda_table, NULL, NULL, 1000);  // 1.000 lambda
}

uint16_t fuel_calc_lookup_ve(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load) {
    if (!maps) {
        return 0;
    }
    return table_16x16_interpolate(&maps->fuel_table, rpm, load);
}

uint16_t fuel_calc_lookup_ignition(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load) {
    if (!maps) {
        return 0;
    }
    return table_16x16_interpolate(&maps->ignition_table, rpm, load);
}

uint16_t fuel_calc_lookup_lambda(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load) {
    if (!maps) {
        return 0;
    }
    return table_16x16_interpolate(&maps->lambda_table, rpm, load);
}

uint16_t fuel_calc_warmup_enrichment(const sensor_data_t *sensors) {
    if (!sensors) {
        return 100;
    }

    if (sensors->clt_c <= WARMUP_TEMP_MIN) {
        return WARMUP_ENRICH_MAX;
    }
    if (sensors->clt_c >= WARMUP_TEMP_MAX) {
        return 100;
    }

    float range = (float)(WARMUP_TEMP_MAX - WARMUP_TEMP_MIN);
    float pos = (float)(sensors->clt_c - WARMUP_TEMP_MIN) / range;
    float enrich = WARMUP_ENRICH_MAX - (WARMUP_ENRICH_MAX - 100.0f) * pos;
    return (uint16_t)(enrich + 0.5f);
}

uint32_t fuel_calc_pulsewidth_us(const sensor_data_t *sensors,
                                 uint16_t rpm,
                                 uint16_t ve_x10,
                                 float lambda_correction) {
    if (!sensors || rpm == 0) {
        return PW_MIN_US;
    }

    float ve = ve_x10 / 10.0f;
    float map_kpa = sensors->map_kpa10 / 10.0f;
    float load_factor = map_kpa / 100.0f;

    float base_pw = (float)REQ_FUEL_US * (ve / 100.0f) * load_factor;

    uint16_t warmup = fuel_calc_warmup_enrichment(sensors);
    float warmup_factor = warmup / 100.0f;

    float lambda_factor = 1.0f + lambda_correction;
    if (lambda_factor < 0.75f) {
        lambda_factor = 0.75f;
    } else if (lambda_factor > 1.25f) {
        lambda_factor = 1.25f;
    }

    float pw = base_pw * warmup_factor * lambda_factor;
    if (pw < PW_MIN_US) {
        pw = PW_MIN_US;
    } else if (pw > PW_MAX_US) {
        pw = PW_MAX_US;
    }

    return (uint32_t)(pw + 0.5f);
}
