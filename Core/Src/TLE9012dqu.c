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
