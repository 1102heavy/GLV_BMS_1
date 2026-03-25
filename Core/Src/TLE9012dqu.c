#include "TLE9012dqu.h"
#include "uart_dma.h"
#include <stddef.h>

uint8_t msb_first_converter(uint8_t old_byte );
uint8_t CRC8_Calc(const uint8_t *buf, size_t len);
float ADC_CONVERSION (uint16_t adc_raw);

extern volatile uint8_t g_rx_cmplt;
extern volatile uint8_t g_uart_cmplt;
extern volatile uint8_t g_tx_cmplt;

uint8_t write_reply_frame[1];
uint8_t Reset_Watchdog_counter_Frame[6];
uint8_t Readback_config_Register_Frame[4];
//Test Variable
uint8_t Test;
uint8_t Flags_Frame[5];
uint8_t Flags_Frame_after[5];
uint16_t UnderVoltage_Flags;

uint8_t Balancing_Flags_Frame[5];
uint8_t Balancing_Flags_Frame_after[5];
uint16_t Balancing_Cells_Selected;

uint16_t Read_General_Diagnostics(void);

void TLE9012_dqu_Wakeup(void)
{
	// Initialize the entire array with the required data.
	uint8_t wake_signal[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

	for (int i = 0; i < sizeof(wake_signal); i++)
	{
		//Convert to MSB first for each byte
		wake_signal[i] = msb_first_converter(wake_signal[i]);
	}

	//Send 0xAA 0XAA two times on USART2
	dma2_stream7_uart_tx_config(wake_signal, 6);
	//dma1_stream6_uart_tx_config(wake_signal, 2);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;
}

void TLE9012_Readback_Config(uint8_t *response_buff)
{
	// Initialize the entire array with the required data.
//	uint8_t Readback_config_Register_Frame[4];


	//Fill up the Read back config Register buffer
	Readback_config_Register_Frame[0] = 0x1E;
	Readback_config_Register_Frame[1] = 0x00;
	Readback_config_Register_Frame[2] = 0x36;
	// Calculate the CRC8
	Readback_config_Register_Frame[3] = CRC8_Calc(Readback_config_Register_Frame, sizeof(Readback_config_Register_Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Readback_config_Register_Frame); i++)
	{
		//Convert to MSB first for each byte
		Readback_config_Register_Frame[i] = msb_first_converter(Readback_config_Register_Frame[i]);
	}


	//Send Readback config Register Frame
	dma2_stream7_uart_tx_config(Readback_config_Register_Frame, 4);
	//dma1_stream6_uart_tx_config(Readback_config_Register_Frame, 4);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(response_buff, 5);
	//dma1_stream5_uart_rx_config(response_buff, 5);
	//delay_us(10);

	for (int i = 0; i < sizeof(response_buff); i++)
	{
		//Convert to MSB first for each byte
		response_buff[i] = msb_first_converter(response_buff[i]);
	}
	//Wait for receive to be completed
	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;



	//delay_us(500);



	//Wait for uart transfer complete flag
//	while (!g_uart_cmplt)
//	{
//
//	}
//	g_uart_cmplt = 0;
}

void Enable_Cell_Monitoring (uint16_t Number_of_Cells)
{
	//package the data to be sent
	uint16_t packaged_data_number_of_cells;
	for (int i = 0; i < Number_of_Cells; i++)
	{
		//Shift in 1 bit from the left
		packaged_data_number_of_cells |= 1u << (11 - i);
	}
	// Initialize the entire array with the required data.
	uint8_t Write_Part_Config_Register_Frame[6];

	//Fill up the Write Part Config Register Frame
	Write_Part_Config_Register_Frame[0] = 0x1E;
	Write_Part_Config_Register_Frame[1] = 0x80;
	Write_Part_Config_Register_Frame[2] = 0x01;
	Write_Part_Config_Register_Frame[3] = packaged_data_number_of_cells >> 8;
	Write_Part_Config_Register_Frame[4] = 0xFF & packaged_data_number_of_cells;
	Write_Part_Config_Register_Frame[5] = CRC8_Calc(Write_Part_Config_Register_Frame, sizeof(Write_Part_Config_Register_Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Write_Part_Config_Register_Frame); i++)
	{
		//Convert to MSB first for each byte
		Write_Part_Config_Register_Frame[i] = msb_first_converter(Write_Part_Config_Register_Frame[i]);
	}

	//Send to activate 12 cells monitoring
	dma2_stream7_uart_tx_config(Write_Part_Config_Register_Frame, 6);
	//dma1_stream6_uart_tx_config(Write_Part_Config_Register_Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Wait for the "Write" Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);
	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;



}

void Read_Cell_Voltages(uint8_t *buffer, uint8_t number_of_cells, uint16_t *cell_voltages_raw, float *cell_voltages)
{
	//Initialise the Entire Array with the rewquired data
	uint8_t PCVM_Set_Up[6];

	//Fill up the PCVM set up frame
	PCVM_Set_Up[0] = 0x1E;
	PCVM_Set_Up[1] = 0x80;
	PCVM_Set_Up[2] = 0x18;
	PCVM_Set_Up[3] = 0xE0;
	PCVM_Set_Up[4] = 0x21;
	PCVM_Set_Up[5] = CRC8_Calc(PCVM_Set_Up, sizeof(PCVM_Set_Up) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(PCVM_Set_Up); i++)
	{
		PCVM_Set_Up[i] = msb_first_converter(PCVM_Set_Up[i]);
	}

	//Send to start voltage measurement via pcvm
	dma2_stream7_uart_tx_config(PCVM_Set_Up, 6);
	//dma1_stream6_uart_tx_config(PCVM_Set_Up, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Wait for Write message reply to be completed
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;

	//Wait for uart transfer complete flag
//	while (!g_uart_cmplt){}
//	g_uart_cmplt = 0;

	//delay for 6ms to wait for calculation of voltage
	delay_us(6000);

	//Send Read Request for each of the cells
	for (int i = 0; i < number_of_cells ; i++ )
	{
		uint8_t Read_Request_Frame[4];

		//Fill up the Read_Request_Frame
		Read_Request_Frame[0] = 0x1E;
		Read_Request_Frame[1] = 0x00;
		Read_Request_Frame[2] = 0x19 + i;
		Read_Request_Frame[3] = CRC8_Calc(Read_Request_Frame, sizeof(Read_Request_Frame) - 1);

		//Convert to MSB for each byte
		for (int j = 0; j < sizeof(Read_Request_Frame); j++)
		{
			Read_Request_Frame[j] = msb_first_converter(Read_Request_Frame[j]);
		}

		//Send Read Request to Receive cell voltage
		dma2_stream7_uart_tx_config(Read_Request_Frame, 4);
		//dma1_stream6_uart_tx_config(Read_Request_Frame, 4);

		//Wait for the transfer to be completed
		while (!g_tx_cmplt)
		{

		}
		g_tx_cmplt = 0;

		//Wait for uart transfer complete flag
		while (!g_uart_cmplt){}
		g_uart_cmplt = 0;


		//Receive the Reply sent on UART
		dma2_stream2_uart_rx_config(buffer, 5);
		//dma1_stream5_uart_rx_config(buffer, 5);

		//Wait for the reply to be received
		while (!g_rx_cmplt)
		{

		}
		g_rx_cmplt = 0;

		//Wait for uart transfer complete flag
//		while (!g_uart_cmplt){}
//		g_uart_cmplt = 0;


		//Reconvert back to LSB first
		for (int k = 0; k < 5; k++)
		{
			buffer[k] = msb_first_converter(buffer[k]);
		}
		//Retreive cell voltage
		cell_voltages_raw[i] = buffer[2] << 8 | buffer[3];

		//ADC conversion of voltages
		cell_voltages[i] = ADC_CONVERSION(cell_voltages_raw[i]);
	}
}

void Reset_Watch_dog_counter(void)
{
	// Initialize the entire array with the required data.
	//uint8_t Reset_Watchdog_counter_Frame[6];

	//Fill up the Reset Watch dog counter Register Frame
	Reset_Watchdog_counter_Frame[0] = 0x1E;
	Reset_Watchdog_counter_Frame[1] = 0x80;
	Reset_Watchdog_counter_Frame[2] = 0x3D;
	Reset_Watchdog_counter_Frame[3] = 0x00;
	Reset_Watchdog_counter_Frame[4] = 0x7F;
	Reset_Watchdog_counter_Frame[5] = CRC8_Calc(Reset_Watchdog_counter_Frame, sizeof(Reset_Watchdog_counter_Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Reset_Watchdog_counter_Frame); i++)
	{
		//Convert to MSB first for each byte
		Reset_Watchdog_counter_Frame[i] = msb_first_converter(Reset_Watchdog_counter_Frame[i]);
	}

	//Send to reset watchdog counter
	dma2_stream7_uart_tx_config(Reset_Watchdog_counter_Frame, 6);
	//dma1_stream6_uart_tx_config(Reset_Watchdog_counter_Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;

//	Wait for uart transfer complete flag
//	while (!g_uart_cmplt){}
//	g_uart_cmplt = 0;

}

void Set_UnderVoltage_Threshold(float UV_threshold)
{
	//Calculate the uv thr in terms of packaging data
	uint16_t packaged_uv_threshold = UV_threshold / 0.004883227;
	uint8_t Frame[6];

	//Initialise the Required Array


	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x03;
	Frame[3] = packaged_uv_threshold >> 8;
	Frame[4] = 0b11111111 & packaged_uv_threshold;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//Send to set Undervoltage Threshold
	//dma1_stream6_uart_tx_config(Frame, 6);
	dma2_stream7_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;

	//Wait for uart transfer complete flag
//	while (!g_uart_cmplt){}
//	g_uart_cmplt = 0;

	//Convert to LSB first
	write_reply_frame[0] = msb_first_converter(write_reply_frame[0]);

}

void Set_Undervoltage_Cells (uint16_t Number_of_cells, uint16_t reset)
{
	//Package the data field of the number of cells
	uint16_t Package_number_of_cells;
	for (int i = 0; i < Number_of_cells; i++)
	{
		Package_number_of_cells |= reset << (11-i);
	}

	//Initialise the Required Array
	uint8_t Frame[6];

	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x0C;
	Frame[3] = Package_number_of_cells >> 8;
	Frame[4] = 0xFF & Package_number_of_cells;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//dma1_stream6_uart_tx_config(Frame, 6);
	dma2_stream7_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;

}

uint16_t Read_Undervoltage_Flags(void)
{
	//Send Read Request
	uint8_t Frame[4];



	Frame[0] = 0x1E;
	Frame[1] = 0x00;
	Frame[2] = 0x0A;
	Frame[3] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}



	//dma1_stream6_uart_tx_config(Frame, 6);
	dma2_stream7_uart_tx_config(Frame, 4);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;


	//Receive the flags
	dma2_stream2_uart_rx_config(Flags_Frame, 5);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;


	//Convert to MSB first for each byte
	Flags_Frame_after[0] = msb_first_converter(Flags_Frame[0]);
	Flags_Frame_after[1] = msb_first_converter(Flags_Frame[1]);
	Flags_Frame_after[2] = msb_first_converter(Flags_Frame[2]);
	Flags_Frame_after[3] = msb_first_converter(Flags_Frame[3]);
	Flags_Frame_after[4] = msb_first_converter(Flags_Frame[4]);

	UnderVoltage_Flags = Flags_Frame_after[2] << 8 | Flags_Frame_after[3];
	return UnderVoltage_Flags;
}

