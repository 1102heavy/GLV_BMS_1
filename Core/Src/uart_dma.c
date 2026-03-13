#include "uart_dma.h"

#define UART2EN					(1U<<17)
#define GPIOAEN					(1U<<0)
#define GPIOCEN					(1U<<2)
#define UART1EN					(1U<<4)
#define UART_HALF_CR3_DUPLEX 	(1U<<3)

#define CR1_TE			(1U<<3)
#define CR1_RE			(1U<<2)
#define CR1_UE			(1U<<13)
#define SR_TXE			(1U<<7)

#define CR3_DMAT		(1U<<7)
#define CR3_DMAR		(1U<<6)
#define SR_TC			(1U<<6)
#define CR1_TCIE		(1U<<6)

#define UART_BAUDRATE	1000000
#define CLK				16000000

#define DMA1EN			    (1U<<21)
#define DMA_SCR_EN  		(1U<<0)
#define DMA_SCR_MINC		(1U<<10)
#define DMA_SCR_PINC		(1U<<9)
#define DMA_SCR_CIRC		(1U<<8)
#define DMA_SCR_TCIE		(1U<<4)
#define DMA_SCR_TEIE		(1U<<2)
#define DMA_SFCR_DMDIS		(1U<<2)

#define DMA2EN				(1U<<22)
#define LIFCR_CDMEIF2		(1U<<18)
#define LIFCR_CTEIF2		(1U<<19)
#define LIFCR_CTCIF2		(1U<<21)


#define HIFCR_CDMEIF5		(1U<<8)
#define HIFCR_CTEIF5		(1U<<9)
#define HIFCR_CTCIF5		(1U<<11)

#define HIFCR_CDMEIF6		(1U<<18)
#define HIFCR_CTEIF6		(1U<<19)
#define HIFCR_CTCIF6		(1U<<21)

#define HIFCR_CDMEIF7		(1U<<24)
#define HIFCR_CTEIF7		(1U<<25)
#define HIFCR_CTCIF7		(1U<<27)

#define HIFSR_TCIF5		(1U<<11)
#define HIFSR_TCIF6		(1U<<21)

#define HIFSR_TCIF7		(1U<<27)
#define LISR_TCIF2		(1U<<21)

#define TIM5EN			(1U<<3)
#define TIM5CEN			(1U<<0)
#define PRESCALE_1US	(CLK / 1000000U - 1U)

static uint16_t compute_uart_bd(uint32_t periph_clk, uint32_t baudrate);
static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate);
void timer5_init();
void charge_interrupt_setup(void);
void charge_interrupt_high(void);
void charge_interrupt_low(void);
uint8_t charge_interrupt_is_high(void);
void uart1_rx_tx_init(void);
void uart1_rx_tx_half_duplex_init(void);
void uart2_rx_tx_init(void);
void uart2_rx_tx_half_duplex_init(void);
void uart1_disable_tx(void);
void uart1_disable_rx(void);
void uart1_enable_tx(void);
void uart1_enable_rx(void);
void dma1_init(void);
void dma2_init(void);
void dma1_stream5_uart_rx_config(uint8_t *buf, uint8_t size);
void dma2_stream2_uart_rx_config(uint8_t *buf, uint8_t size);


char uart_data_buffer[UART_DATA_BUFF_SIZE];

//TLE9012DQU overall Frame Buffer
uint8_t tx_buff[6];

uint8_t g_rx_cmplt;
uint8_t g_tx_cmplt;

uint8_t g_uart_cmplt;


// PC2 = Charge Interrupt output

void charge_interrupt_setup(void)
{
    /* 1. Enable clock for GPIOC */
    RCC->AHB1ENR |= GPIOCEN;

    /* 2. Set PC2 as general-purpose output
       MODER2[1:0] = 01 */
    GPIOC->MODER &= ~(3U << 4);   // clear bits 5:4
    GPIOC->MODER |=  (1U << 4);   // set as output

    /* 3. Set output type = push-pull */
    GPIOC->OTYPER &= ~(1U << 2);

    /* 4. Optional: set speed */
    GPIOC->OSPEEDR &= ~(3U << 4);
    GPIOC->OSPEEDR |=  (1U << 4); // medium speed

    /* 5. Optional: no pull-up / pull-down */
    GPIOC->PUPDR &= ~(3U << 4);

    /* Start low */
    GPIOC->BSRR = (1U << (2 + 16));
}

