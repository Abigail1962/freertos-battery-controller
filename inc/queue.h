#ifndef INC_QUEUE_H
#define INC_QUEUE_H

#include "FreeRTOS.h"

#define errQUEUE_FULL ((BaseType_t)0)

typedef void * QueueHandle_t;

QueueHandle_t xQueueCreate(
    UBaseType_t uxQueueLength,
    UBaseType_t uxItemSize
);

BaseType_t xQueueSend(
    QueueHandle_t xQueue,
    const void * pvItemToQueue,
    TickType_t xTicksToWait
);

BaseType_t xQueueReceive(
    QueueHandle_t xQueue,
    void * const pvBuffer,
    TickType_t xTicksToWait
);

void vQueueDelete(QueueHandle_t queue);

#endif /* INC_QUEUE_H */