void Set_OverVoltage_Threshold(float OV_Threshold)
{

	//Calculate the uv thr in terms of packaging data
	uint16_t packaged_ov_threshold = OV_Threshold / 0.004883227;

	//Initialise the Required Array
	uint8_t Frame[6];

	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x02;
	Frame[3] = packaged_ov_threshold >> 8;
	Frame[4] = 0b11111111 & packaged_ov_threshold;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//Send to set Undervoltage Threshold
	//dma1_stream6_uart_tx_config(Frame, 6);
	dma2_stream7_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;


}

void Activate_ERRORS(uint16_t Errors)
{
	//Initialise the Required Array
	uint8_t Frame[6];

	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x0A;
	Frame[3] = Errors >> 8;
	Frame[4] = 0xFF & Errors;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//Send to set Undervoltage Threshold
	//dma1_stream6_uart_tx_config(Frame, 6);
	dma2_stream7_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;

	//Wait for uart transfer complete flag
//	while (!g_uart_cmplt){}
//	g_uart_cmplt = 0;

}

void Set_Balancing_Current_Threshold(uint16_t Under_Current_Threshold, uint16_t Over_Current_Threshold)
{
	//Package both thresholds into 16-bit data field
	//Upper byte  : under-current threshold
	//Lower byte  : over-current threshold
	uint16_t Packaged_Balancing_Current_Threshold = (Under_Current_Threshold << 8) | (0xFF & Over_Current_Threshold);

	//Initialise the Required Array
	uint8_t Frame[6];

	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x15;
	Frame[3] = Packaged_Balancing_Current_Threshold >> 8;
	Frame[4] = 0xFF & Packaged_Balancing_Current_Threshold;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//Send Frame
	dma2_stream7_uart_tx_config(Frame, 6);
	//dma1_stream6_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;
}

