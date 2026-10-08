#include "bench_app.h"
#include <stdio.h>
volatile int bench_can_result = -99;
void bench_acquire(bench_sample_t *sample, bool *sensor_ready) {
    bench_sample_t next = {0};
    /* Explicit bench stimulus: no external cell front-end exists. */
    next.synthetic_cells = true;
    for (unsigned i = 0; i < BENCH_CELLS; ++i) next.cells_mv[i] = 3600;
    if (!*sensor_ready) *sensor_ready = mpu_init(&board_sensor_bus, 0x68) == 0;
    mpu_sample_t sensor;
    if (*sensor_ready && mpu_read(&board_sensor_bus, 0x68, &sensor) == 0) {
        next.temperature_centi = sensor.temperature_centi;
        next.sensor_valid = true;
    } else *sensor_ready = false;
    adc_snapshot_t adc = board_adc_snapshot();
    const uint32_t now = board_now_ms();
    next.adc_valid = adc.completed_blocks > 0 && adc.errors == 0 &&
                     (uint32_t)(now - adc.sampled_ms) <= 100U;
    next.sampled_ms = now;
    *sample = next;
}
void bench_poll_commands(bench_command_t *command) {
    char c;
    while (board_uart_get(&c) == 0) {
        switch (c) {
            case 't': command->injected |= FAULT_TEMP; break;
            case 'u': command->injected |= FAULT_UNDER; break;
            case 'o': command->injected |= FAULT_OVER; break;
            case 's': command->injected |= FAULT_INJECTED; break;
            case '0': command->injected = 0; break;
            case 'r': command->reset = true; break;
            default: break;
        }
    }
}
void bench_log(const bench_sample_t *sample, const bench_control_t *control) {
    char text[240];
    adc_snapshot_t adc = board_adc_snapshot();
    uart_stats_t uart = board_uart_stats();
    int length = snprintf(text, sizeof(text),
        "ms=%lu temp_centi=%ld sensor=%u cells=SYNTHETIC adc_raw=%u/%u dma=%lu/%lu "
        "fault=0x%lx led=%u can_internal=%d txdrop=%lu rxdrop=%lu\r\n",
        (unsigned long)board_now_ms(), (long)sample->temperature_centi, sample->sensor_valid,
        adc.temperature_raw, adc.vref_raw, (unsigned long)adc.completed_blocks,
        (unsigned long)adc.errors, (unsigned long)control->faults, control->output_enabled,
        bench_can_result, (unsigned long)uart.tx_dropped_messages, (unsigned long)uart.rx_dropped_bytes);
    if (length > 0 && (unsigned)length < sizeof(text)) (void)board_uart_write(text);
}
