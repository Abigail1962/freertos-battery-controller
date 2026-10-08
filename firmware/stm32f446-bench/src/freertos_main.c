#include "bench_app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
static SemaphoreHandle_t fault_event, i2c_mutex;
static QueueHandle_t log_queue;
static bench_sample_t latest;
static bench_command_t command;
static bench_control_t control;
static volatile bool scheduler_ready;
volatile uint32_t bench_log_queue_drops;
volatile UBaseType_t bench_sensor_stack_min, bench_control_stack_min, bench_logger_stack_min;
typedef struct { bench_sample_t sample; bench_control_t control; } log_event_t;
void board_timer_event_from_isr(void) {
    if (scheduler_ready) {
        BaseType_t wake = pdFALSE;
        xSemaphoreGiveFromISR(fault_event, &wake);
        portYIELD_FROM_ISR(wake);
    }
}
static void sensor_task(void *argument) {
    (void)argument;
    bool ready = false;
    TickType_t next = xTaskGetTickCount();
    while (1) {
        bench_sample_t sample;
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10)) == pdPASS) {
            bench_acquire(&sample, &ready);
            xSemaphoreGive(i2c_mutex);
        } else {
            sample = (bench_sample_t){0};
            sample.sampled_ms = board_now_ms();
        }
        bench_command_t incoming;
        taskENTER_CRITICAL();
        incoming = command;
        taskEXIT_CRITICAL();
        incoming.reset = false; /* Only new UART reset requests may be published. */
        bench_poll_commands(&incoming);
        taskENTER_CRITICAL();
        latest = sample;
        command.injected = incoming.injected;
        command.reset |= incoming.reset;
        scheduler_ready = true; /* First sample exists before the timer can notify. */
        taskEXIT_CRITICAL();
        /* Notify on a new sample or fault; controller is highest priority. */
        xSemaphoreGive(fault_event);
        bench_sensor_stack_min = uxTaskGetStackHighWaterMark(NULL);
        xTaskDelayUntil(&next, pdMS_TO_TICKS(50));
    }
}
static void control_task(void *argument) {
    (void)argument;
    while (1) {
        (void)xSemaphoreTake(fault_event, portMAX_DELAY);
        bench_sample_t sample;
        bench_command_t current;
        taskENTER_CRITICAL();
        sample = latest; current = command; command.reset = false;
        taskEXIT_CRITICAL();
        control_step(&control, &sample, board_now_ms(), current.reset, current.injected);
        board_output(control.output_enabled);
        log_event_t event = {.sample = sample, .control = control};
        if (xQueueSend(log_queue, &event, 0) != pdPASS) ++bench_log_queue_drops;
        bench_control_stack_min = uxTaskGetStackHighWaterMark(NULL);
    }
}
static void logger_task(void *argument) {
    (void)argument;
    while (1) {
        log_event_t event;
        if (xQueueReceive(log_queue, &event, portMAX_DELAY) == pdPASS)
            bench_log(&event.sample, &event.control);
        bench_logger_stack_min = uxTaskGetStackHighWaterMark(NULL);
    }
}
void vApplicationMallocFailedHook(void) { board_fatal(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task; (void)name; board_fatal();
}
int main(void) {
    board_init();
    board_timer_start();
    bench_can_result = board_can_loopback();
    (void)board_uart_write("REAL FREERTOS KERNEL BENCH: hardware execution UNVERIFIED.\r\n"
                           "Cells synthetic; t/u/o/s inject, 0 clear, r safe reset.\r\n");
    control_init(&control);
    fault_event = xSemaphoreCreateBinary();
    i2c_mutex = xSemaphoreCreateMutex();
    log_queue = xQueueCreate(8, sizeof(log_event_t));
    if (!fault_event || !i2c_mutex || !log_queue) board_fatal();
    if (xTaskCreate(control_task, "Control", 384, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(sensor_task, "Sensor", 384, NULL, 3, NULL) != pdPASS ||
        xTaskCreate(logger_task, "Logger", 512, NULL, 1, NULL) != pdPASS) board_fatal();
    vTaskStartScheduler();
    board_fatal();
}
