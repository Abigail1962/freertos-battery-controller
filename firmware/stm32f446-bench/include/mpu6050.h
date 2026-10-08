#ifndef BENCH_MPU6050_H
#define BENCH_MPU6050_H
#include <stdint.h>
typedef struct {
    int (*read)(uint8_t address, uint8_t reg, uint8_t *data, unsigned size);
    int (*write)(uint8_t address, uint8_t reg, uint8_t value);
    void (*delay_ms)(uint32_t ms);
} sensor_bus_t;
typedef struct {
    int16_t accel[3];
    int16_t gyro[3];
    int32_t temperature_centi;
} mpu_sample_t;
int mpu_init(const sensor_bus_t *bus, uint8_t address);
int mpu_read(const sensor_bus_t *bus, uint8_t address, mpu_sample_t *sample);
int16_t mpu_signed16(const uint8_t bytes[2]);
int32_t mpu_temperature_centi(int16_t raw);
#endif
