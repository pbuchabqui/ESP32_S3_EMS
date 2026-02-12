#ifndef IGNITION_TIMING_H
#define IGNITION_TIMING_H

#include <stdint.h>

#include <stdbool.h>

bool ignition_init(void);
void ignition_apply_timing(uint16_t advance_deg10, uint16_t rpm);

#endif // IGNITION_TIMING_H
