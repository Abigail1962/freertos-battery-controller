#ifndef INC_SEMPHR_H
#define INC_SEMPHR_H

#include "FreeRTOS.h"

typedef void * SemaphoreHandle_t;

/* Semaphore / Mutex Macros */
#define xSemaphoreCreateBinary()        xSemaphoreCreateBinary_Impl()
#define xSemaphoreCreateMutex()         xSemaphoreCreateMutex_Impl()
#define xSemaphoreTake(xSem, xWait)     xSemaphoreTake_Impl(xSem, xWait)
#define xSemaphoreGive(xSem)            xSemaphoreGive_Impl(xSem)

/* Implementation Functions (defined in freertos_port.c) */
SemaphoreHandle_t xSemaphoreCreateBinary_Impl(void);
SemaphoreHandle_t xSemaphoreCreateMutex_Impl(void);
BaseType_t xSemaphoreTake_Impl(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive_Impl(SemaphoreHandle_t xSemaphore);

void vSemaphoreDelete(SemaphoreHandle_t sem);

#endif /* INC_SEMPHR_H */