void Set_Balancing_Cells(uint16_t Number_of_cells, uint16_t reset)
{
	//Package the data field of the number of cells
	//BAL_SETTINGS uses one bit per cell for up to 12 cells
	uint16_t Package_number_of_cells = 0;

	for (int i = 0; i < Number_of_cells; i++)
	{
		Package_number_of_cells |= reset << i;
	}

	//Mask to 12 valid balancing bits only
	Package_number_of_cells &= 0x0FFF;

	//Store selected balancing mask
	Balancing_Cells_Selected = Package_number_of_cells;

	//Initialise the Required Array
	uint8_t Frame[6];

	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x16;
	Frame[3] = Package_number_of_cells >> 8;
	Frame[4] = 0xFF & Package_number_of_cells;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//Send Frame
	dma2_stream7_uart_tx_config(Frame, 6);
	//dma1_stream6_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;
}

void Enable_Balancing_on_Cell(uint8_t Cell_Number)
{
	//Valid cells are 0 to 11
	if (Cell_Number > 11)
	{
		return;
	}

	//Set the requested bit
	Balancing_Cells_Selected |= (1U << Cell_Number);

	//Initialise the Required Array
	uint8_t Frame[6];

	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x16;
	Frame[3] = Balancing_Cells_Selected >> 8;
	Frame[4] = 0xFF & Balancing_Cells_Selected;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//Send Frame
	dma2_stream7_uart_tx_config(Frame, 6);
	//dma1_stream6_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;
}

