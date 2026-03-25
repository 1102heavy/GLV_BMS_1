#include "i2c.h"
#include "stm32f4xx.h"

/* ------------------------------------------------------------------ */
/* RCC bit positions                                                    */
/* ------------------------------------------------------------------ */
#define I2C_GPIOB_EN   (1U << 1)    /* AHB1ENR : GPIOBEN */
#define I2C_I2C1_EN    (1U << 21)   /* APB1ENR : I2C1EN  */

/* ------------------------------------------------------------------ */
/* I2C1 CR1 bits                                                        */
/* ------------------------------------------------------------------ */
#define I2C_CR1_PE     (1U << 0)
#define I2C_CR1_START  (1U << 8)
#define I2C_CR1_STOP   (1U << 9)
#define I2C_CR1_ACK   (1U << 10)
#define I2C_CR1_POS   (1U << 11)
#define I2C_CR1_SWRST (1U << 15)

/* ------------------------------------------------------------------ */
/* I2C1 SR1 flags                                                       */
/* ------------------------------------------------------------------ */
#define I2C_SR1_SB    (1U << 0)   /* Start bit sent          */
#define I2C_SR1_ADDR  (1U << 1)   /* Address sent / matched  */
#define I2C_SR1_BTF   (1U << 2)   /* Byte transfer finished  */
#define I2C_SR1_RXNE  (1U << 6)   /* RX data register not empty */
#define I2C_SR1_TXE   (1U << 7)   /* TX data register empty  */

/* ------------------------------------------------------------------ */
/* I2C1 SR2 flags                                                       */
/* ------------------------------------------------------------------ */
#define I2C_SR2_BUSY  (1U << 1)   /* Bus busy                */

/* ------------------------------------------------------------------ */
/* Timing (APB1 = 16 MHz HSI, standard mode 100 kHz)                   */
/* ------------------------------------------------------------------ */
#define I2C_CR2_FREQ   16U   /* Must equal APB1 in MHz              */
#define I2C_CCR_VAL    80U   /* = PCLK / (2 × 100 kHz) = 80        */
#define I2C_TRISE_VAL  17U   /* = ceil(1000 ns × 16 MHz) + 1 = 17  */

/* ------------------------------------------------------------------ */
/* Timeout: ~200 000 loop iterations at 180 MHz core ≈ several ms      */
/* ------------------------------------------------------------------ */
#define I2C_TIMEOUT    200000U

/*
 * I2C_WAIT — busy-poll for 'cond' to become true.
 * If the timeout expires the enclosing function returns 1 (error).
 * Only use inside functions that return uint8_t.
 */
#define I2C_WAIT(cond)                              \
    do {                                            \
        uint32_t _t = I2C_TIMEOUT;                  \
        while (!(cond)) { if (!_t--) return 1U; }   \
    } while(0)

/* ================================================================== */
/* i2c1_init                                                           */
/* ================================================================== */
void i2c1_init(void)
{
    /* 1. Enable clocks */
    RCC->AHB1ENR |= I2C_GPIOB_EN;
    RCC->APB1ENR |= I2C_I2C1_EN;
    /* Barrier: APB peripheral clock has 2-cycle activation latency */
    { volatile uint32_t t = RCC->APB1ENR; (void)t; }

    /* 2. Configure PB6 (SCL) and PB7 (SDA)
     *    AF4, open-drain (I2C requirement), high-speed, internal pull-up
     *    (external 4.7 kΩ pull-ups should be fitted on the PCB) */

    /* MODER: bits [13:12] = 10 (PB6 alternate), bits [15:14] = 10 (PB7 alternate) */
    GPIOB->MODER &= ~((3U << 12) | (3U << 14));
    GPIOB->MODER |=  ((2U << 12) | (2U << 14));

    /* OTYPER: bit 6 and bit 7 = 1 (open-drain) */
    GPIOB->OTYPER |= (1U << 6) | (1U << 7);

    /* OSPEEDR: bits [13:12] = 11 (PB6 high), bits [15:14] = 11 (PB7 high) */
    GPIOB->OSPEEDR |= (3U << 12) | (3U << 14);

    /* PUPDR: bits [13:12] = 01 (PB6 pull-up), bits [15:14] = 01 (PB7 pull-up) */
    GPIOB->PUPDR &= ~((3U << 12) | (3U << 14));
    GPIOB->PUPDR |=  ((1U << 12) | (1U << 14));

    /* AFRL: PB6 bits [27:24] = 0100 (AF4), PB7 bits [31:28] = 0100 (AF4) */
    GPIOB->AFR[0] &= ~((0xFU << 24) | (0xFU << 28));
    GPIOB->AFR[0] |=  ((4U   << 24) | (4U   << 28));

    /* 3. Reset I2C1 peripheral */
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    /* 4. Configure I2C1 timing */
    I2C1->CR2   = I2C_CR2_FREQ;    /* peripheral frequency in MHz         */
    I2C1->CCR   = I2C_CCR_VAL;     /* SCL clock control: standard 100 kHz */
    I2C1->TRISE = I2C_TRISE_VAL;   /* maximum rise time                   */

    /* 5. Enable I2C1 */
    I2C1->CR1 |= I2C_CR1_PE;
}

