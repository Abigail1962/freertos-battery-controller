#ifndef BENCH_CONTROLLER_H
#define BENCH_CONTROLLER_H
#include <stdbool.h>
#include <stdint.h>
#define BENCH_CELLS 12U
enum {
    FAULT_UNDER = 1U, FAULT_OVER = 2U, FAULT_TEMP = 4U,
    FAULT_SENSOR = 8U, FAULT_STALE = 16U, FAULT_INJECTED = 32U
};
typedef struct {
    uint16_t cells_mv[BENCH_CELLS];
    int32_t temperature_centi;
    uint32_t sampled_ms;
    bool sensor_valid;
    bool adc_valid;
    bool synthetic_cells;
} bench_sample_t;
typedef struct {
    uint32_t faults;
    uint32_t transitions;
    bool output_enabled;
    bool initialized;
} bench_control_t;
void control_init(bench_control_t *control);
uint32_t sample_faults(const bench_sample_t *sample, uint32_t now_ms);
void control_step(bench_control_t *control, const bench_sample_t *sample,
                  uint32_t now_ms, bool reset_requested, uint32_t injected_faults);
#endif
