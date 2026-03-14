#include "TLE9012dqu.h"
#include "uart_dma.h"
#include <stddef.h>

extern volatile uint8_t g_rx_cmplt;
extern volatile uint8_t g_uart_cmplt;
extern volatile uint8_t g_tx_cmplt;

/* ---------- Private helpers ---------- */

static uint8_t tle9012_reverse_byte(uint8_t in);
static void tle9012_reverse_buf(uint8_t *buf, size_t len);
static uint8_t tle9012_crc8(const uint8_t *buf, size_t len);
static float tle9012_adc_to_voltage(uint16_t adc_raw);

static TLE9012_Status_t tle9012_wait_flag(volatile uint8_t *flag, uint32_t timeout);
static TLE9012_Status_t tle9012_tx_bytes(uint8_t *buf, size_t len);
static TLE9012_Status_t tle9012_rx_bytes(uint8_t *buf, size_t len);
static TLE9012_Status_t tle9012_write_reg16(TLE9012_Handle_t *dev, uint8_t reg, uint16_t data);
static TLE9012_Status_t tle9012_read_reg(TLE9012_Handle_t *dev, uint8_t reg, uint8_t *reply, size_t reply_len);
TLE9012_Status_t TLE9012_SetTemperatureConfig(TLE9012_Handle_t *dev,
                                              uint8_t nr_temp_sense,
                                              uint8_t i_ntc,
                                              uint16_t ext_ot_thr);
TLE9012_Status_t TLE9012_ReadExternalTemperature(TLE9012_Handle_t *dev, uint8_t channel);

/* ---------- Public API ---------- */

TLE9012_Status_t TLE9012_Init(TLE9012_Handle_t *dev, uint16_t num_cells)
{
    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    dev->num_cells = num_cells;
    return TLE9012_OK;
}

