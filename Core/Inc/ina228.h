#ifndef INA228_H
#define INA228_H

#include <stdint.h>

/*
 * ina228.h — Driver for the Texas Instruments INA228AIDGST
 *
 * The INA228 is an 85 V, 20-bit power/energy/charge monitor with I2C.
 * It measures bus voltage, shunt voltage (→ current), and die temperature.
 *
 * I2C address: 0x40  (A1 = GND, A0 = GND)
 * Interface  : I2C1 on PB6 (SCL) / PB7 (SDA) via i2c.h
 */

/* ------------------------------------------------------------------ */
/* I2C slave address (7-bit, A1=GND, A0=GND)                           */
/* ------------------------------------------------------------------ */
#define INA228_I2C_ADDR      0x40U

/* ------------------------------------------------------------------ */
/* Register addresses                                                   */
/* ------------------------------------------------------------------ */
#define INA228_REG_CONFIG    0x00U   /* Device configuration        (16-bit) */
#define INA228_REG_ADC_CFG   0x01U   /* ADC configuration           (16-bit) */
#define INA228_REG_SHUNT_CAL 0x02U   /* Shunt calibration           (16-bit) */
#define INA228_REG_VSHUNT    0x04U   /* Shunt voltage result        (24-bit) */
#define INA228_REG_VBUS      0x05U   /* Bus voltage result          (24-bit) */
#define INA228_REG_TEMP      0x06U   /* Die temperature result      (16-bit) */
#define INA228_REG_CURRENT   0x07U   /* Calculated current          (24-bit) */
#define INA228_REG_POWER     0x08U   /* Calculated power            (24-bit) */
#define INA228_REG_DIAG_ALRT 0x0BU   /* Diagnostics and alert flags (16-bit) */
#define INA228_REG_MFR_ID    0x3EU   /* Manufacturer ID  = 0x5449 (TI)      */
#define INA228_REG_DEV_ID    0x3FU   /* Device ID        = 0x2281           */

/* ------------------------------------------------------------------ */
/* Hardware configuration — adjust to match PCB schematic               */
/*                                                                      */
/*   INA228_RSHUNT_OHMS   : shunt resistor value in ohms               */
/*   INA228_MAX_CURRENT_A : maximum expected pack current in amperes    */
/*                                                                      */
/* For a 6S3P pack (LG M58T, 20 A max per cell × 3P = 60 A) the shunt */
/* is typically 1–10 mΩ. Verify against the BMS PCB schematic before   */
/* flashing.                                                            */
/* ------------------------------------------------------------------ */
#define INA228_RSHUNT_OHMS   0.005f    /* 5 mΩ — CHECK PCB VALUE   */
#define INA228_MAX_CURRENT_A 20.0f     /* 20 A  — CHECK PCB VALUE  */

/* ------------------------------------------------------------------ */
/* Derived calibration constant                                          */
/*                                                                      */
/*   CURRENT_LSB = MAX_CURRENT / 2^19                                  */
/*   SHUNT_CAL   = 819.2e6 × CURRENT_LSB × R_SHUNT   (ADCRANGE = 0)  */
/* ------------------------------------------------------------------ */
#define INA228_CURRENT_LSB   (INA228_MAX_CURRENT_A / 524288.0f)   /* A/bit  */

/* ------------------------------------------------------------------ */
/* Fixed LSBs from INA228 datasheet (SBOS886B, Table 7)                */
/*                                                                      */
/*   VBUS    : 195.3125 µV/bit  (ADCRANGE = 0, 20-bit unsigned)        */
/*   TEMP    : 7.8125 m°C/bit   (12-bit signed, bits[15:4])            */
/*   POWER   : 3.2 × CURRENT_LSB W/bit (24-bit unsigned)               */
/* ------------------------------------------------------------------ */
#define INA228_VBUS_LSB_V    195.3125e-6f
#define INA228_TEMP_LSB_C    7.8125e-3f
#define INA228_POWER_COEFF   3.2f

/* ------------------------------------------------------------------ */
/* Result structure                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    float bus_voltage_v;    /* Pack bus voltage (V)              */
    float current_a;        /* Pack current, positive = discharge */
    float power_w;          /* Instantaneous power (W)           */
    float temperature_c;    /* INA228 die temperature (°C)       */
} INA228_Data_t;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

/*
 * ina228_init — Write ADC_CONFIG and SHUNT_CAL registers.
 * Call once after i2c1_init().
 */
void ina228_init(void);

/*
 * ina228_read_all — Read bus voltage, current, power, and temperature
 * into *out in a single burst of four I2C transactions.
 * Returns 0 on success; non-zero if any I2C transaction timed out.
 */
uint8_t ina228_read_all(INA228_Data_t *out);

#endif /* INA228_H */