void charge_interrupt_high(void)
{
    GPIOC->BSRR = (1U << 2);   // set PC2
}

void charge_interrupt_low(void)
{
    GPIOC->BSRR = (1U << (2 + 16));   // reset PC2
}

uint8_t charge_interrupt_is_high(void)
{
    return (GPIOC->ODR & (1U << 2)) ? 1U : 0U;
}


void set_charge_interrupt(void)
{
	GPIOC -> ODR |= (1U<<2);
}

void disable_charge_interrupt(void)
{
	GPIOC -> ODR &= ~(1U<<2);
}

void uart2_rx_tx_init(void)
{
	/*************Configure UART GPIO pin********************/
	/*1.Enable clock access to GPIOA*/
	RCC->AHB1ENR |= GPIOAEN;

	/*2.Set PA2 mode to alternate function mode*/
	GPIOA->MODER &= ~(1U<<4);
	GPIOA->MODER |=	 (1U<<5);

	/*3.Set PA3 mode to alternate function mode*/
	GPIOA->MODER &= ~(1U<<6);
	GPIOA->MODER |=	 (1U<<7);

	/*4.Set PA2 alternate function function type to AF7(UART2_TX)*/
	GPIOA->AFR[0] |= (1U<<8);
	GPIOA->AFR[0] |= (1U<<9);
	GPIOA->AFR[0] |= (1U<<10);
	GPIOA->AFR[0] &= ~(1U<<11);

	/*5.Set PA3 alternate function function type to AF7(UART2_TX)*/
	GPIOA->AFR[0] |= (1U<<12);
	GPIOA->AFR[0] |= (1U<<13);
	GPIOA->AFR[0] |= (1U<<14);
	GPIOA->AFR[0] &= ~(1U<<15);
	/*************Configure UART Module********************/

	//Set Ospeed R to fast for PA2
	GPIOA -> OSPEEDR |=  (1U << 5);
	GPIOA -> OSPEEDR |=  (1U << 4);

	//Set Ospeed R to fast for PA3
	GPIOA -> OSPEEDR |=  (1U << 7);
	GPIOA -> OSPEEDR |= (1U << 6);

	/*6. Enable clock access to UART2*/
	RCC->APB1ENR |= UART2EN;

	/*7. Set baudrate*/
	uart_set_baudrate(CLK,UART_BAUDRATE);

	/*8. Select to use DMA for TX and RX*/
	USART2->CR3 = CR3_DMAT |CR3_DMAR;

	/*9. Set transfer direction*/
	 USART2->CR1 = CR1_TE |CR1_RE;

	/*10.Clear TC flag*/
	 USART2->SR &=~SR_TC;

	/*11.Enable TCIE*/
	 USART2->CR1 |=CR1_TCIE;

	 /*Enabling Stop bit*/
	 USART2 -> CR2 &= ~(1U << 13);
	 USART2 -> CR2 &= ~(1U << 12);

	/*12. Enable uart module*/
	 USART2->CR1 |= CR1_UE;

	 /*13.Enable USART2 interrupt in the NVIC*/
	 NVIC_EnableIRQ(USART2_IRQn);

}

