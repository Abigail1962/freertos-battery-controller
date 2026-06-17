# FreeRTOS-Based EV Charger & Battery State Controller

This project is a multi-tasking embedded firmware application designed using **FreeRTOS** APIs. It manages battery state monitoring, EV charging cycles (Level 2 Charging), and safety interlocks utilizing real-time scheduling primitives.

To compile and verify this code on development environments without physical MCU hardware, the project includes a **POSIX pthread-based FreeRTOS Port Wrapper** in C. This wrapper maps FreeRTOS task creation, queues, mutexes, and binary semaphores directly to standard OS threads and condition variables, enabling realistic multi-threaded simulation.

---

## ⚙️ Multi-Tasking Architecture & Priorities

The system runs four concurrent tasks managed by the scheduler:

1. **Sensor Acquisition Task (`vSensorAcquisitionTask` - Priority: HIGH)**:
   - Periodically executes every 50ms to read battery cell voltages and temperature sensors.
   - Accesses the shared I2C peripheral bus inside a critical section guarded by the **`xI2CBusMutex`** to prevent data race conditions.
   - Evaluates safety thresholds (Temp > 60°C). If exceeded, triggers the binary semaphore: `xSemaphoreGive(xFaultSemaphore)`.

2. **Charger Controller Task (`vChargerControllerTask` - Priority: MEDIUM)**:
   - Event-driven task that blocks indefinitely on the fault semaphore: `xSemaphoreTake(xFaultSemaphore)`.
   - On wake-up, instantly transitions the charger state machine to `FAULT`, drives GPIO pins to open the charging relays (Contactors), and halts current flow.

3. **Diagnostic Logging Task (`vDiagnosticLoggingTask` - Priority: LOW)**:
   - Event-driven task that blocks waiting for diagnostic log messages to arrive in **`xLogQueue`**.
   - Retrieves log pointers and writes them over the serial terminal interface (USART/stdout).

4. **Simulation Harness / CLI Parser Task (`vSimulationHarnessTask` - Priority: LOW)**:
   - Simulates the passage of real-time events.
   - Periodically logs charger parameters and injects battery faults (simulated thermal runaway) to demonstrate safety preemption and recovery.

---

## 📁 File Structure

- **`inc/FreeRTOS.h`**, **`inc/task.h`**, **`inc/queue.h`**, **`inc/semphr.h`**: Mappings of standard FreeRTOS API headers.
- **`src/freertos_port.c`**: POSIX wrapper code implementing FreeRTOS task, queue, and semaphore behaviors using `pthread` and condition variables.
- **`src/main.c`**: Main system entry point, task setups, shared mutex/queue initializations, and task loop code.
- **`Makefile`**: Multi-platform compiler configuration.

---

## 🛠️ Build & Run Instructions

### 1. Compile the Simulator
```bash
make
```

### 2. Run the Real-Time Simulation
```bash
./bms_freertos_sim
```

### 3. Clean Build Outputs
```bash
make clean
```

---

## 📊 Simulation Demo Output

Executing the binary (`./bms_freertos_sim`) runs the tasks concurrently and demonstrates the safety interlock sequence:

```text
==================================================================
     FreeRTOS EV CHARGER & SAFETY CONTROLLER FIRMWARE             
==================================================================

[FreeRTOS-Port] OS Scheduler started. Standard POSIX threads running.
[t=  0ms] | System State: CHARGING | Charging Relay: CLOSED | Current: 32.0 A | Temp: 34.2 C
[SYSTEM-INIT] EV Charger initiated. Mode: Level 2 Charging, Rate: 32A.
[t=100ms] | System State: CHARGING | Charging Relay: CLOSED | Current: 32.0 A | Temp: 34.2 C
...
[t=500ms] | System State: CHARGING | Charging Relay: CLOSED | Current: 32.0 A | Temp: 34.2 C

[SIMULATION EVENT] Injecting battery thermal runaway (Sensor 0 > 60 C)...
[SENSOR-TASK] Warning: Critical high temperature detected on Sensor 0!
[STATE-TASK] EMERGENCY TRIP: Fault semaphore received. Opened charging relay.
[t=600ms] | System State: FAULT    | Charging Relay: OPEN   | Current:  0.0 A | Temp: 68.5 C
...
[t=1300ms] | System State: FAULT    | Charging Relay: OPEN   | Current:  0.0 A | Temp: 68.5 C

[SIMULATION EVENT] Resetting fault state. Resuming charging...
[CLI-SHELL] Fault cleared. Charging contactors re-engaged.
[t=1400ms] | System State: CHARGING | Charging Relay: CLOSED | Current: 32.0 A | Temp: 34.2 C
[t=1500ms] | System State: CHARGING | Charging Relay: CLOSED | Current: 32.0 A | Temp: 34.2 C
...
Simulation finished. Stopping OS threads...
```
