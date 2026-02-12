#include "../include/fuel_calc.h"
#include "../include/table_16x16.h"
#include <math.h>
#include <string.h>

typedef struct {
    uint16_t last_rpm;
    uint16_t last_load;
    uint16_t last_result;
    uint16_t table_checksum;
    bool valid;
} interp_cache_t;

static interp_cache_t g_fuel_cache = {0};
static interp_cache_t g_ign_cache = {0};
static interp_cache_t g_lambda_cache = {0};

static uint16_t abs_u16_delta(uint16_t a, uint16_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static uint16_t lookup_with_cache(const table_16x16_t *table,
                                  interp_cache_t *cache,
                                  uint16_t rpm,
                                  uint16_t load) {
    if (!table || !cache) {
        return 0;
    }

    if (cache->valid &&
        cache->table_checksum == table->checksum &&
        abs_u16_delta(rpm, cache->last_rpm) <= INTERP_CACHE_RPM_DEADBAND &&
        abs_u16_delta(load, cache->last_load) <= INTERP_CACHE_LOAD_DEADBAND) {
        return cache->last_result;
    }

    uint16_t result = table_16x16_interpolate(table, rpm, load);
    cache->last_rpm = rpm;
    cache->last_load = load;
    cache->last_result = result;
    cache->table_checksum = table->checksum;
    cache->valid = true;
    return result;
}

void fuel_calc_init_defaults(fuel_calc_maps_t *maps) {
    if (!maps) {
        return;
    }

    table_16x16_init(&maps->fuel_table, NULL, NULL, 1000);    // 100.0% VE
    table_16x16_init(&maps->ignition_table, NULL, NULL, 150); // 15.0 deg
    table_16x16_init(&maps->lambda_table, NULL, NULL, 1000);  // 1.000 lambda
    fuel_calc_reset_interpolation_cache();
}

void fuel_calc_reset_interpolation_cache(void) {
    memset(&g_fuel_cache, 0, sizeof(g_fuel_cache));
    memset(&g_ign_cache, 0, sizeof(g_ign_cache));
    memset(&g_lambda_cache, 0, sizeof(g_lambda_cache));
}

uint16_t fuel_calc_lookup_ve(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load) {
    if (!maps) {
        return 0;
    }
    return lookup_with_cache(&maps->fuel_table, &g_fuel_cache, rpm, load);
}

uint16_t fuel_calc_lookup_ignition(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load) {
    if (!maps) {
        return 0;
    }
    return lookup_with_cache(&maps->ignition_table, &g_ign_cache, rpm, load);
}

uint16_t fuel_calc_lookup_lambda(const fuel_calc_maps_t *maps, uint16_t rpm, uint16_t load) {
    if (!maps) {
        return 0;
    }
    return lookup_with_cache(&maps->lambda_table, &g_lambda_cache, rpm, load);
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