TLE9012_Status_t TLE9012_Wakeup(TLE9012_Handle_t *dev)
{
    (void)dev;

    uint8_t wake_signal[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    return tle9012_tx_bytes(wake_signal, sizeof(wake_signal));
}

TLE9012_Status_t TLE9012_ResetWatchdog(TLE9012_Handle_t *dev)
{
    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    return tle9012_write_reg16(dev, 0x3D, 0x007F);
}

TLE9012_Status_t TLE9012_EnableCellMonitoring(TLE9012_Handle_t *dev, uint16_t num_cells)
{
    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    if ((num_cells == 0U) || (num_cells > 12U))
    {
        return TLE9012_ERR_PARAM;
    }

    uint16_t packaged = 0U;
    for (uint16_t i = 0; i < num_cells; i++)
    {
        packaged |= (1U << (11U - i));
    }

    dev->num_cells = num_cells;
    return tle9012_write_reg16(dev, 0x01, packaged);
}

TLE9012_Status_t TLE9012_SetUnderVoltageThreshold(TLE9012_Handle_t *dev, float uv_threshold)
{
    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    uint16_t raw = (uint16_t)(uv_threshold / 0.004883227f);
    return tle9012_write_reg16(dev, 0x03, raw);
}

TLE9012_Status_t TLE9012_SetOverVoltageThreshold(TLE9012_Handle_t *dev, float ov_threshold)
{
    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    uint16_t raw = (uint16_t)(ov_threshold / 0.004883227f);
    return tle9012_write_reg16(dev, 0x02, raw);
}

TLE9012_Status_t TLE9012_SetUndervoltageCells(TLE9012_Handle_t *dev, uint16_t number_of_cells, uint16_t reset)
{
    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    if ((number_of_cells == 0U) || (number_of_cells > 12U))
    {
        return TLE9012_ERR_PARAM;
    }

    uint16_t packaged = 0U;
    for (uint16_t i = 0; i < number_of_cells; i++)
    {
        packaged |= ((reset & 0x1U) << (11U - i));
    }

    return tle9012_write_reg16(dev, 0x0C, packaged);
}

TLE9012_Status_t TLE9012_ActivateErrors(TLE9012_Handle_t *dev, uint16_t errors)
{
    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    return tle9012_write_reg16(dev, 0x0A, errors);
}

TLE9012_Status_t TLE9012_ReadbackConfig(TLE9012_Handle_t *dev, uint8_t *response, size_t len)
{
    if ((dev == NULL) || (response == NULL) || (len < 5U))
    {
        return TLE9012_ERR_PARAM;
    }

    return tle9012_read_reg(dev, 0x36, response, 5);
}

TLE9012_Status_t TLE9012_ReadUnderVoltageFlags(TLE9012_Handle_t *dev, uint16_t *flags)
{
    if ((dev == NULL) || (flags == NULL))
    {
        return TLE9012_ERR_PARAM;
    }

    TLE9012_Status_t st = tle9012_read_reg(dev, 0x0A, dev->rx_buf, 5);
    if (st != TLE9012_OK)
    {
        return st;
    }

    *flags = ((uint16_t)dev->rx_buf[2] << 8) | dev->rx_buf[3];
    return TLE9012_OK;
}

TLE9012_Status_t TLE9012_ReadCellVoltages(TLE9012_Handle_t *dev,
                                           uint16_t *cell_voltages_raw,
                                           float *cell_voltages,
                                           uint8_t number_of_cells)
{
    if ((dev == NULL) || (cell_voltages_raw == NULL) || (cell_voltages == NULL))
    {
        return TLE9012_ERR_PARAM;
    }

    if ((number_of_cells == 0U) || (number_of_cells > 12U))
    {
        return TLE9012_ERR_PARAM;
    }

    /* Start PCVM */
    dev->tx_buf[0] = 0x1E;
    dev->tx_buf[1] = 0x80;
    dev->tx_buf[2] = 0x18;
    dev->tx_buf[3] = 0xE0;
    dev->tx_buf[4] = 0x21;
    dev->tx_buf[5] = tle9012_crc8(dev->tx_buf, 5);

    TLE9012_Status_t st = tle9012_tx_bytes(dev->tx_buf, 6);
    if (st != TLE9012_OK)
    {
        return st;
    }

    st = tle9012_rx_bytes(dev->rx_buf, 1);
    if (st != TLE9012_OK)
    {
        return st;
    }

    delay_us(6000);

    for (uint8_t i = 0; i < number_of_cells; i++)
    {
        st = tle9012_read_reg(dev, (uint8_t)(0x19U + i), dev->rx_buf, 5);
        if (st != TLE9012_OK)
        {
            return st;
        }

        cell_voltages_raw[i] = ((uint16_t)dev->rx_buf[2] << 8) | dev->rx_buf[3];
        cell_voltages[i] = tle9012_adc_to_voltage(cell_voltages_raw[i]);
    }

    return TLE9012_OK;
}

TLE9012_Status_t TLE9012_SetTemperatureConfig(TLE9012_Handle_t *dev,
                                              uint8_t nr_temp_sense,
                                              uint8_t i_ntc,
                                              uint16_t ext_ot_thr)
{
    uint16_t data;

    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    nr_temp_sense &= 0x07U;   // bits 14:12
    i_ntc         &= 0x03U;   // bits 11:10
    ext_ot_thr    &= 0x03FFU; // bits 9:0

    data  = ((uint16_t)nr_temp_sense << 12);
    data |= ((uint16_t)i_ntc << 10);
    data |= ext_ot_thr;

    dev->temp_cfg.nr_temp_sense = nr_temp_sense;
    dev->temp_cfg.i_ntc = i_ntc;
    dev->temp_cfg.ext_ot_thr = ext_ot_thr;

    return tle9012_write_reg16(dev, 0x04, data);
}

TLE9012_Status_t TLE9012_ReadExternalTemperature(TLE9012_Handle_t *dev, uint8_t channel)
{
    uint16_t reg_word;
    TLE9012_Status_t st;

    if (dev == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    if (channel > 4U)
    {
        return TLE9012_ERR_PARAM;
    }

    /* EXT_TEMP_0..4 are at 0x29..0x2D */
    st = tle9012_read_reg(dev, (uint8_t)(0x29U + channel), dev->rx_buf, 5);
    if (st != TLE9012_OK)
    {
        return st;
    }

    reg_word = ((uint16_t)dev->rx_buf[2] << 8) | dev->rx_buf[3];

    dev->ext_temp[channel].result_raw =  reg_word        & 0x03FFU; /* bits 9:0  */
    dev->ext_temp[channel].intc       = (reg_word >> 10) & 0x03U;   /* bits 11:10 */
    dev->ext_temp[channel].pulldown   = (reg_word >> 12) & 0x01U;   /* bit 12 */
    dev->ext_temp[channel].valid      = (reg_word >> 13) & 0x01U;   /* bit 13 */
    dev->ext_temp[channel].pd_err     = (reg_word >> 14) & 0x01U;   /* bit 14 */

    return TLE9012_OK;
}

/* ---------- Private helpers ---------- */

static TLE9012_Status_t tle9012_write_reg16(TLE9012_Handle_t *dev, uint8_t reg, uint16_t data)
{
    dev->tx_buf[0] = 0x1E;
    dev->tx_buf[1] = 0x80;
    dev->tx_buf[2] = reg;
    dev->tx_buf[3] = (uint8_t)(data >> 8);
    dev->tx_buf[4] = (uint8_t)(data & 0xFFU);
    dev->tx_buf[5] = tle9012_crc8(dev->tx_buf, 5);

    TLE9012_Status_t st = tle9012_tx_bytes(dev->tx_buf, 6);
    if (st != TLE9012_OK)
    {
        return st;
    }

    return tle9012_rx_bytes(dev->rx_buf, 1);
}

static TLE9012_Status_t tle9012_read_reg(TLE9012_Handle_t *dev, uint8_t reg, uint8_t *reply, size_t reply_len)
{
    dev->tx_buf[0] = 0x1E;
    dev->tx_buf[1] = 0x00;
    dev->tx_buf[2] = reg;
    dev->tx_buf[3] = tle9012_crc8(dev->tx_buf, 3);

    TLE9012_Status_t st = tle9012_tx_bytes(dev->tx_buf, 4);
    if (st != TLE9012_OK)
    {
        return st;
    }

    return tle9012_rx_bytes(reply, reply_len);
}

static TLE9012_Status_t tle9012_tx_bytes(uint8_t *buf, size_t len)
{
    if ((buf == NULL) || (len == 0U))
    {
        return TLE9012_ERR_PARAM;
    }

    tle9012_reverse_buf(buf, len);

    dma2_stream7_uart_tx_config(buf, len);

    TLE9012_Status_t st = tle9012_wait_flag(&g_tx_cmplt, 1000000U);
    if (st != TLE9012_OK)
    {
        return st;
    }

    return tle9012_wait_flag(&g_uart_cmplt, 1000000U);
}

static TLE9012_Status_t tle9012_rx_bytes(uint8_t *buf, size_t len)
{
    if ((buf == NULL) || (len == 0U))
    {
        return TLE9012_ERR_PARAM;
    }

    dma2_stream2_uart_rx_config(buf, len);

    TLE9012_Status_t st = tle9012_wait_flag(&g_rx_cmplt, 1000000U);
    if (st != TLE9012_OK)
    {
        return st;
    }

    tle9012_reverse_buf(buf, len);
    return TLE9012_OK;
}

static TLE9012_Status_t tle9012_wait_flag(volatile uint8_t *flag, uint32_t timeout)
{
    if (flag == NULL)
    {
        return TLE9012_ERR_NULL;
    }

    while (!(*flag))
    {
        if (timeout == 0U)
        {
            return TLE9012_ERR_TIMEOUT;
        }
        timeout--;
    }

    *flag = 0U;
    return TLE9012_OK;
}

static void tle9012_reverse_buf(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        buf[i] = tle9012_reverse_byte(buf[i]);
    }
}

static uint8_t tle9012_reverse_byte(uint8_t in)
{
    uint8_t reversed = 0U;

    for (uint8_t i = 0; i < 8U; i++)
    {
        uint8_t bit = (uint8_t)((in >> i) & 0x01U);
        reversed |= (uint8_t)(bit << (7U - i));
    }

    return reversed;
}

static uint8_t tle9012_crc8(const uint8_t *buf, size_t len)
{
    uint8_t poly = 0x1D;
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= buf[i];

        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            if (crc & 0x80U)
            {
                crc = (uint8_t)((crc << 1) ^ poly);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    crc ^= 0xFFU;
    return crc;
}

static float tle9012_adc_to_voltage(uint16_t adc_raw)
{
    return 5.0f * (float)adc_raw / 65535.0f;
}