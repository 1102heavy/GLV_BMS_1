#ifndef I2C_H
#define I2C_H

#include <stdint.h>

/*
 * i2c.h — Bare-metal polling driver for I2C1
 *
 * Pins:  PB6 = SCL  (AF4)
 *        PB7 = SDA  (AF4)
 * Clock: APB1 = 16 MHz (HSI, no PLL)
 * Mode:  Standard-mode 100 kHz, open-drain
 */

void    i2c1_init(void);

/*
 * i2c1_write_reg — Write 'len' bytes to register 'reg' on device 'addr' (7-bit).
 * Returns 0 on success, 1 on timeout or bus error.
 */
uint8_t i2c1_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t len);

/*
 * i2c1_read_reg — Read 'len' bytes from register 'reg' on device 'addr' (7-bit).
 * Returns 0 on success, 1 on timeout or bus error.
 */
uint8_t i2c1_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len);

#endif /* I2C_H */
