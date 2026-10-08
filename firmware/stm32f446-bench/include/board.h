#ifndef BENCH_BOARD_H
#define BENCH_BOARD_H
#include "stm32f4xx.h"
#include "mpu6050.h"
#include <stdbool.h>
#include <stddef.h>
extern const sensor_bus_t board_sensor_bus;
typedef struct {
    uint16_t temperature_raw, vref_raw;
    uint32_t completed_blocks, errors, sampled_ms;
} adc_snapshot_t;
typedef struct {
    uint32_t tx_dropped_messages, rx_dropped_bytes, rx_errors;
} uart_stats_t;
void board_init(void);
void board_timer_start(void);
uint32_t board_now_ms(void);
void board_delay_ms(uint32_t ms);
void board_output(bool enabled);
void board_fatal(void);
int board_uart_write(const char *message);
int board_uart_get(char *character);
uart_stats_t board_uart_stats(void);
adc_snapshot_t board_adc_snapshot(void);
int board_can_loopback(void);
void board_timer_event_from_isr(void);
#endif
