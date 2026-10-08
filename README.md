# Battery controller: pthread-based FreeRTOS API mock

This host simulation uses a custom API subset, NOT the actual FreeRTOS kernel.
Task priority arguments are metadata only; tasks start at creation. The host OS
schedules them. The mock has no MCU ISR APIs or priority inheritance.

~~~sh
make all test
./bms_freertos_sim
~~~

Shared data is protected by a mock mutex, flags are C11 atomic, threads are joined
before resources are freed, and finite waits use timed condition waits. The mock
mutex enforces ownership but does not model all FreeRTOS semantics. Create tasks
from the main thread only. Handles expire after the mock scheduler joins them.
Delete sync objects only after all users stop.

Tests cover queues, finite waits, binary semaphore saturation, mock mutex, and
thread completion. Host waits are not real-time timing evidence. The demo uses
generated sensor values and a relay variable; no physical bus, contactor, charger,
or STM32 execution has been validated.

To port: use the real Cortex-M4 FreeRTOS port, FreeRTOSConfig.h, target IRQ handlers
and CMSIS board support. Re-check periods, priorities, bounded ISR work, FromISR
calls, stack sizing, and reset/fault behavior on the actual board.

## STM32F446RE firmware target

[Standalone MCU target](firmware/stm32f446-bench/README.md) includes register-level
MPU6050 I2C, UART interrupt rings, circular ADC DMA, TIM2, safety fault handling
and bxCAN internal silent loopback. Both bare-metal and actual FreeRTOS V11.1.0
kernel variants compile and link with ARM GCC and warnings treated as errors.
This is separate from the host mock above. Board execution, peripheral timing
and electrical behavior remain unverified. Cell inputs are synthetic and the
output is an LED surrogate. See the target README for wiring and validation.

```sh
cd firmware/stm32f446-bench
python3 build.py
clang -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined -Iinclude src/controller.c src/mpu6050.c tests/test_logic.c -o build/test_logic
./build/test_logic
```
