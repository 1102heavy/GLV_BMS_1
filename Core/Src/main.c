#include "TLE9012dqu.h"
#include "uart_dma.h"

TLE9012_Handle_t bms;

uint16_t cell_raw[12];
float cell_v[12];
float temperature[5];
uint16_t uv_flags;

int main(void)
{
    timer5_init();
    dma2_init();
    dma1_init();
    uart1_rx_tx_half_duplex_init();

    TLE9012_Init(&bms, 12);

    TLE9012_Wakeup(&bms);
    TLE9012_Wakeup(&bms);
    TLE9012_Wakeup(&bms);

    TLE9012_EnableCellMonitoring(&bms, 12);
    TLE9012_ActivateErrors(&bms, 0b0001000000100000);
    TLE9012_SetUnderVoltageThreshold(&bms, 2.9f);
    TLE9012_SetUndervoltageCells(&bms, 12, 0);
    TLE9012_SetOverVoltageThreshold(&bms, 3.4f);

    while (1)
    {
        TLE9012_ResetWatchdog(&bms);
        TLE9012_ReadCellVoltages(&bms, cell_raw, cell_v, 12);
        TLE9012_ReadUnderVoltageFlags(&bms, &uv_flags);

		/* read TMP2 */
		if (TLE9012_ReadExternalTemperature(&bms, 2) == TLE9012_OK)
		{
			if (bms.ext_temp[2].valid)
			{
				/* use bms.ext_temp[2].result_raw */
			}
		}
    }
}