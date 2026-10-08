# STM32F446RE peripheral and controller bench

Status: both ARM firmware variants compile and link with warnings treated as errors.
Controller and MPU6050 parsing tests run on the host with ASan/UBSan.
Neither firmware has been flashed or validated on hardware. This is not HIL.

## Build

Run `python3 build.py` in this directory. It finds the installed CubeIDE ARM GCC;
alternatively set `ARM_GCC` to the full compiler path. Official CMSIS, ST startup
and FreeRTOS V11.1.0 sources are vendored with licenses and pinned revision/hash
manifests. No dependency downloads are needed for the build.

Outputs: `build/baremetal/firmware.elf` and `.bin`, and
`build/freertos/firmware.elf` and `.bin`. Linker: 512 KB flash, 128 KB SRAM.
Clock: HSI 16 MHz, no PLL. Firmware uses register-level peripheral code.

## Wiring and first validation

Use the Nucleo ST-LINK USB connection for power/debug and USART2 virtual COM port.
Connect MPU6050 VCC to 3.3 V, GND to GND, SCL to PB8 (Arduino D15), SDA to PB9
(Arduino D14), and AD0 to GND for address 0x68. Check the module's pull-ups are
to 3.3 V; I2C pins need external pull-ups. Leave INT unconnected.

1. Start with baremetal ELF in CubeIDE or CubeProgrammer using ST-LINK/SWD.
   Flashing is a manual step; build.py does not program hardware.
2. Open USART2 VCP at 115200 baud, 8N1. Check WHO_AM_I at 0x75 reads 0x68;
   inspect I2C traffic if initialization fails. Verify sensor=1 and temperature
   changes plausibly. This is chip temperature, not battery pack temperature.
3. Check DMA block count increases and errors remain zero. These ADC channels
   are internal temperature and Vref raw counts, not twelve measured cells.
4. Check PA5/LD2 follows controller state. Commands t/u/o/s inject thermal,
   undervoltage, overvoltage and generic faults. `0` removes injected stimuli;
   `r` requests reset, permitted only with fresh, valid, safe inputs.
   Recovery never occurs solely because a stimulus disappears.
5. Confirm can_internal=0 for the internal silent-loopback test. This does not
   validate CAN wires, a transceiver, another node or a vehicle network.
6. Repeat on the FreeRTOS ELF. Inspect stack minima (`bench_*_stack_min`),
   `bench_log_queue_drops`, UART drops and DMA errors in the debugger. Confirm
   control continues during logging congestion and fault input transitions.
7. Disconnect/reconnect the sensor; check fault latch, output off, bounded I2C
   timeout and explicit safe reset after recovery. Verify timing with a scope
   or logic analyzer before reporting latency or real-time performance.

## Scope and known limits

Baremetal uses a 10 Hz TIM2 interrupt and deferred main-loop processing.
FreeRTOS uses the real Cortex-M4F kernel port, priority 4 control / 3 acquisition /
1 logging tasks, a queue, binary semaphore and I2C mutex. The current mutex has
one bus-using task: mutual exclusion is implemented, contention is not demonstrated.
UART TX and RX are interrupt-driven rings with drop counters. ADC is circular DMA.
Twelve cell voltages are explicitly synthetic. PA5 is an LED surrogate for a relay;
there is no high-voltage contactor, battery measurement front-end or power control.
MPU6050 is a replacement bench sensor; TMP102 and external SPI flash are not attached.
This target does not validate the Linux kernel project or external W25Q hardware.
I2C software reset is implemented, but recovery from a physically stuck SDA line
has not been demonstrated. Peripheral timing, interrupt interaction and electrical
behavior still require the board tests above.
