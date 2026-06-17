#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* ==============================================================================
 * SYSTEM STATES & GLOBALS
 * ==============================================================================
 */
typedef enum {
    CHARGER_STATE_IDLE = 0,
    CHARGER_STATE_CHARGING,
    CHARGER_STATE_FAULT
} ChargerState_t;

typedef struct {
    float cell_voltages[4];
    float temperatures[2];      // 2 Temperature sensors on the battery pack
    float charge_current;       // Current in Amps
    uint8_t safety_relay;       // GPIO pin representation: 1=CLOSED (Charging), 0=OPEN (Trip)
    ChargerState_t state;
} EVSystem_t;

/* Global System Data */
static EVSystem_t g_system;

/* FreeRTOS Communication Handles */
static QueueHandle_t xLogQueue = NULL;
static SemaphoreHandle_t xFaultSemaphore = NULL;
static SemaphoreHandle_t xI2CBusMutex = NULL;

/* Simulation control variables */
static int g_simulation_running = 1;
static int g_inject_temp_fault = 0;

/* ==============================================================================
 * TASK 1: SENSOR ACQUISITION TASK (HIGH PRIORITY)
 * ==============================================================================
 */
void vSensorAcquisitionTask(void *pvParameters) {
    (void)pvParameters;
    const char *log_msg = NULL;

    while (g_simulation_running) {
        // Take I2C Mutex to prevent resource contention on the shared I2C bus
        if (xSemaphoreTake(xI2CBusMutex, portMAX_DELAY) == pdPASS) {
            // Read simulated temperature sensors
            if (g_inject_temp_fault) {
                g_system.temperatures[0] = 68.5f; // Overtemp condition (>60C)
            } else {
                g_system.temperatures[0] = 34.2f; // Normal charging temperature
            }
            g_system.temperatures[1] = 33.8f;

            // Read simulated cell voltages
            g_system.cell_voltages[0] = 3.82f;
            g_system.cell_voltages[1] = 3.81f;
            g_system.cell_voltages[2] = 3.83f;
            g_system.cell_voltages[3] = 3.82f;

            // Release Mutex
            xSemaphoreGive(xI2CBusMutex);
        }

        // Safety Interlock evaluation
        if (g_system.temperatures[0] > 60.0f) {
            log_msg = "[SENSOR-TASK] Warning: Critical high temperature detected on Sensor 0!";
            xQueueSend(xLogQueue, &log_msg, 0);

            // Give binary semaphore to signal the safety controller task instantly
            xSemaphoreGive(xFaultSemaphore);
        }

        vTaskDelay(50); // Periodic execution every 50ms
    }
}

/* ==============================================================================
 * TASK 2: CHARGER CONTROLLER TASK (MEDIUM PRIORITY)
 * ==============================================================================
 */
void vChargerControllerTask(void *pvParameters) {
    (void)pvParameters;
    const char *log_msg = NULL;

    while (g_simulation_running) {
        // Block indefinitely waiting for the fault binary semaphore
        if (xSemaphoreTake(xFaultSemaphore, portMAX_DELAY) == pdPASS) {
            if (g_system.state != CHARGER_STATE_FAULT) {
                // Transition state machine to FAULT
                g_system.state = CHARGER_STATE_FAULT;
                
                // Toggle safety relay GPIO pin to open contactors (Open relay, halt current)
                g_system.safety_relay = 0; 
                g_system.charge_current = 0.0f;

                log_msg = "[STATE-TASK] EMERGENCY TRIP: Fault semaphore received. Opened charging relay.";
                xQueueSend(xLogQueue, &log_msg, 0);
            }
        }
    }
}

/* ==============================================================================
 * TASK 3: DIAGNOSTIC LOGGING TASK (LOW PRIORITY)
 * ==============================================================================
 */
void vDiagnosticLoggingTask(void *pvParameters) {
    (void)pvParameters;
    const char *rx_log = NULL;

    while (g_simulation_running) {
        // Block waiting for new log entries in the queue
        if (xQueueReceive(xLogQueue, &rx_log, portMAX_DELAY) == pdPASS) {
            printf("%s\n", rx_log);
            fflush(stdout);
        }
    }
}

