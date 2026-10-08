#include "mpu6050.h"
#include <stddef.h>
int16_t mpu_signed16(const uint8_t bytes[2]) {
    int32_t value = ((uint32_t)bytes[0] << 8) | bytes[1];
    if (value & 0x8000) value -= 65536;
    return (int16_t)value;
}
int32_t mpu_temperature_centi(int16_t raw) {
    return (int32_t)raw * 100 / 340 + 3653;
}
int mpu_init(const sensor_bus_t *bus, uint8_t address) {
    if (!bus || !bus->read || !bus->write || !bus->delay_ms ||
        (address != 0x68 && address != 0x69)) return -1;
    uint8_t id = 0;
    if (bus->read(address, 0x75, &id, 1) != 0 || id != 0x68) return -2;
    if (bus->write(address, 0x6B, 0x01) != 0) return -3; /* Wake, PLL with X gyro. */
    bus->delay_ms(100);
    /* +/-2g, +/-250dps; explicit ranges for interpreting raw values. */
    if (bus->write(address, 0x1C, 0) != 0 || bus->write(address, 0x1B, 0) != 0) return -3;
    return 0;
}
int mpu_read(const sensor_bus_t *bus, uint8_t address, mpu_sample_t *sample) {
    if (!bus || !bus->read || !sample || (address != 0x68 && address != 0x69)) return -1;
    uint8_t data[14];
    if (bus->read(address, 0x3B, data, sizeof(data)) != 0) return -2;
    mpu_sample_t result = {0};
    for (unsigned i = 0; i < 3; ++i) {
        result.accel[i] = mpu_signed16(data + 2 * i);
        result.gyro[i] = mpu_signed16(data + 8 + 2 * i);
    }
    result.temperature_centi = mpu_temperature_centi(mpu_signed16(data + 6));
    *sample = result; /* Do not partially update caller output on a bus error. */
    return 0;
}