void Disable_Balancing_on_Cell(uint8_t Cell_Number)
{
	//Valid cells are 0 to 11
	if (Cell_Number > 11)
	{
		return;
	}

	//Clear the requested bit
	Balancing_Cells_Selected &= ~(1U << Cell_Number);

	//Initialise the Required Array
	uint8_t Frame[6];

	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x16;
	Frame[3] = Balancing_Cells_Selected >> 8;
	Frame[4] = 0xFF & Balancing_Cells_Selected;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//Send Frame
	dma2_stream7_uart_tx_config(Frame, 6);
	//dma1_stream6_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;
}

void Disable_All_Balancing(void)
{
	//Clear all balancing bits
	Balancing_Cells_Selected = 0x0000;

	//Initialise the Required Array
	uint8_t Frame[6];

	//Fill up the frame
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x16;
	Frame[3] = 0x00;
	Frame[4] = 0x00;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	//Convert to MSB first for each byte
	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	//Send Frame
	dma2_stream7_uart_tx_config(Frame, 6);
	//dma1_stream6_uart_tx_config(Frame, 6);

	//Wait for the transfer to be completed
	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	//Wait for uart transfer complete flag
	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	//Receive Reply
	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	//dma1_stream5_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;
}

uint16_t Auto_Balance_Cells_With_Offset(float *cell_voltages, uint8_t number_of_cells, uint8_t starting_cell, float delta_voltage, float minimum_cell_voltage)
{
	float minimum_voltage;
	uint16_t Balancing_Mask = 0;

	if (number_of_cells == 0)
	{
		Disable_All_Balancing();
		return 0;
	}

	if ((starting_cell + number_of_cells) > 12)
	{
		number_of_cells = 12 - starting_cell;
	}

	minimum_voltage = cell_voltages[starting_cell];

	for (int i = starting_cell + 1; i < (starting_cell + number_of_cells); i++)
	{
		if (cell_voltages[i] < minimum_voltage)
		{
			minimum_voltage = cell_voltages[i];
		}
	}

	for (int i = starting_cell; i < (starting_cell + number_of_cells); i++)
	{
		if ((cell_voltages[i] >= minimum_cell_voltage) && ((cell_voltages[i] - minimum_voltage) >= delta_voltage))
		{
			Balancing_Mask |= 1U << i;
		}
	}

	Balancing_Cells_Selected = Balancing_Mask & 0x0FFF;

	uint8_t Frame[6];

	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x16;
	Frame[3] = Balancing_Cells_Selected >> 8;
	Frame[4] = 0xFF & Balancing_Cells_Selected;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	dma2_stream7_uart_tx_config(Frame, 6);

	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	dma2_stream2_uart_rx_config(write_reply_frame, 1);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;

	return Balancing_Cells_Selected;
}