/* ==============================================================================
 * TASK 4: SIMULATION HARNESS / CLI COMMAND POLLER (LOW PRIORITY)
 * ==============================================================================
 */
void vSimulationHarnessTask(void *pvParameters) {
    (void)pvParameters;
    const char *log_msg = NULL;

    // Normal startup
    g_system.state = CHARGER_STATE_CHARGING;
    g_system.safety_relay = 1; // Relays closed
    g_system.charge_current = 32.0f; // 32A charge rate

    log_msg = "[SYSTEM-INIT] EV Charger initiated. Mode: Level 2 Charging, Rate: 32A.";
    xQueueSend(xLogQueue, &log_msg, 0);

    for (int step = 0; step < 20; step++) {
        // Print periodic status update
        printf("[t=%3dms] | System State: %-8s | Charging Relay: %-6s | Current: %4.1f A | Temp: %4.1f C\n",
            step * 100,
            (g_system.state == CHARGER_STATE_CHARGING ? "CHARGING" : 
             g_system.state == CHARGER_STATE_FAULT ? "FAULT" : "IDLE"),
            (g_system.safety_relay ? "CLOSED" : "OPEN"),
            g_system.charge_current,
            g_system.temperatures[0]
        );
        fflush(stdout);

        // Inject high temperature fault at t=600ms
        if (step == 5) {
            printf("\n[SIMULATION EVENT] Injecting battery thermal runaway (Sensor 0 > 60 C)...\n");
            g_inject_temp_fault = 1;
        }

        // Clear fault at t=1400ms and restart charging
        if (step == 13) {
            printf("\n[SIMULATION EVENT] Resetting fault state. Resuming charging...\n");
            g_inject_temp_fault = 0;
            g_system.state = CHARGER_STATE_CHARGING;
            g_system.safety_relay = 1;
            g_system.charge_current = 32.0f;
            log_msg = "[CLI-SHELL] Fault cleared. Charging contactors re-engaged.";
            xQueueSend(xLogQueue, &log_msg, 0);
        }

        vTaskDelay(100); // Poll status every 100ms
    }

    // Terminate simulation
    printf("\nSimulation finished. Stopping OS threads...\n");
    g_simulation_running = 0;
    
    // Send dummy items to unblock logging task and allow exit
    log_msg = "EXIT";
    xQueueSend(xLogQueue, &log_msg, 0);
    xSemaphoreGive(xFaultSemaphore);
}

/* ==============================================================================
 * MAIN SYSTEM ENTRY
 * ==============================================================================
 */
int main(void) {
    printf("==================================================================\n");
    printf("     FreeRTOS EV CHARGER & SAFETY CONTROLLER FIRMWARE             \n");
    printf("==================================================================\n\n");

    /* Create FreeRTOS synchronization objects */
    xLogQueue = xQueueCreate(10, sizeof(char *));
    xFaultSemaphore = xSemaphoreCreateBinary();
    xI2CBusMutex = xSemaphoreCreateMutex();

    if (xLogQueue == NULL || xFaultSemaphore == NULL || xI2CBusMutex == NULL) {
        fprintf(stderr, "Failed to create FreeRTOS sync objects.\n");
        return 1;
    }

    /* Create concurrent tasks with priorities */
    // Note: Priority values are passed to represent priority layouts (High=3, Med=2, Low=1)
    xTaskCreate(vSensorAcquisitionTask, "SensorAcq", 256, NULL, 3, NULL);
    xTaskCreate(vChargerControllerTask, "ChargerCtrl", 256, NULL, 2, NULL);
    xTaskCreate(vDiagnosticLoggingTask, "Logger", 256, NULL, 1, NULL);
    xTaskCreate(vSimulationHarnessTask, "SimHarness", 256, NULL, 1, NULL);

    /* Start FreeRTOS Scheduler (POSIX Threads spawn immediately on creation) */
    vTaskStartScheduler();

    // In this POSIX simulation, main thread blocks until threads complete
    while (g_simulation_running) {
        usleep(50000); // 50ms sleep
    }

    return 0;
}
