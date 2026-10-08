#ifndef BENCH_APP_H
#define BENCH_APP_H
#include "board.h"
#include "controller.h"
typedef struct {
    uint32_t injected;
    bool reset;
} bench_command_t;
void bench_acquire(bench_sample_t *sample, bool *sensor_ready);
void bench_poll_commands(bench_command_t *command);
void bench_log(const bench_sample_t *sample, const bench_control_t *control);
extern volatile int bench_can_result;
#endif