uint8_t CRC8_Calc(const uint8_t *buf, size_t len)
{
	uint8_t poly = 0x1D;
	uint8_t crc = 0xFF;

	for (int bytes = 0; bytes < len; bytes++)
	{
		//XOR with the first byte of the frame and then continue with the rest of the bytes in the frame
		crc = crc ^ *buf++;

		for(int bit = 0; bit < 8; bit++)
		{
			//Check if MSB is 1
			if(crc & 0x80)
			{
				//Shift left by 1 position
				crc = crc << 1;

				//XOR with the poly
				crc = crc ^ poly;
			}
			else
			{
				//MSB is 0 so just shift left
				crc = crc << 1;
			}
		}
	}

	//Final XOR with 0xFF
	crc = crc ^ 0xFF;

	return crc;
}

/*
 * Balance_To_Minimum
 *
 * Finds the lowest cell voltage in the measured array and enables passive
 * balancing via BAL_SETTINGS (reg 0x16) on every cell whose voltage exceeds
 * that minimum by more than BALANCE_HYSTERESIS_V (10 mV).  All cells at or
 * within the hysteresis band are turned off in the same write, so the entire
 * pack converges toward the weakest cell with a single UART transaction per
 * call.
 *
 * BAL_SETTINGS and PCVM registers use PHYSICAL cell numbering:
 *   BAL_SETTINGS bit n  →  physical cell n (between U_n and U_{n+1})
 *   PCVM_n register     →  physical cell n voltage
 *
 * For a 6S pack using cells 6–11 (U6–U12), Read_Cell_Voltages is called with
 * 12 slots; valid data sits at Cell_Voltages[6..11].  Call this function with
 * a pointer to the first valid cell and the matching bit_offset:
 *   Balance_To_Minimum(&Cell_Voltages[6], 6, 6)
 *
 * bit_offset shifts the BAL bit: cell_voltages[i] → BAL_SETTINGS bit (bit_offset + i).
 * This keeps the call consistent with Enable_Balancing_on_Cell(n) where n is the
 * physical cell number.
 *
 * Safety guard: a cell is never balanced below BALANCE_UV_FLOOR_V (2.5 V),
 * preventing an already-low cell from being discharged further through noise.
 *
 * Parameters:
 *   cell_voltages    – pointer to the first real cell voltage in the array
 *   number_of_cells  – number of cells to consider (max 12)
 *   bit_offset       – physical cell index of cell_voltages[0] (e.g. 6)
 *
 * Returns: the BAL_SETTINGS mask written to the IC (0x000 = all balancing off).
 */
#define BALANCE_HYSTERESIS_V  0.010f   /* 10 mV — prevents rapid on/off cycling  */
#define BALANCE_UV_FLOOR_V    2.500f   /* LG M58T minimum — do not balance below */