void uart1_rx_tx_init(void)
{
	/*************Configure UART GPIO pin********************/
	/*1.Enable clock access to GPIOA*/
	RCC->AHB1ENR |= GPIOAEN;

	/*2.Set PA9 mode to alternate function mode*/
	GPIOA->MODER &= ~(1U<<18);
	GPIOA->MODER |=	 (1U<<19);

	/*3.Set PA10 mode to alternate function mode*/
	GPIOA->MODER &= ~(1U<<20);
	GPIOA->MODER |=	 (1U<<21);

	/*4.Set PA9 alternate function function type to AF7(UART2_TX)*/
	GPIOA->AFR[1] |= (1U<<4);
	GPIOA->AFR[1] |= (1U<<5);
	GPIOA->AFR[1] |= (1U<<6);
	GPIOA->AFR[1] &= ~(1U<<7);

	/*5.Set PA10 alternate function function type to AF7(UART2_TX)*/
	GPIOA->AFR[1] |= (1U<<8);
	GPIOA->AFR[1] |= (1U<<9);
	GPIOA->AFR[1] |= (1U<<10);
	GPIOA->AFR[1] &= ~(1U<<11);

	//Set Ospeed R to fast for PA9
	GPIOA -> OSPEEDR |= (1U << 19);
	GPIOA -> OSPEEDR |= (1U << 18);

	//Set Ospeed R to fast for PA10
	GPIOA -> OSPEEDR |= (1U << 21);
	GPIOA -> OSPEEDR |= (1U << 20);
	/*************Configure UART Module********************/

	/*6. Enable clock access to UART1*/
	RCC->APB2ENR |= UART1EN;

	/*7. Set baudrate*/
	uart_set_baudrate(CLK,UART_BAUDRATE);

	/*8. Select to use DMA for TX and RX*/
	USART1->CR3 = CR3_DMAT |CR3_DMAR;

	/*9. Set transfer direction*/
	 USART1->CR1 = CR1_TE |CR1_RE;

	/*10.Clear TC flag*/
	 USART1->SR &=~SR_TC;

	/*11.Enable TCIE*/
	 USART1->CR1 |=CR1_TCIE;

	 /*Enabling Stop bit*/
	 USART1 -> CR2 &= ~(1U << 13);
	 USART1 -> CR2 &= ~(1U << 12);

	/*12. Enable uart module*/
	 USART1->CR1 |= CR1_UE;

	 /*13.Enable USART2 interrupt in the NVIC*/
	 NVIC_EnableIRQ(USART1_IRQn);

}

void uart1_rx_tx_half_duplex_init(void)
{
	/*************Configure UART GPIO pin********************/
	/*1.Enable clock access to GPIOA*/
	RCC->AHB1ENR |= GPIOAEN;

	/*2.Set PA9 mode to alternate function mode*/
	GPIOA->MODER &= ~(1U<<18);
	GPIOA->MODER |=	 (1U<<19);

	/*3.Set PA10 mode to alternate function mode*/
	GPIOA->MODER &= ~(1U<<20);
	GPIOA->MODER |=	 (1U<<21);

	/*4.Set PA9 alternate function function type to AF7(UART2_TX)*/
	GPIOA->AFR[1] |= (1U<<4);
	GPIOA->AFR[1] |= (1U<<5);
	GPIOA->AFR[1] |= (1U<<6);
	GPIOA->AFR[1] &= ~(1U<<7);

	/*5.Set PA10 alternate function function type to AF7(UART2_TX)*/
	GPIOA->AFR[1] |= (1U<<8);
	GPIOA->AFR[1] |= (1U<<9);
	GPIOA->AFR[1] |= (1U<<10);
	GPIOA->AFR[1] &= ~(1U<<11);

	//Set Ospeed R to fast for PA9
	GPIOA -> OSPEEDR |= (1U << 19);
	GPIOA -> OSPEEDR |= (1U << 18);

	//Set Ospeed R to fast for PA10
	GPIOA -> OSPEEDR |= (1U << 21);
	GPIOA -> OSPEEDR |= (1U << 20);

	//Open drain mode
	//GPIOA->OTYPER |= (1U << 9);

    GPIOA->PUPDR |= (1u << (18));
	GPIOA->PUPDR &=  ~(1u << (19));   // 01 = pull-up
	/*************Configure UART Module********************/

	/*6. Enable clock access to UART1*/
	RCC->APB2ENR |= UART1EN;

	/*7. Set baudrate*/
	uart_set_baudrate(CLK,UART_BAUDRATE);

	/*8. Select to use DMA for TX and RX*/
	USART1->CR3 = CR3_DMAT |CR3_DMAR;

	/*9. Set transfer direction*/
	 USART1->CR1 = CR1_TE |CR1_RE;

	/*10.Clear TC flag*/
	 USART1->SR &=~SR_TC;

	/*11.Enable TCIE*/
	 USART1->CR1 |=CR1_TCIE;

	 /*Enabling Stop bit*/
	 USART1 -> CR2 &= ~(1U << 13);
	 USART1 -> CR2 &= ~(1U << 12);

	 //ENable Half Duplex
	 USART1 -> CR3 |= UART_HALF_CR3_DUPLEX;

	/*12. Enable uart module*/
	 USART1->CR1 |= CR1_UE;

	 /*13.Enable USART2 interrupt in the NVIC*/
	 NVIC_EnableIRQ(USART1_IRQn);

}

