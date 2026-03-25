
#include "uart_dma.h"
#include "TLE9012dqu.h"
#include "i2c.h"
#include "ina228.h"

extern volatile uint8_t g_rx_cmplt;
extern volatile uint8_t g_uart_cmplt;
extern volatile uint8_t g_tx_cmplt;

extern uint8_t tx_buff[6];
extern char uart_data_buffer[UART_DATA_BUFF_SIZE];

uint8_t signal_echo[3] = {0,0};
uint8_t back_signal[5];
uint8_t Cell_Voltage_Reply[5];
uint16_t Cell_voltages_raw[12];
uint16_t Undervoltage_Flags;
float Cell_Voltages[12];
uint16_t reset = 0;
float undervoltage_threshold = 2.7;
float overvoltage_threshold = 4.2;
uint16_t number_of_cells = 6;
uint16_t Errors_to_be_enabled = 0b0011000000100000;
uint16_t diag;

INA228_Data_t g_ina228;   /* current sensor readings */


int main(void)
{
	//Initialise Timer 5
	timer5_init();

	//Intialise the DMA (Give clock access)
	dma2_init();
	dma1_init();

	//uart1_rx_tx_init();
	uart1_rx_tx_half_duplex_init();
	//uart2_rx_tx_init();
	//uart2_rx_tx_half_duplex_init();

	/* Initialise I2C1 and INA228 current sensor */
	i2c1_init();
	ina228_init();


//	//Send wake up signal
	TLE9012_dqu_Wakeup();
	TLE9012_dqu_Wakeup();
	TLE9012_dqu_Wakeup();
//
//	//Wait some time for it to wake up
	delay_us(100);
//
	//TLE9012_Readback_Config(back_signal);
//
	//Enable Cell monitoring
	Enable_Cell_Monitoring(number_of_cells);


//
//	/* Disable again */
//	Disable_All_Balancing();

	//Activate Error Pin
	Activate_ERRORS (Errors_to_be_enabled);

	//Set undervoltage threshold
	Set_UnderVoltage_Threshold(undervoltage_threshold);

	//Activate the Undervoltage fucntion of each cell
	//Set_Undervoltage_Cells (12, reset);

	Set_OverVoltage_Threshold(overvoltage_threshold);

	/* Configure the balancing current thresholds once at startup.
	 * Under-current threshold 0x13, over-current threshold 0x30 — written
	 * to BAL_CURR_THR (reg 0x15) so the IC can auto-stop balancing if the
	 * balancing resistor current falls out of the expected window. */
	Set_Balancing_Current_Threshold(0x13, 0x30);

	/* Start with all balancing off; Balance_To_Minimum in the main loop
	 * will enable channels as needed. */
	Disable_All_Balancing();

	charge_interrupt_setup();

	//set_charge_interrupt();

	while(1)
	{
		Reset_Watch_dog_counter();

		/* Stop balancing before measuring: the discharge current (~85 mA)
		 * flows through the 10 Ω sense resistors and drops the U-pin voltage
		 * at the TLE9012, causing equal-and-opposite errors on adjacent cells.
		 * Wait 5 ms for the RC filter (10 Ω × 330 nF) to fully settle. */
		Disable_All_Balancing();
		delay_us(5000);

		Read_Cell_Voltages(Cell_Voltage_Reply, 12, Cell_voltages_raw, Cell_Voltages );
		Undervoltage_Flags = Read_Undervoltage_Flags();

		/* Read pack current, bus voltage, power, and die temperature */
		(void)ina228_read_all(&g_ina228);

		/* Re-enable passive balancing after measurements are captured.
		 * Physical cell mapping for this 6S pack:
		 *   Real cells sit at indices 6–11 (PCVM_6..11, U6-U7 .. U11-U12).
		 *   bit_offset=6 aligns cell_voltages[i] with BAL_SETTINGS bit (6+i). */
		Balance_To_Minimum(&Cell_Voltages[6], number_of_cells, 6);

		delay_us(295000);  /* balance for ~295 ms; total cycle ≈ 300 ms */

		diag = Read_General_Diagnostics();
//
//
//		for (int i = 0; i < number_of_cells ; i++)
//		{
//			if (Cell_Voltages[11 - i] > 3.3)
//			{
//				set_charge_interrupt();
//			}
//		}

		if(g_rx_cmplt)
		{
			g_rx_cmplt = 0;
		}

//
//		if (g_tx_cmplt)
//		{
//		    TLE9012_dqu_Wakeup();
//			g_tx_cmplt = 0;
//			dma2_stream2_uart_rx_config(back_signal, 2);
//			while(!g_rx_cmplt){}
//		}


	}
}
