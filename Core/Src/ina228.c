#include "ina228.h"
#include "i2c.h"

/* ================================================================== */
/* ina228_init                                                          */
/* ================================================================== */
void ina228_init(void)
{
    uint8_t data[2];

    /*
     * ADC_CONFIG register (0x01) — 16-bit
     *
     *   bits [15:12]  MODE    = 0b1111  Continuous: bus + shunt + temperature
     *   bits [11:9]   VBUSCT  = 0b100   Bus voltage conversion time  = 1.1 ms
     *   bits [8:6]    VSHCT   = 0b100   Shunt voltage conversion time = 1.1 ms
     *   bits [5:3]    VTCT    = 0b100   Temperature conversion time   = 1.1 ms
     *   bits [2:0]    AVG     = 0b000   Averaging count = 1 (no averaging)
     *
     * Total round-trip per sample ≈ 3.3 ms; suitable for a 10 ms task period.
     * Value = 0xF920
     */
    data[0] = 0xF9U;
    data[1] = 0x20U;
    (void)i2c1_write_reg(INA228_I2C_ADDR, INA228_REG_ADC_CFG, data, 2U);

    /*
     * SHUNT_CAL register (0x02) — 16-bit
     *
     *   SHUNT_CAL = round( 819.2e6 × CURRENT_LSB × R_SHUNT )   [ADCRANGE = 0]
     *
     * With INA228_MAX_CURRENT_A = 20 A and INA228_RSHUNT_OHMS = 5 mΩ:
     *   CURRENT_LSB = 20 / 524288 = 38.147 µA/bit
     *   SHUNT_CAL   = 819.2e6 × 38.147e-6 × 0.005 = 156.25 → 156
     *
     * NOTE: update INA228_RSHUNT_OHMS and INA228_MAX_CURRENT_A in ina228.h
     *       to match the actual PCB shunt resistor value.
     */
    uint16_t shunt_cal = (uint16_t)(819.2e6f * INA228_CURRENT_LSB * INA228_RSHUNT_OHMS + 0.5f);
    data[0] = (uint8_t)(shunt_cal >> 8U);
    data[1] = (uint8_t)(shunt_cal);
    (void)i2c1_write_reg(INA228_I2C_ADDR, INA228_REG_SHUNT_CAL, data, 2U);
}

/* ================================================================== */
/* ina228_read_all                                                      */
/* ================================================================== */
uint8_t ina228_read_all(INA228_Data_t *out)
{
    uint8_t raw[3];
    uint8_t err = 0U;

    /* ---- Bus voltage (VBUS, 0x05) --------------------------------- */
    /*
     * 3-byte register, 20-bit unsigned in bits [23:4], bits [3:0] = 0.
     * LSB = 195.3125 µV.
     *
     *   raw_24 = (B0 << 16) | (B1 << 8) | B2
     *   counts = raw_24 >> 4
     *   V_bus  = counts × 195.3125e-6
     */
    err |= i2c1_read_reg(INA228_I2C_ADDR, INA228_REG_VBUS, raw, 3U);
    {
        uint32_t v = ((uint32_t)raw[0] << 16U) |
                     ((uint32_t)raw[1] <<  8U) |
                      (uint32_t)raw[2];
        out->bus_voltage_v = (float)(v >> 4U) * INA228_VBUS_LSB_V;
    }

    /* ---- Current (CURRENT, 0x07) ---------------------------------- */
    /*
     * 3-byte register, 20-bit two's-complement in bits [23:4], bits [3:0] = 0.
     *
     * Sign-extension trick:
     *   Place the 24-bit value in the upper 24 bits of an int32 by shifting
     *   left by 8.  The original sign bit (bit 23) lands at bit 31, giving
     *   correct int32 sign extension.  Then arithmetic right-shift by 12
     *   (= 8 + 4) removes the original lower 4 zeros AND the padding byte,
     *   yielding the 20-bit signed measurement at int32 bits [19:0].
     *
     *   I_pack (A) = signed_20bit × CURRENT_LSB
     *
     * Positive current = current flowing into IN+ (discharge for this design).
     */
    err |= i2c1_read_reg(INA228_I2C_ADDR, INA228_REG_CURRENT, raw, 3U);
    {
        int32_t c = (int32_t)(((uint32_t)raw[0] << 24U) |
                               ((uint32_t)raw[1] << 16U) |
                               ((uint32_t)raw[2] <<  8U));
        out->current_a = (float)(c >> 12) * INA228_CURRENT_LSB;
    }

    /* ---- Power (POWER, 0x08) --------------------------------------- */
    /*
     * 3-byte register, 24-bit unsigned.
     * P (W) = raw × 3.2 × CURRENT_LSB
     */
    err |= i2c1_read_reg(INA228_I2C_ADDR, INA228_REG_POWER, raw, 3U);
    {
        uint32_t p = ((uint32_t)raw[0] << 16U) |
                     ((uint32_t)raw[1] <<  8U) |
                      (uint32_t)raw[2];
        out->power_w = (float)p * INA228_POWER_COEFF * INA228_CURRENT_LSB;
    }

    /* ---- Die temperature (TEMP, 0x06) ----------------------------- */
    /*
     * 2-byte register, 12-bit two's-complement in bits [15:4], bits [3:0] = 0.
     * Arithmetic right-shift of int16 by 4 sign-extends correctly.
     * LSB = 7.8125 m°C.
     *
     *   T (°C) = (int16_t)(raw_16) >> 4  ×  7.8125e-3
     */
    err |= i2c1_read_reg(INA228_I2C_ADDR, INA228_REG_TEMP, raw, 2U);
    {
        int16_t t = (int16_t)(((uint16_t)raw[0] << 8U) | raw[1]);
        out->temperature_c = (float)(t >> 4) * INA228_TEMP_LSB_C;
    }

    return err;
}
