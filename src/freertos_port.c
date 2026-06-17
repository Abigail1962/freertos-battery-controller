#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* ==============================================================================
 * TASK API IMPLEMENTATION
 * ==============================================================================
 */
typedef struct {
    TaskFunction_t pxTaskCode;
    void *pvParameters;
    char pcName[32];
    UBaseType_t uxPriority;
    pthread_t thread;
} MockTask_t;

static void *task_runner_wrapper(void *arg) {
    MockTask_t *task = (MockTask_t *)arg;
    task->pxTaskCode(task->pvParameters);
    free(task);
    return NULL;
}

BaseType_t xTaskCreate(
    TaskFunction_t pxTaskCode,
    const char * const pcName,
    const uint16_t usStackDepth,
    void * const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t * const pxCreatedTask
) {
    (void)usStackDepth; // Unused in simulation

    MockTask_t *task = (MockTask_t *)malloc(sizeof(MockTask_t));
    if (!task) return pdFAIL;

    task->pxTaskCode = pxTaskCode;
    task->pvParameters = pvParameters;
    task->uxPriority = uxPriority;
    strncpy(task->pcName, pcName, sizeof(task->pcName) - 1);
    task->pcName[sizeof(task->pcName) - 1] = '\0';

    int res = pthread_create(&(task->thread), NULL, task_runner_wrapper, task);
    if (res != 0) {
        free(task);
        return pdFAIL;
    }

    if (pxCreatedTask != NULL) {
        *pxCreatedTask = (TaskHandle_t)task;
    }

    return pdPASS;
}

void vTaskDelay(const TickType_t xTicksToDelay) {
    // 1 tick = 1ms in our simulation configuration
    usleep(xTicksToDelay * 1000);
}

void vTaskStartScheduler(void) {
    // In POSIX execution, scheduler is handled by the OS kernel,
    // so we just join the threads or wait. Here, we'll let main block.
    printf("[FreeRTOS-Port] OS Scheduler started. Standard POSIX threads running.\n");
}

/* ==============================================================================
 * QUEUE API IMPLEMENTATION
 * ==============================================================================
 */
typedef struct {
    uint8_t *buffer;
    uint32_t length;
    uint32_t item_size;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond_read;
    pthread_cond_t cond_write;
} MockQueue_t;

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize) {
    MockQueue_t *queue = (MockQueue_t *)malloc(sizeof(MockQueue_t));
    if (!queue) return NULL;

    queue->buffer = (uint8_t *)malloc(uxQueueLength * uxItemSize);
    if (!queue->buffer) {
        free(queue);
        return NULL;
    }

    queue->length = uxQueueLength;
    queue->item_size = uxItemSize;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

    pthread_mutex_init(&(queue->mutex), NULL);
    pthread_cond_init(&(queue->cond_read), NULL);
    pthread_cond_init(&(queue->cond_write), NULL);

    return (QueueHandle_t)queue;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void * pvItemToQueue, TickType_t xTicksToWait) {
    MockQueue_t *queue = (MockQueue_t *)xQueue;
    if (!queue) return pdFAIL;

    pthread_mutex_lock(&(queue->mutex));

    while (queue->count == queue->length) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&(queue->mutex));
            return errQUEUE_FULL; // Timeout instantly
        }
        // Block until queue has space
        pthread_cond_wait(&(queue->cond_write), &(queue->mutex));
    }

    // Copy element to queue buffer
    uint8_t *dest = queue->buffer + (queue->head * queue->item_size);
    memcpy(dest, pvItemToQueue, queue->item_size);

    queue->head = (queue->head + 1) % queue->length;
    queue->count++;

    // Signal waiting readers
    pthread_cond_signal(&(queue->cond_read));
    pthread_mutex_unlock(&(queue->mutex));

    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void * const pvBuffer, TickType_t xTicksToWait) {
    MockQueue_t *queue = (MockQueue_t *)xQueue;
    if (!queue) return pdFAIL;

    pthread_mutex_lock(&(queue->mutex));

    while (queue->count == 0) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&(queue->mutex));
            return pdFALSE; // Queue empty
        }
        // Block until queue has data
        pthread_cond_wait(&(queue->cond_read), &(queue->mutex));
    }

    // Extract element from queue buffer
    uint8_t *src = queue->buffer + (queue->tail * queue->item_size);
    memcpy(pvBuffer, src, queue->item_size);

    queue->tail = (queue->tail + 1) % queue->length;
    queue->count--;

    // Signal waiting writers
    pthread_cond_signal(&(queue->cond_write));
    pthread_mutex_unlock(&(queue->mutex));

    return pdPASS;
}



/* ==============================================================================
 * SEMAPHORE & MUTEX API IMPLEMENTATION
 * ==============================================================================
 */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint32_t state;     // 0 = taken, 1 = given
    int is_mutex;       // 1 = Mutex, 0 = Binary Semaphore
} MockSemaphore_t;

SemaphoreHandle_t xSemaphoreCreateBinary_Impl(void) {
    MockSemaphore_t *sem = (MockSemaphore_t *)malloc(sizeof(MockSemaphore_t));
    if (!sem) return NULL;

    pthread_mutex_init(&(sem->mutex), NULL);
    pthread_cond_init(&(sem->cond), NULL);
    sem->state = 0; // Starts in taken state
    sem->is_mutex = 0;

    return (SemaphoreHandle_t)sem;
}

SemaphoreHandle_t xSemaphoreCreateMutex_Impl(void) {
    MockSemaphore_t *sem = (MockSemaphore_t *)malloc(sizeof(MockSemaphore_t));
    if (!sem) return NULL;

    pthread_mutex_init(&(sem->mutex), NULL);
    sem->state = 1; // Starts in given/unlocked state
    sem->is_mutex = 1;

    return (SemaphoreHandle_t)sem;
}

BaseType_t xSemaphoreTake_Impl(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    MockSemaphore_t *sem = (MockSemaphore_t *)xSemaphore;
    if (!sem) return pdFAIL;

    if (sem->is_mutex) {
        // Direct mutex lock (ignores xTicksToWait in this simulation)
        pthread_mutex_lock(&(sem->mutex));
        return pdPASS;
    } else {
        // Binary Semaphore Wait
        pthread_mutex_lock(&(sem->mutex));
        while (sem->state == 0) {
            if (xTicksToWait == 0) {
                pthread_mutex_unlock(&(sem->mutex));
                return pdFALSE;
            }
            pthread_cond_wait(&(sem->cond), &(sem->mutex));
        }
        sem->state = 0; // Take the semaphore
        pthread_mutex_unlock(&(sem->mutex));
        return pdPASS;
    }
}

BaseType_t xSemaphoreGive_Impl(SemaphoreHandle_t xSemaphore) {
    MockSemaphore_t *sem = (MockSemaphore_t *)xSemaphore;
    if (!sem) return pdFAIL;

    if (sem->is_mutex) {
        // Direct mutex unlock
        pthread_mutex_unlock(&(sem->mutex));
        return pdPASS;
    } else {
        // Binary Semaphore Give
        pthread_mutex_lock(&(sem->mutex));
        sem->state = 1;
        pthread_cond_signal(&(sem->cond));
        pthread_mutex_unlock(&(sem->mutex));
        return pdPASS;
    }
}
