#include "controller.h"
#include <stddef.h>
void control_init(bench_control_t *control) {
    if (control) *control = (bench_control_t){0};
}
uint32_t sample_faults(const bench_sample_t *sample, uint32_t now_ms) {
    if (!sample) return FAULT_SENSOR;
    uint32_t faults = 0;
    if (!sample->sensor_valid || !sample->adc_valid) faults |= FAULT_SENSOR;
    if ((uint32_t)(now_ms - sample->sampled_ms) > 250U) faults |= FAULT_STALE;
    if (sample->temperature_centi > 6000) faults |= FAULT_TEMP;
    for (unsigned i = 0; i < BENCH_CELLS; ++i) {
        if (sample->cells_mv[i] < 2800U) faults |= FAULT_UNDER;
        if (sample->cells_mv[i] > 4200U) faults |= FAULT_OVER;
    }
    return faults;
}
void control_step(bench_control_t *control, const bench_sample_t *sample,
                  uint32_t now_ms, bool reset_requested, uint32_t injected_faults) {
    if (!control) return;
    const uint32_t active = sample_faults(sample, now_ms) | injected_faults;
    const bool previous = control->output_enabled;
    if (active) control->faults |= active;
    else if (reset_requested) control->faults = 0;
    control->initialized = true;
    control->output_enabled = active == 0 && control->faults == 0;
    if (previous != control->output_enabled) ++control->transitions;
}
