#include "bench_app.h"
static volatile uint32_t pending_ticks;
void board_timer_event_from_isr(void) { ++pending_ticks; }
int main(void) {
    board_init();
    board_timer_start();
    bench_can_result = board_can_loopback();
    (void)board_uart_write("BAREMETAL BENCH: hardware execution UNVERIFIED; cells synthetic.\r\n"
                           "t/u/o/s inject; 0 clear stimulus; r request safe reset.\r\n");
    bench_control_t control;
    bench_sample_t sample = {0};
    bench_command_t command = {0};
    bool sensor_ready = false;
    uint32_t consumed = 0;
    control_init(&control);
    while (1) {
        bench_poll_commands(&command);
        if (pending_ticks != consumed) {
            consumed = pending_ticks; /* Coalesce missed periods; never claim no missed ticks. */
            bench_acquire(&sample, &sensor_ready);
            control_step(&control, &sample, board_now_ms(), command.reset, command.injected);
            command.reset = false;
            board_output(control.output_enabled);
            bench_log(&sample, &control);
        }
        __WFI();
    }
}
