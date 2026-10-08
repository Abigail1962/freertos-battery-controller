#include "controller.h"
#include "mpu6050.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t identity = 0x68, registers[256];
static int bus_failed;
static int fake_read(uint8_t address, uint8_t reg, uint8_t *data, unsigned size) {
    (void)address;
    if (bus_failed) return -1;
    if (reg == 0x75) { *data = identity; return 0; }
    memcpy(data, registers + reg, size);
    return 0;
}
static int fake_write(uint8_t address, uint8_t reg, uint8_t value) {
    (void)address;
    if (bus_failed) return -1;
    registers[reg] = value;
    return 0;
}
static void fake_delay(uint32_t ms) { assert(ms == 100); }
int main(void) {
    sensor_bus_t bus = {fake_read, fake_write, fake_delay};
    assert(mpu_init(&bus, 0x68) == 0);
    assert(registers[0x6B] == 1 && registers[0x1C] == 0 && registers[0x1B] == 0);
    identity = 0x69;
    assert(mpu_init(&bus, 0x68) != 0); /* AD0 does not change WHO_AM_I. */
    identity = 0x68;
    const uint8_t negative[2] = {0x80, 0}, positive[2] = {0x7F, 0xFF};
    assert(mpu_signed16(negative) == -32768 && mpu_signed16(positive) == 32767);
    assert(mpu_temperature_centi(0) == 3653);
    assert(mpu_temperature_centi(340) == 3753);
    assert(mpu_temperature_centi(-340) == 3553);
    registers[0x3B] = 0xFF; registers[0x3C] = 0xFF;
    registers[0x41] = 0x01; registers[0x42] = 0x54;
    mpu_sample_t result = {0};
    assert(mpu_read(&bus, 0x68, &result) == 0);
    assert(result.accel[0] == -1 && result.temperature_centi == 3753);
    mpu_sample_t previous = result;
    bus_failed = 1;
    assert(mpu_read(&bus, 0x68, &result) != 0);
    assert(memcmp(&result, &previous, sizeof(result)) == 0);
    bench_sample_t sample = {.temperature_centi = 2500, .sampled_ms = 100,
        .sensor_valid = true, .adc_valid = true, .synthetic_cells = true};
    for (unsigned i = 0; i < BENCH_CELLS; ++i) sample.cells_mv[i] = 3600;
    bench_control_t control;
    control_init(&control);
    assert(!control.output_enabled);
    control_step(&control, &sample, 100, false, 0);
    assert(control.output_enabled);
    sample.cells_mv[11] = 4201;
    control_step(&control, &sample, 100, true, 0);
    assert(!control.output_enabled && (control.faults & FAULT_OVER));
    sample.cells_mv[11] = 3600;
    control_step(&control, &sample, 100, false, 0);
    assert(!control.output_enabled); /* Clearing stimulus does not clear latch. */
    control_step(&control, &sample, 100, true, 0);
    assert(control.output_enabled);
    sample.sensor_valid = false;
    control_step(&control, &sample, 100, true, 0);
    assert(!control.output_enabled && (control.faults & FAULT_SENSOR));
    sample.sensor_valid = true;
    assert(sample_faults(&sample, 351) & FAULT_STALE);
    sample.sampled_ms = UINT32_MAX - 9;
    assert(!(sample_faults(&sample, 10) & FAULT_STALE)); /* Counter wrap is safe. */
    sample.sampled_ms = 100;
    sample.cells_mv[0] = 2800; sample.cells_mv[11] = 4200;
    assert(sample_faults(&sample, 100) == 0);
    sample.cells_mv[0] = 2799;
    assert(sample_faults(&sample, 100) & FAULT_UNDER);
    puts("PASS: MPU protocol/decoding and fault/reset/freshness control (host, not hardware)");
}
