#ifndef INC_FREERTOS_H
#define INC_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

/* FreeRTOS type mappings */
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE  (1)
#define pdFALSE (0)
#define pdPASS  (1)
#define pdFAIL  (0)

#define portMAX_DELAY (0xFFFFFFFFUL)

#endif /* INC_FREERTOS_H */
