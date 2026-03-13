#ifndef __TLE9012DQU_H__
#define __TLE9012DQU_H__

#include "stm32f4xx.h"
#include <stdint.h>

void TLE9012_dqu_Wakeup(void);
void TLE9012_Readback_Config(uint8_t *response_buff);
void Reset_Watch_dog_counter(void);
void Enable_Cell_Monitoring(uint16_t Number_of_Cells);
void Read_Cell_Voltages(uint8_t *buffer, uint8_t number_of_cells, uint16_t *cell_voltages_raw, float *cell_voltages);
void Set_UnderVoltage_Threshold(float UV_threshold);
void Set_OverVoltage_Threshold(float OV_Threshold);
void Activate_ERRORS(uint16_t Errors);
void Set_Undervoltage_Cells(uint16_t Number_of_cells, uint16_t reset);
uint16_t Read_UnderVoltage_Flags(void);

#endif