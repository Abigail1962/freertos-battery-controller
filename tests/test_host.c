#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static void worker(void *arg) {
    QueueHandle_t queue = arg;
    vTaskDelay(10);
    int value = 42;
    assert(xQueueSend(queue, &value, 0) == pdPASS);
}
int main(void) {
    assert(!xQueueCreate(0, sizeof(int)));
    QueueHandle_t queue = xQueueCreate(1, sizeof(int));
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    assert(queue && sem && mutex);
    assert(xSemaphoreGive(mutex) == pdFAIL); /* Unowned mutex cannot be released. */
    int value = 7, out = 0;
    double start = now();
    assert(xQueueReceive(queue, &out, 25) == pdFAIL);
    assert(now() - start >= .020);
    assert(xQueueSend(queue, &value, 0) == pdPASS);
    assert(xQueueSend(queue, &value, 25) == pdFAIL);
    assert(xQueueReceive(queue, &out, 0) == pdPASS && out == 7);
    assert(xSemaphoreTake(sem, 25) == pdFAIL);
    assert(xSemaphoreGive(sem) == pdPASS);
    assert(xSemaphoreGive(sem) == pdPASS); /* Binary token saturates at one. */
    assert(xSemaphoreTake(sem, 0) == pdPASS);
    assert(xSemaphoreTake(sem, 0) == pdFAIL);
    assert(xSemaphoreTake(mutex, 0) == pdPASS);
    assert(xSemaphoreTake(mutex, 25) == pdFAIL);
    assert(xSemaphoreGive(mutex) == pdPASS);
    assert(xTaskCreate(worker, "worker", 256, queue, 1, NULL) == pdPASS);
    assert(xQueueReceive(queue, &out, 1000) == pdPASS && out == 42);
    vTaskStartScheduler(); /* Joins worker before resource destruction. */
    vQueueDelete(queue); vSemaphoreDelete(sem); vSemaphoreDelete(mutex);
    puts("PASS: queue, timeouts, binary token, mock mutex, task join");
}