void uart2_rx_tx_half_duplex_init(void)
{

	/*************Configure UART GPIO pin********************/
	/*1.Enable clock access to GPIOA*/
	RCC->AHB1ENR |= GPIOAEN;

	/*2.Set PA2 mode to alternate function mode*/
	GPIOA->MODER &= ~(1U<<4);
	GPIOA->MODER |=	 (1U<<5);

	/*3.Set PA3 mode to alternate function mode*/
	GPIOA->MODER &= ~(1U<<6);
	GPIOA->MODER |=	 (1U<<7);

	/*4.Set PA2 alternate function function type to AF7(UART2_TX)*/
	GPIOA->AFR[0] |= (1U<<8);
	GPIOA->AFR[0] |= (1U<<9);
	GPIOA->AFR[0] |= (1U<<10);
	GPIOA->AFR[0] &= ~(1U<<11);

	/*5.Set PA3 alternate function function type to AF7(UART2_TX)*/
	GPIOA->AFR[0] |= (1U<<12);
	GPIOA->AFR[0] |= (1U<<13);
	GPIOA->AFR[0] |= (1U<<14);
	GPIOA->AFR[0] &= ~(1U<<15);
	/*************Configure UART Module********************/

	//Set Ospeed R to fast for PA2
	GPIOA -> OSPEEDR |=  (1U << 5);
	GPIOA -> OSPEEDR |=  (1U << 4);

	//Set Ospeed R to fast for PA3
	GPIOA -> OSPEEDR |=  (1U << 7);
	GPIOA -> OSPEEDR |= (1U << 6);

	/*6. Enable clock access to UART2*/
	RCC->APB1ENR |= UART2EN;

	/*7. Set baudrate*/
	uart_set_baudrate(CLK,UART_BAUDRATE);

	/*8. Select to use DMA for TX and RX*/
	USART2->CR3 = CR3_DMAT |CR3_DMAR;

	/*9. Set transfer direction*/
	 USART2->CR1 = CR1_TE |CR1_RE;

	/*10.Clear TC flag*/
	 USART2->SR &=~SR_TC;

	/*11.Enable TCIE*/
	 USART2->CR1 |=CR1_TCIE;

	 /*Enabling Stop bit*/
	 USART2 -> CR2 &= ~(1U << 13);
	 USART2 -> CR2 &= ~(1U << 12);

	 //ENable Half Duplex
	 USART2 -> CR3 |= UART_HALF_CR3_DUPLEX;

	/*12. Enable uart module*/
	 USART2->CR1 |= CR1_UE;

	 /*13.Enable USART2 interrupt in the NVIC*/
	 NVIC_EnableIRQ(USART2_IRQn);

}
void uart1_disable_tx(void)
{
	USART1->CR1 &= ~CR1_TE;
}

void uart1_disable_rx(void)
{
	USART1->CR1 &= ~CR1_RE;
}

void uart1_enable_tx(void)
{
	USART1->CR1 |= CR1_TE;
}

void uart1_enable_rx(void)
{
	USART1->CR1 |= CR1_RE;
}