uint16_t Balance_To_Minimum(float *cell_voltages, uint8_t number_of_cells, uint8_t bit_offset)
{
	if (number_of_cells == 0 || cell_voltages == NULL)
	{
		Disable_All_Balancing();
		return 0;
	}

	if ((uint8_t)(bit_offset + number_of_cells) > 12)
		number_of_cells = 12 - bit_offset;

	/* Step 1: find the minimum voltage across the supplied cells */
	float min_voltage = cell_voltages[0];
	for (int i = 1; i < number_of_cells; i++)
	{
		if (cell_voltages[i] < min_voltage)
			min_voltage = cell_voltages[i];
	}

	/* Step 2: build the BAL_SETTINGS bitmask.
	 * BAL_SETTINGS bit n maps directly to physical cell n, so
	 * cell_voltages[i] → bit (bit_offset + i). */
	uint16_t mask = 0;
	for (int i = 0; i < number_of_cells; i++)
	{
		if ((cell_voltages[i] > (min_voltage + BALANCE_HYSTERESIS_V))
			&& (cell_voltages[i] >= BALANCE_UV_FLOOR_V))
		{
			mask |= (uint16_t)(1U << (bit_offset + i));
		}
	}

	/* Step 3: write the full mask to BAL_SETTINGS in one UART transaction.
	 * Writing the complete register (on + off bits together) is atomic from
	 * the TLE9012's perspective and avoids partial-update glitches. */
	Balancing_Cells_Selected = mask & 0x0FFF;

	uint8_t Frame[6];
	Frame[0] = 0x1E;
	Frame[1] = 0x80;
	Frame[2] = 0x16;
	Frame[3] = Balancing_Cells_Selected >> 8;
	Frame[4] = 0xFF & Balancing_Cells_Selected;
	Frame[5] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	for (int i = 0; i < sizeof(Frame); i++)
		Frame[i] = msb_first_converter(Frame[i]);

	dma2_stream7_uart_tx_config(Frame, 6);
	while (!g_tx_cmplt) {}
	g_tx_cmplt = 0;
	while (!g_uart_cmplt) {}
	g_uart_cmplt = 0;

	dma2_stream2_uart_rx_config(write_reply_frame, 1);
	while (!g_rx_cmplt) {}
	g_rx_cmplt = 0;

	return Balancing_Cells_Selected;
}

uint16_t Read_General_Diagnostics(void)
{
	uint8_t Frame[4];
	uint8_t Reply[5];
	uint16_t GEN_DIAG;

	Frame[0] = 0x1E;
	Frame[1] = 0x00;
	Frame[2] = 0x0B;
	Frame[3] = CRC8_Calc(Frame, sizeof(Frame) - 1);

	for (int i = 0; i < sizeof(Frame); i++)
	{
		Frame[i] = msb_first_converter(Frame[i]);
	}

	dma2_stream7_uart_tx_config(Frame, 4);

	while (!g_tx_cmplt)
	{

	}
	g_tx_cmplt = 0;

	while (!g_uart_cmplt){}
	g_uart_cmplt = 0;

	dma2_stream2_uart_rx_config(Reply, 5);

	while (!g_rx_cmplt)
	{

	}
	g_rx_cmplt = 0;

	for (int i = 0; i < 5; i++)
	{
		Reply[i] = msb_first_converter(Reply[i]);
	}

	GEN_DIAG = (Reply[2] << 8) | Reply[3];
	return GEN_DIAG;
}



uint8_t msb_first_converter(uint8_t in)
{
	uint8_t MSB = 0;
	uint8_t Reversed_byte = 0;
	for (int i = 0; i < 8; i++)
	{
		//Extract the MSB
		MSB = (in << i) & 0x80;

		//Shift MSB to the right by 7
		MSB = MSB >> 7;

		// OR it with wanted Result
		Reversed_byte |= MSB << i;
	}

	return Reversed_byte;
}


float ADC_CONVERSION (uint16_t adc_raw)
{
	return 5.0f * (float)adc_raw / 65535.0f;
}