/* ================================================================== */
/* i2c1_write_reg                                                      */
/* ================================================================== */
uint8_t i2c1_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t len)
{
    /* Wait until the bus is free */
    uint32_t t = I2C_TIMEOUT;
    while ((I2C1->SR2 & I2C_SR2_BUSY) && t) { t--; }
    if (!t) return 1U;

    /* Generate START */
    I2C1->CR1 |= I2C_CR1_START;
    I2C_WAIT(I2C1->SR1 & I2C_SR1_SB);

    /* Send slave address + WRITE (bit0 = 0) */
    I2C1->DR = (uint8_t)(addr << 1U);
    I2C_WAIT(I2C1->SR1 & I2C_SR1_ADDR);
    (void)I2C1->SR2;   /* reading SR2 clears the ADDR flag */

    /* Send register pointer */
    I2C1->DR = reg;
    I2C_WAIT(I2C1->SR1 & I2C_SR1_TXE);

    /* Send data bytes */
    for (uint8_t i = 0U; i < len; i++) {
        I2C1->DR = data[i];
        I2C_WAIT(I2C1->SR1 & I2C_SR1_TXE);
    }

    /* Wait for BTF (shift register empty) then generate STOP */
    I2C_WAIT(I2C1->SR1 & I2C_SR1_BTF);
    I2C1->CR1 |= I2C_CR1_STOP;
    return 0U;
}

/* ================================================================== */
/* i2c1_read_reg                                                       */
/* ================================================================== */
uint8_t i2c1_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    if (len == 0U) { return 0U; }

    /* Wait until the bus is free */
    uint32_t t = I2C_TIMEOUT;
    while ((I2C1->SR2 & I2C_SR2_BUSY) && t) { t--; }
    if (!t) return 1U;

    /* Clear POS; enable ACK for the upcoming reception */
    I2C1->CR1 &= ~I2C_CR1_POS;
    I2C1->CR1 |=  I2C_CR1_ACK;

    /* --- Phase 1: write register pointer --- */
    I2C1->CR1 |= I2C_CR1_START;
    I2C_WAIT(I2C1->SR1 & I2C_SR1_SB);

    I2C1->DR = (uint8_t)(addr << 1U);   /* address + WRITE */
    I2C_WAIT(I2C1->SR1 & I2C_SR1_ADDR);
    (void)I2C1->SR2;

    I2C1->DR = reg;
    I2C_WAIT(I2C1->SR1 & I2C_SR1_BTF); /* wait until reg byte is fully clocked */

    /* --- Phase 2: repeated START + address + READ --- */
    I2C1->CR1 |= I2C_CR1_START;
    I2C_WAIT(I2C1->SR1 & I2C_SR1_SB);

    I2C1->DR = (uint8_t)((addr << 1U) | 1U);  /* address + READ */
    I2C_WAIT(I2C1->SR1 & I2C_SR1_ADDR);

    /* --- Phase 3: receive bytes (sequence differs by length) --- */
    if (len == 1U) {
        /*
         * 1-byte read: NACK immediately, generate STOP before reading DR.
         * (STM32F4 I2C application note AN2824, Figure 2)
         */
        I2C1->CR1 &= ~I2C_CR1_ACK;    /* send NACK */
        (void)I2C1->SR2;               /* clear ADDR */
        I2C1->CR1 |= I2C_CR1_STOP;
        I2C_WAIT(I2C1->SR1 & I2C_SR1_RXNE);
        data[0] = (uint8_t)I2C1->DR;

    } else if (len == 2U) {
        /*
         * 2-byte read: use POS bit so that ACK/NACK applies to the byte
         * currently in the shift register, not the one in DR.
         * (STM32F4 RM0390, Section 18.3.3; AN2824 Figure 3)
         */
        I2C1->CR1 |= I2C_CR1_POS;     /* NACK applies to next byte after current shift */
        (void)I2C1->SR2;               /* clear ADDR */
        I2C1->CR1 &= ~I2C_CR1_ACK;    /* prepare NACK */
        I2C_WAIT(I2C1->SR1 & I2C_SR1_BTF); /* wait: DR = byte[0], SR = byte[1] */
        I2C1->CR1 |= I2C_CR1_STOP;
        data[0] = (uint8_t)I2C1->DR;
        data[1] = (uint8_t)I2C1->DR;
        I2C1->CR1 &= ~I2C_CR1_POS;    /* restore POS */

    } else {
        /*
         * N >= 3 byte read.
         * ACK is already set. Read bytes [0 .. N-4] one by one on RXNE,
         * then handle the final 3 bytes with the BTF sequence to ensure
         * the STOP is generated before the last byte is clocked in.
         * (STM32F4 RM0390, Section 18.3.3; AN2824 Figure 1)
         */
        (void)I2C1->SR2;               /* clear ADDR, ACK active */

        for (uint8_t i = 0U; i < (uint8_t)(len - 3U); i++) {
            I2C_WAIT(I2C1->SR1 & I2C_SR1_RXNE);
            data[i] = (uint8_t)I2C1->DR;
        }

        /* byte[N-3] in DR, byte[N-2] in shift register */
        I2C_WAIT(I2C1->SR1 & I2C_SR1_BTF);
        I2C1->CR1 &= ~I2C_CR1_ACK;        /* NACK byte[N-1] */
        data[len - 3U] = (uint8_t)I2C1->DR;

        /* byte[N-2] in DR, byte[N-1] received with NACK in shift register */
        I2C_WAIT(I2C1->SR1 & I2C_SR1_BTF);
        I2C1->CR1 |= I2C_CR1_STOP;
        data[len - 2U] = (uint8_t)I2C1->DR;
        data[len - 1U] = (uint8_t)I2C1->DR;
    }

    return 0U;
}