void dma1_init(void)
{
   /*Enable clock access to DMA*/
	RCC->AHB1ENR |=DMA1EN;

	/*Enable DMA Stream6 Interrupt in NVIC*/
	NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

void dma2_init(void)
{
   /*Enable clock access to DMA*/
	RCC->AHB1ENR |=DMA2EN;

	/*Enable DMA Stream6 Interrupt in NVIC*/
	NVIC_EnableIRQ(DMA2_Stream7_IRQn);
}


void dma1_stream5_uart_rx_config(uint8_t *buf, uint8_t len)
{
	/*Disable DMA stream*/
	DMA1_Stream5->CR &=~DMA_SCR_EN;

	/*Wait till DMA Stream is disabled*/
	while((DMA1_Stream5->CR & DMA_SCR_EN)){}

	/*Clear interrupt flags for stream 5*/
	DMA1->HIFCR = HIFCR_CDMEIF5 |HIFCR_CTEIF5|HIFCR_CTCIF5;
	/*Set periph address*/
	DMA1_Stream5->PAR = (uint32_t)(&(USART2->DR));

	/*Set mem address*/
	DMA1_Stream5->M0AR = (uint32_t)(buf);

	/*Set number of transfer*/
	DMA1_Stream5->NDTR = (uint16_t)len;

	/*Select Channel 4*/
	DMA1_Stream5->CR &= ~(1u<<25);
	DMA1_Stream5->CR &= ~(1u<<26);
	DMA1_Stream5->CR |= (1u<<27);

	/*Enable memory addr increment*/
	DMA1_Stream5->CR |=DMA_SCR_MINC;

	/*Enable transfer complete interrupt*/
	DMA1_Stream5->CR |= DMA_SCR_TCIE;

	/*Enable Circular mode*/
	DMA1_Stream5->CR |=DMA_SCR_CIRC;

	/*Set transfer direction : Periph to Mem*/
	DMA1_Stream5->CR &=~(1U<<6);
	DMA1_Stream5->CR &=~(1U<<7);

	/*Enable DMA stream*/
	DMA1_Stream5->CR |= DMA_SCR_EN;

	/*Enable DMA Stream5 Interrupt in NVIC*/
	NVIC_EnableIRQ(DMA1_Stream5_IRQn);

}

void dma2_stream2_uart_rx_config(uint8_t *buf, uint8_t len)
{
	/*Disable DMA stream*/
	DMA2_Stream2->CR &=~DMA_SCR_EN;

	/*Wait till DMA Stream is disabled*/
	while((DMA2_Stream2->CR & DMA_SCR_EN)){}

	/*Clear interrupt flags for stream 5*/
	DMA2->LIFCR = LIFCR_CDMEIF2 |LIFCR_CTEIF2 |LIFCR_CTCIF2;

	/*Set periph address*/
	DMA2_Stream2->PAR = (uint32_t)(&(USART1->DR));

	/*Set mem address*/
	DMA2_Stream2->M0AR = (uint32_t)buf;

	/*Set number of transfer*/
	DMA2_Stream2->NDTR = (uint16_t)len;

	/*Select Channel 4*/
	DMA2_Stream2->CR &= ~(1u<<25);
	DMA2_Stream2->CR &= ~(1u<<26);
	DMA2_Stream2->CR |=  (1u<<27);

	/*Enable memory addr increment*/
	DMA2_Stream2->CR |=DMA_SCR_MINC;

	/*Enable transfer complete interrupt*/
	DMA2_Stream2->CR |= DMA_SCR_TCIE;

	/*Enable Circular mode*/
	DMA2_Stream2->CR |=DMA_SCR_CIRC;

	/*Set transfer direction : Periph to Mem*/
	DMA2_Stream2->CR &=~(1U<<6);
	DMA2_Stream2->CR &=~(1U<<7);

	/*Enable DMA stream*/
	DMA2_Stream2->CR |= DMA_SCR_EN;

	/*Enable DMA Stream5 Interrupt in NVIC*/
	NVIC_EnableIRQ(DMA2_Stream2_IRQn);

}

void dma1_stream6_uart_tx_config(uint8_t *buf, uint32_t msg_len)
{
	/*Disable DMA stream*/
	DMA1_Stream6->CR &=~DMA_SCR_EN;

	/*Wait till  DMA Stream is disabled*/
	while((DMA1_Stream6->CR & DMA_SCR_EN)){}

	/*Clear interrupt flags for stream 6*/
	DMA1->HIFCR = HIFCR_CDMEIF6 |HIFCR_CTEIF6|HIFCR_CTCIF6;

	/*Set periph address*/
	DMA1_Stream6->PAR = (uint32_t)(&(USART2->DR));

	/*Set mem address*/
	DMA1_Stream6->M0AR = (uint32_t)buf;

	/*Set number of transfer*/
	DMA1_Stream6->NDTR = msg_len;

	/*Select Channel 4 (mem to par)*/
	DMA1_Stream6->CR &= ~(1u<<25);
	DMA1_Stream6->CR &= ~(1u<<26);
	DMA1_Stream6->CR |= (1u<<27);

	/*Enable memory addr increment*/
	DMA1_Stream6->CR |=DMA_SCR_MINC;

	/*Set transfer direction :Mem to Periph*/
	DMA1_Stream6->CR |= (1U<<6);
	DMA1_Stream6->CR &=~(1U<<7);

	/*Set transfer complete interrupt*/
	DMA1_Stream6->CR |= DMA_SCR_TCIE;

	/*Enable DMA stream*/
	DMA1_Stream6->CR |= DMA_SCR_EN;

}

void dma2_stream7_uart_tx_config(uint8_t *buf, uint32_t msg_len)
{
	/*Disable DMA stream*/
	DMA2_Stream7->CR &=~DMA_SCR_EN;

	/*Wait till  DMA Stream is disabled*/
	while((DMA2_Stream7->CR & DMA_SCR_EN)){}

	/*Clear interrupt flags for stream 6*/
	DMA2->HIFCR = HIFCR_CDMEIF7 |HIFCR_CTEIF7|HIFCR_CTCIF7;

	/*Set periph address*/
	DMA2_Stream7->PAR = (uint32_t)(&(USART1->DR));

	/*Set mem address*/
	DMA2_Stream7->M0AR = (uint32_t)buf;

	/*Set number of transfer*/
	DMA2_Stream7->NDTR = msg_len;

	/*Select Channel 4 (mem to par)*/
	DMA2_Stream7->CR &= ~(1u<<25);
	DMA2_Stream7->CR &= ~(1u<<26);
	DMA2_Stream7->CR |=  (1u<<27);

	/*Enable memory addr increment*/
	DMA2_Stream7->CR |=DMA_SCR_MINC;

	/*Set transfer direction :Mem to Periph*/
	DMA2_Stream7->CR |= (1U<<6);
	DMA2_Stream7->CR &=~(1U<<7);

	/*Set transfer complete interrupt*/
	DMA2_Stream7->CR |= DMA_SCR_TCIE;

	/*Enable DMA stream*/
	DMA2_Stream7->CR |= DMA_SCR_EN;

}

static uint16_t compute_uart_bd(uint32_t periph_clk, uint32_t baudrate)
{
	return ((periph_clk +( baudrate/2U ))/baudrate);
}


static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate)
{
	USART2->BRR  = compute_uart_bd(periph_clk,baudrate);
	USART1->BRR  = compute_uart_bd(periph_clk,baudrate);
}


