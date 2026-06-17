#ifndef INC_TASK_H
#define INC_TASK_H

#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void *);
typedef void * TaskHandle_t;

BaseType_t xTaskCreate(
    TaskFunction_t pxTaskCode,
    const char * const pcName,
    const uint16_t usStackDepth,
    void * const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t * const pxCreatedTask
);

void vTaskDelay(const TickType_t xTicksToDelay);
void vTaskStartScheduler(void);

#endif /* INC_TASK_H */
