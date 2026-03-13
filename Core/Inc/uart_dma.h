#ifndef __UART_DMA_H__
#define __UART_DMA_H__
#include <stdint.h>
#include "stm32f4xx.h"

#define UART_DATA_BUFF_SIZE		8

void uart2_rx_tx_init(void);
void dma1_init(void);
void dma1_stream5_uart_rx_config(uint8_t *buf, uint8_t len);
void dma1_stream6_uart_tx_config(uint8_t *buf, uint32_t msg_len);

void uart1_rx_tx_init(void);
void uart1_disable_rx(void);
void uart1_disable_tx(void);
void uart1_enable_tx(void);
void uart1_enable_rx(void);
void dma2_init(void);
void uart1_rx_tx_half_duplex_init(void);
void uart2_rx_tx_half_duplex_init(void);

void dma2_stream2_uart_rx_config(uint8_t *buf, uint8_t len);
void dma2_stream7_uart_tx_config(uint8_t *buf, uint32_t msg_len);

void delay_us(uint32_t us);
void timer5_init();

void charge_interrupt_setup();
void set_charge_interrupt(void);
void disable_charge_interrupt(void);
void charge_interrupt_high(void);
void charge_interrupt_low(void);
uint8_t charge_interrupt_is_high(void);

#endif