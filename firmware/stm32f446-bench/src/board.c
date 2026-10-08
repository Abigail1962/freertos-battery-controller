#include "board.h"

#define CLOCK_HZ 16000000UL
#define UART_SIZE 512U
#define UART_MASK (UART_SIZE - 1U)
#define BUS_ERROR (I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF | I2C_SR1_OVR | I2C_SR1_TIMEOUT)
static char tx_buffer[UART_SIZE], rx_buffer[UART_SIZE];
static volatile uint16_t tx_head, tx_tail, rx_head, rx_tail;
static volatile uint32_t clock_ms;
static volatile uart_stats_t uart_stats;
static volatile uint16_t adc_buffer[64];
static volatile adc_snapshot_t adc_data;

static uint32_t irq_lock(void) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return mask;
}
static void irq_unlock(uint32_t mask) { __DMB(); __set_PRIMASK(mask); }
static int wait_bits(volatile uint32_t *reg, uint32_t mask, bool set, uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    do {
        if (((*reg & mask) != 0) == set) return 0;
    } while ((uint32_t)(DWT->CYCCNT - start) < us * (CLOCK_HZ / 1000000UL));
    return -1;
}
void board_delay_ms(uint32_t ms) {
    while (ms--) {
        uint32_t start = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start) < CLOCK_HZ / 1000UL) {}
    }
}
void board_output(bool enabled) {
    GPIOA->BSRR = enabled ? (1U << 5) : (1U << 21);
}
void board_fatal(void) {
    __disable_irq();
    board_output(false);
    while (1) __WFI();
}
static void i2c_configure(void) {
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;
    I2C1->CR2 = 16U;
    I2C1->CCR = 80U;  /* PCLK1 16 MHz / (2 * 100 kHz). */
    I2C1->TRISE = 17U;
    I2C1->CR1 = I2C_CR1_PE;
}
static int i2c_wait(volatile uint32_t *reg, uint32_t mask) {
    uint32_t start = DWT->CYCCNT;
    do {
        if (I2C1->SR1 & BUS_ERROR) return -1;
        if (*reg & mask) return 0;
    } while ((uint32_t)(DWT->CYCCNT - start) < CLOCK_HZ / 200U); /* 5 ms per phase. */
    return -1;
}
static void i2c_clear_address(void) {
    volatile uint32_t discard = I2C1->SR1;
    discard = I2C1->SR2;
    (void)discard;
}
static int i2c_address(uint8_t address, bool read) {
    I2C1->CR1 |= I2C_CR1_START;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_SB)) return -1;
    I2C1->DR = ((uint32_t)address << 1) | (read ? 1U : 0U);
    return i2c_wait(&I2C1->SR1, I2C_SR1_ADDR);
}
static int i2c_abort(void) {
    I2C1->CR1 |= I2C_CR1_STOP;
    i2c_configure();
    return -1;
}
static int i2c_register_read(uint8_t address, uint8_t reg, uint8_t *data, unsigned size) {
    if (!data || size == 0 || size > 32 || address > 0x7F) return -1;
    if (wait_bits(&I2C1->SR2, I2C_SR2_BUSY, false, 5000)) return i2c_abort();
    I2C1->CR1 &= ~I2C_CR1_POS;
    I2C1->CR1 |= I2C_CR1_ACK;
    if (i2c_address(address, false)) return i2c_abort();
    i2c_clear_address();
    if (i2c_wait(&I2C1->SR1, I2C_SR1_TXE)) return i2c_abort();
    I2C1->DR = reg;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF)) return i2c_abort();
    if (size == 2) I2C1->CR1 |= I2C_CR1_POS;
    if (i2c_address(address, true)) return i2c_abort();
    if (size == 1) {
        uint32_t mask = irq_lock();
        I2C1->CR1 &= ~I2C_CR1_ACK;
        i2c_clear_address();
        I2C1->CR1 |= I2C_CR1_STOP;
        irq_unlock(mask);
        if (i2c_wait(&I2C1->SR1, I2C_SR1_RXNE)) return i2c_abort();
        data[0] = (uint8_t)I2C1->DR;
    } else if (size == 2) {
        uint32_t mask = irq_lock();
        I2C1->CR1 &= ~I2C_CR1_ACK;
        i2c_clear_address();
        irq_unlock(mask);
        if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF)) return i2c_abort();
        mask = irq_lock();
        I2C1->CR1 |= I2C_CR1_STOP;
        data[0] = (uint8_t)I2C1->DR;
        data[1] = (uint8_t)I2C1->DR;
        irq_unlock(mask);
    } else {
        i2c_clear_address();
        unsigned position = 0, remaining = size;
        while (remaining > 3) {
            if (i2c_wait(&I2C1->SR1, I2C_SR1_RXNE)) return i2c_abort();
            data[position++] = (uint8_t)I2C1->DR;
            --remaining;
        }
        if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF)) return i2c_abort();
        uint32_t mask = irq_lock();
        I2C1->CR1 &= ~I2C_CR1_ACK;
        data[position++] = (uint8_t)I2C1->DR;
        irq_unlock(mask);
        if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF)) return i2c_abort();
        mask = irq_lock();
        I2C1->CR1 |= I2C_CR1_STOP;
        data[position++] = (uint8_t)I2C1->DR;
        data[position] = (uint8_t)I2C1->DR;
        irq_unlock(mask);
    }
    I2C1->CR1 &= ~I2C_CR1_POS;
    I2C1->CR1 |= I2C_CR1_ACK;
    return 0;
}
static int i2c_register_write(uint8_t address, uint8_t reg, uint8_t value) {
    if (address > 0x7F) return -1;
    if (wait_bits(&I2C1->SR2, I2C_SR2_BUSY, false, 5000) || i2c_address(address, false))
        return i2c_abort();
    i2c_clear_address();
    if (i2c_wait(&I2C1->SR1, I2C_SR1_TXE)) return i2c_abort();
    I2C1->DR = reg;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_TXE)) return i2c_abort();
    I2C1->DR = value;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF)) return i2c_abort();
    I2C1->CR1 |= I2C_CR1_STOP;
    return 0;
}
const sensor_bus_t board_sensor_bus = {
    .read = i2c_register_read, .write = i2c_register_write, .delay_ms = board_delay_ms
};
int board_uart_write(const char *message) {
    if (!message) return -1;
    unsigned length = 0;
    while (message[length]) if (++length >= UART_SIZE) return -1;
    uint32_t mask = irq_lock();
    unsigned used = (tx_head - tx_tail) & UART_MASK;
    if (length > UART_MASK - used) {
        ++uart_stats.tx_dropped_messages;
        irq_unlock(mask);
        return -1;
    }
    for (unsigned i = 0; i < length; ++i) {
        tx_buffer[tx_head] = message[i];
        tx_head = (tx_head + 1U) & UART_MASK;
    }
    USART2->CR1 |= USART_CR1_TXEIE;
    irq_unlock(mask);
    return 0;
}
int board_uart_get(char *character) {
    if (!character) return -1;
    uint32_t mask = irq_lock();
    if (rx_head == rx_tail) { irq_unlock(mask); return -1; }
    *character = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1U) & UART_MASK;
    irq_unlock(mask);
    return 0;
}
uart_stats_t board_uart_stats(void) {
    uint32_t mask = irq_lock();
    uart_stats_t result = uart_stats;
    irq_unlock(mask);
    return result;
}
void USART2_IRQHandler(void) {
    uint32_t status = USART2->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        char c = (char)USART2->DR; /* SR then DR clears receive/error flags. */
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) ++uart_stats.rx_errors;
        else {
            uint16_t next = (rx_head + 1U) & UART_MASK;
            if (next == rx_tail) ++uart_stats.rx_dropped_bytes;
            else { rx_buffer[rx_head] = c; __DMB(); rx_head = next; }
        }
    }
    if ((status & USART_SR_TXE) && (USART2->CR1 & USART_CR1_TXEIE)) {
        if (tx_head == tx_tail) USART2->CR1 &= ~USART_CR1_TXEIE;
        else {
            USART2->DR = (uint8_t)tx_buffer[tx_tail];
            tx_tail = (tx_tail + 1U) & UART_MASK;
        }
    }
}
uint32_t board_now_ms(void) {
    uint32_t mask = irq_lock();
    uint32_t base = clock_ms;
    uint32_t counter = TIM2->CNT;
    if (TIM2->SR & TIM_SR_UIF) { base += 100U; counter = TIM2->CNT; }
    irq_unlock(mask);
    return base + counter;
}
void TIM2_IRQHandler(void) {
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF; /* Write-zero-to-clear; preserve other status flags. */
        clock_ms += 100U;
        board_timer_event_from_isr();
    }
}
adc_snapshot_t board_adc_snapshot(void) {
    uint32_t mask = irq_lock();
    adc_snapshot_t result = adc_data;
    irq_unlock(mask);
    return result;
}
void DMA2_Stream0_IRQHandler(void) {
    const uint32_t status = DMA2->LISR;
    DMA2->LIFCR = status & 0x3DU;
    if (status & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)) {
        ++adc_data.errors;
        return;
    }
    const bool half = (status & DMA_LISR_HTIF0) != 0;
    const bool full = (status & DMA_LISR_TCIF0) != 0;
    if (half && full) { ++adc_data.errors; return; } /* ISR delayed an entire half-buffer. */
    if (half || full) {
        unsigned offset = half ? 30U : 62U;
        adc_data.temperature_raw = adc_buffer[offset];
        adc_data.vref_raw = adc_buffer[offset + 1];
        adc_data.sampled_ms = board_now_ms();
        ++adc_data.completed_blocks;
    }
}
void board_timer_start(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->CR1 = 0;
    TIM2->PSC = 15999U;
    TIM2->ARR = 99U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM2_IRQn, 6);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
}
void board_init(void) {
    /* Startup SystemInit resets RCC. Explicitly select HSI, all buses undivided. */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
    RCC->CFGR = 0;
    while (RCC->CFGR & RCC_CFGR_SWS) {}
    SystemCoreClock = CLOCK_HZ;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    NVIC_SetPriorityGrouping(3); /* Four preemption bits, no subpriority. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_DMA2EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN | RCC_APB1ENR_I2C1EN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->AHB1ENR; (void)RCC->APB1ENR; (void)RCC->APB2ENR;
    GPIOA->MODER = (GPIOA->MODER & ~((3U << 10) | (3U << 4) | (3U << 6))) |
                   (1U << 10) | (2U << 4) | (2U << 6);
    board_output(false);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~((15U << 8) | (15U << 12))) | (7U << 8) | (7U << 12);
    USART2->BRR = (CLOCK_HZ + 57600U) / 115200U;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    USART2->CR3 = USART_CR3_EIE;
    NVIC_SetPriority(USART2_IRQn, 6); NVIC_EnableIRQ(USART2_IRQn);
    GPIOB->MODER = (GPIOB->MODER & ~((3U << 16) | (3U << 18))) | (2U << 16) | (2U << 18);
    GPIOB->OTYPER |= (1U << 8) | (1U << 9);
    GPIOB->OSPEEDR |= (3U << 16) | (3U << 18);
    GPIOB->PUPDR &= ~((3U << 16) | (3U << 18)); /* External pull-ups required. */
    GPIOB->AFR[1] = (GPIOB->AFR[1] & ~0xFFU) | 0x44U;
    i2c_configure();
    DMA2_Stream0->CR = 0;
    DMA2->LIFCR = 0x3DU;
    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)adc_buffer;
    DMA2_Stream0->NDTR = 64;
    DMA2_Stream0->FCR = 0;
    DMA2_Stream0->CR = DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_PSIZE_0 |
                      DMA_SxCR_MSIZE_0 | DMA_SxCR_HTIE | DMA_SxCR_TCIE |
                      DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    NVIC_SetPriority(DMA2_Stream0_IRQn, 6); NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    ADC123_COMMON->CCR = ADC_CCR_TSVREFE | ADC_CCR_ADCPRE; /* ADC clock 2 MHz. */
    ADC1->CR1 = ADC_CR1_SCAN;
    ADC1->SMPR1 = (7U << 18) | (7U << 21); /* Channels 16/17, 480-cycle sampling. */
    ADC1->SQR1 = 1U << 20; /* Two actual internal channels; not 12 cell voltages. */
    ADC1->SQR2 = 0;
    ADC1->SQR3 = 16U | (17U << 5);
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_CONT | ADC_CR2_DMA | ADC_CR2_DDS;
    board_delay_ms(1);
    DMA2_Stream0->CR |= DMA_SxCR_EN;
    ADC1->CR2 |= ADC_CR2_SWSTART;
}
int board_can_loopback(void) {
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;
    CAN1->MCR = CAN_MCR_INRQ;
    if (wait_bits(&CAN1->MSR, CAN_MSR_INAK, true, 5000)) return -1;
    CAN1->BTR = CAN_BTR_LBKM | CAN_BTR_SILM | (11U << 16) | (2U << 20) | 1U;
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1U; CAN1->FM1R &= ~1U; CAN1->FS1R |= 1U; CAN1->FFA1R &= ~1U;
    CAN1->sFilterRegister[0].FR1 = 0; CAN1->sFilterRegister[0].FR2 = 0;
    CAN1->FA1R |= 1U; CAN1->FMR &= ~CAN_FMR_FINIT;
    CAN1->MCR &= ~CAN_MCR_INRQ;
    if (wait_bits(&CAN1->MSR, CAN_MSR_INAK, false, 5000)) return -2;
    CAN1->sTxMailBox[0].TIR = 0x2A0U << 21;
    CAN1->sTxMailBox[0].TDTR = 8U;
    CAN1->sTxMailBox[0].TDLR = 0x44332211U; CAN1->sTxMailBox[0].TDHR = 0x88776655U;
    CAN1->sTxMailBox[0].TIR |= CAN_TI0R_TXRQ;
    if (wait_bits(&CAN1->RF0R, CAN_RF0R_FMP0, true, 10000)) return -3;
    bool valid = (CAN1->sFIFOMailBox[0].RIR >> 21) == 0x2A0U &&
                 (CAN1->sFIFOMailBox[0].RDTR & 15U) == 8U &&
                 CAN1->sFIFOMailBox[0].RDLR == 0x44332211U &&
                 CAN1->sFIFOMailBox[0].RDHR == 0x88776655U;
    CAN1->RF0R |= CAN_RF0R_RFOM0;
    return valid ? 0 : -4;
}
