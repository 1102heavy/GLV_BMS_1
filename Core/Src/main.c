
#include <stdio.h>
#include "uart_dma.h"
#include "TLE9012dqu.h"
#include <string.h>

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
float undervoltage_threshold = 2.9;
float overvoltage_threshold = 3.4;
uint16_t number_of_cells = 6;
uint16_t Errors_to_be_enabled = 0b0001000000100000;

// void SystemInit(void)
// {

//     /* Enable full access to CP10 and CP11 (single-precision FPU) */
//     SCB->CPACR |= (0xFU << 20);
//     __DSB();          /* data sync barrier  */
//     __ISB();          /* instr sync barrier */
// }


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


//	//Send wake up signal
	TLE9012_dqu_Wakeup();
	TLE9012_dqu_Wakeup();
	TLE9012_dqu_Wakeup();
//
//	//Wait some time for it to wake up
	//delay_us(100);
//
	//TLE9012_Readback_Config(back_signal);
//
	//Enable Cell monitoring
	Enable_Cell_Monitoring(number_of_cells);

	//Activate Error Pin
	Activate_ERRORS (Errors_to_be_enabled);

	//Set undervoltage threshold
	Set_UnderVoltage_Threshold(undervoltage_threshold);

	//Activate the Undervoltage fucntion of each cell
	Set_Undervoltage_Cells (12, reset);

	Set_OverVoltage_Threshold(overvoltage_threshold);

	//charge_interrupt_setup();

	//set_charge_interrupt();

	while(1)
	{
		Reset_Watch_dog_counter();
		Read_Cell_Voltages(Cell_Voltage_Reply, 12, Cell_voltages_raw, Cell_Voltages );
		//Undervoltage_Flags = Read_UnderVoltage_Flags();
    
    //delay for 5 seconds
    //delay_us(15000000);

    //Check if charge interrupt is high
    // if (charge_interrupt_is_high())
    // {
    //     charge_interrupt_low();
    // } else
    // {
    //     charge_interrupt_high();
    // }

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