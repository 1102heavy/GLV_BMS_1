#ifndef __TLE9012DQU_H__
#define __TLE9012DQU_H__

#include "stm32f4xx.h"
#include <stdint.h>
#include <stddef.h>

typedef enum
{
    TLE9012_OK = 0,
    TLE9012_ERR_NULL,
    TLE9012_ERR_PARAM,
    TLE9012_ERR_TIMEOUT,
    TLE9012_ERR_IO
} TLE9012_Status_t;

typedef struct
{
    uint16_t num_cells;
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
} TLE9012_Handle_t;

/* Init / basic control */
TLE9012_Status_t TLE9012_Init(TLE9012_Handle_t *dev, uint16_t num_cells);
TLE9012_Status_t TLE9012_Wakeup(TLE9012_Handle_t *dev);
TLE9012_Status_t TLE9012_ResetWatchdog(TLE9012_Handle_t *dev);

/* Configuration */
TLE9012_Status_t TLE9012_EnableCellMonitoring(TLE9012_Handle_t *dev, uint16_t num_cells);
TLE9012_Status_t TLE9012_SetUnderVoltageThreshold(TLE9012_Handle_t *dev, float uv_threshold);
TLE9012_Status_t TLE9012_SetOverVoltageThreshold(TLE9012_Handle_t *dev, float ov_threshold);
TLE9012_Status_t TLE9012_SetUndervoltageCells(TLE9012_Handle_t *dev, uint16_t number_of_cells, uint16_t reset);
TLE9012_Status_t TLE9012_ActivateErrors(TLE9012_Handle_t *dev, uint16_t errors);

/* Readback */
TLE9012_Status_t TLE9012_ReadbackConfig(TLE9012_Handle_t *dev, uint8_t *response, size_t len);
TLE9012_Status_t TLE9012_ReadUnderVoltageFlags(TLE9012_Handle_t *dev, uint16_t *flags);
TLE9012_Status_t TLE9012_ReadCellVoltages(TLE9012_Handle_t *dev,
                                           uint16_t *cell_voltages_raw,
                                           float *cell_voltages,
                                           uint8_t number_of_cells);

#endif