void DMA1_Stream6_IRQHandler(void)
{
	if((DMA1->HISR) & HIFSR_TCIF6)
	{
		//do_ssomething
		g_tx_cmplt = 1;
		/*Clear the flag*/
		DMA1->HIFCR |= HIFCR_CTCIF6;
	}
}

void DMA2_Stream7_IRQHandler(void)
{
	if((DMA2->HISR) & HIFSR_TCIF7)
	{
		//do_ssomething
		g_tx_cmplt = 1;
		/*Clear the flag*/
		DMA2->HIFCR |= HIFCR_CTCIF7;
	}
}


void DMA1_Stream5_IRQHandler(void)
{
	if((DMA1->HISR) & HIFSR_TCIF5)
	{

		g_rx_cmplt = 1;

		//do_ssomething


		/*Clear the flag*/
		DMA1->HIFCR |= HIFCR_CTCIF5;
	}
}

void DMA2_Stream2_IRQHandler(void)
{
	if((DMA2->LISR) & LISR_TCIF2)
	{

		g_rx_cmplt = 1;

		//do_ssomething


		/*Clear the flag*/
		DMA2->LIFCR |= LIFCR_CTCIF2;
	}
}

void USART2_IRQHandler(void)
{
	g_uart_cmplt  = 1;

	/*Clear TC interrupt flag*/
	USART2->SR &=~SR_TC;
}

void USART1_IRQHandler(void)
{
	g_uart_cmplt  = 1;

	/*Clear TC interrupt flag*/
	USART1->SR &=~SR_TC;
}

void timer5_init()
{
	//1. Give Clock access to timer 5
	RCC->APB1ENR |= TIM5EN;

	//2. Switch off the counter in the control register
	TIM5 -> CR1 &= ~(TIM5CEN);

	//3.Set Prescaler
	TIM5 -> PSC = (PRESCALE_1US);

	//4.Set ARR
	TIM5 -> ARR = 0xFFFFFFFF;

	TIM5->EGR = TIM_EGR_UG;   // forces an update event

	TIM5->SR  = 0;            // clears UIF raised by that UG write
}

void delay_us(uint32_t us)
{
	//2. Set timer 5 counter to 0
	TIM5 -> CNT = 0;

	//3. Enable timer 5 counter
	TIM5 -> CR1 |= TIM5CEN;

	//4. Wait in while loop for the counter
	while (TIM5 -> CNT < us){}

	//5. Disable the counter
	TIM5 -> CR1 &= ~(TIM5CEN);
}
