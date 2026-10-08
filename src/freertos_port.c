#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
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
static MockTask_t *tasks[16];
static size_t task_count;

static struct timespec deadline(TickType_t ticks) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ticks / 1000;
    t.tv_nsec += (long)(ticks % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    return t;
}


static void *task_runner_wrapper(void *arg) {
    MockTask_t *task = (MockTask_t *)arg;
    task->pxTaskCode(task->pvParameters);
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
    if (!pxTaskCode || !pcName || task_count == 16) return pdFAIL;
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

    tasks[task_count++] = task;
    return pdPASS;
}

void vTaskDelay(const TickType_t xTicksToDelay) {
    struct timespec interval = { xTicksToDelay / 1000, (long)(xTicksToDelay % 1000) * 1000000L };
    while (nanosleep(&interval, &interval) != 0 && errno == EINTR) {}
}

void vTaskStartScheduler(void) {
    // In POSIX execution, scheduler is handled by the OS kernel,
    // so we just join the threads or wait. Here, we'll let main block.
    printf("[Host mock] POSIX threads; no FreeRTOS priority scheduling.\n");
    for (size_t i = 0; i < task_count; ++i) {
        pthread_join(tasks[i]->thread, NULL);
        free(tasks[i]);
    }
    task_count = 0;
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
    if (!uxQueueLength || !uxItemSize || uxQueueLength > SIZE_MAX / uxItemSize) return NULL;
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
    if (!queue || !pvItemToQueue) return pdFAIL;
    struct timespec until = deadline(xTicksToWait);

    pthread_mutex_lock(&(queue->mutex));

    while (queue->count == queue->length) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&(queue->mutex));
            return errQUEUE_FULL; // Timeout instantly
        }
        // Block until queue has space
        int rc = xTicksToWait == portMAX_DELAY ? pthread_cond_wait(&queue->cond_write, &queue->mutex) : pthread_cond_timedwait(&queue->cond_write, &queue->mutex, &until);
        if (rc) { pthread_mutex_unlock(&queue->mutex); return pdFAIL; }
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
    if (!queue || !pvBuffer) return pdFAIL;
    struct timespec until = deadline(xTicksToWait);

    pthread_mutex_lock(&(queue->mutex));

    while (queue->count == 0) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&(queue->mutex));
            return pdFALSE; // Queue empty
        }
        // Block until queue has data
        int rc = xTicksToWait == portMAX_DELAY ? pthread_cond_wait(&queue->cond_read, &queue->mutex) : pthread_cond_timedwait(&queue->cond_read, &queue->mutex, &until);
        if (rc) { pthread_mutex_unlock(&queue->mutex); return pdFAIL; }
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
    pthread_t owner;    /* Ownership only; no priority inheritance. */
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
    pthread_cond_init(&(sem->cond), NULL);
    sem->state = 1; // Starts in given/unlocked state
    sem->is_mutex = 1;

    return (SemaphoreHandle_t)sem;
}

BaseType_t xSemaphoreTake_Impl(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    MockSemaphore_t *sem = (MockSemaphore_t *)xSemaphore;
    if (!sem) return pdFAIL;

    {
        struct timespec until = deadline(xTicksToWait);
        // Binary Semaphore Wait
        pthread_mutex_lock(&(sem->mutex));
        while (sem->state == 0) {
            if (xTicksToWait == 0) {
                pthread_mutex_unlock(&(sem->mutex));
                return pdFALSE;
            }
            int rc = xTicksToWait == portMAX_DELAY ? pthread_cond_wait(&sem->cond, &sem->mutex) : pthread_cond_timedwait(&sem->cond, &sem->mutex, &until);
            if (rc) { pthread_mutex_unlock(&sem->mutex); return pdFAIL; }
        }
        sem->state = 0; // Take the semaphore
        if (sem->is_mutex) sem->owner = pthread_self();
        pthread_mutex_unlock(&(sem->mutex));
        return pdPASS;
    }
}

BaseType_t xSemaphoreGive_Impl(SemaphoreHandle_t xSemaphore) {
    MockSemaphore_t *sem = (MockSemaphore_t *)xSemaphore;
    if (!sem) return pdFAIL;

    {
        // Binary Semaphore Give
        pthread_mutex_lock(&(sem->mutex));
        if (sem->is_mutex && (sem->state != 0 || !pthread_equal(sem->owner, pthread_self()))) {
            pthread_mutex_unlock(&sem->mutex);
            return pdFAIL;
        }
        sem->state = 1;
        pthread_cond_signal(&(sem->cond));
        pthread_mutex_unlock(&(sem->mutex));
        return pdPASS;
    }
}


void vQueueDelete(QueueHandle_t h) {
    MockQueue_t *q = h; if (!q) return;
    pthread_mutex_destroy(&q->mutex); pthread_cond_destroy(&q->cond_read); pthread_cond_destroy(&q->cond_write);
    free(q->buffer); free(q);
}
void vSemaphoreDelete(SemaphoreHandle_t h) {
    MockSemaphore_t *s = h; if (!s) return;
    pthread_mutex_destroy(&s->mutex); pthread_cond_destroy(&s->cond); free(s);
}
