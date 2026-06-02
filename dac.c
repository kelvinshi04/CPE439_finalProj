/*
 * dac.c
 *
 *  Created on: May 11, 2025
 *      Author: kelvi
 */
#include "DAC.h"

void DAC_init(){
	SPI_GPIO_setup();
	DAC_GPIO_setup();
	SPI_init();
	GPIOA->BSRR = (GPIO_PIN_1 | GPIO_PIN_2); // no shutdown
}

uint16_t DAC_volt_conv(uint32_t voltage){
	// check range of voltage
	if (voltage > VOLTAGE_RAIL){
		voltage = VOLTAGE_RAIL;
	}
	if (voltage < 0){
		voltage = 0;
	}

	// convert to binary
	float step = VOLTAGE_RAIL/(4095);
	return (uint16_t) (voltage/step);
}

void DAC_write(uint16_t data){
	GPIOA->BSRR = GPIO_PIN_0;
	uint16_t command = CONTROL_BITS;
	command |= (data & 0x0FFF);
	while (!(SPI1->SR & 0x02));
	SPI1->DR = command;
	while (SPI1->SR & SPI_SR_BSY);      // wait for transmission complete
	DAC_update();                   	// pulse CS to latch output
}


void SPI_init( void ) {
   // SPI config as specified @ STM32L4 RM0351 rev.9 p.1459
   // called by or with DAC_init()
   // build control registers CR1 & CR2 for SPI control of peripheral DAC
   // assumes no active SPI xmits & no recv data in process (BSY=0)

   // CR1 (reset value = 0x0000)
   SPI1->CR1 &= ~( SPI_CR1_SPE );             	// disable SPI for config
   SPI1->CR1 &= ~( SPI_CR1_RXONLY );          	// recv-only OFF (both MOSI and MISO is active)
   SPI1->CR1 &= ~( SPI_CR1_LSBFIRST );        	// data bit order MSb:LSb
   SPI1->CR1 &= ~( SPI_CR1_CPOL | SPI_CR1_CPHA ); // SCLK polarity:phase = 0:0
   SPI1->CR1 |=	 SPI_CR1_MSTR;              	// MCU is SPI controller
   SPI1->CR1 &= ~(SPI_CR1_BR);                    // clear baud rate bits
   SPI1->CR1 |=  (1 << SPI_CR1_BR_Pos);           // PCLK/4 = 20 MHz @ 80 MHz

   // CR2 (reset value = 0x0700 : 8b data)
   SPI1->CR2 &= ~( SPI_CR2_TXEIE | SPI_CR2_RXNEIE ); // disable FIFO intrpts
   SPI1->CR2 &= ~( SPI_CR2_FRF);              	// Moto frame format
   SPI1->CR2 |=	 SPI_CR2_NSSP;              	// auto-generate NSS pulse
   SPI1->CR2 |=	 SPI_CR2_DS;                	// 16-bit data
   SPI1->CR2 |=	 SPI_CR2_SSOE;              	// enable SS output

   // CR1
   SPI1->CR1 |=	 SPI_CR1_SPE;               	// re-enable SPI for ops
}

void SPI_GPIO_setup(){
	// enable clock for GPIOA & SPI1
	RCC->AHB2ENR |= (RCC_AHB2ENR_GPIOAEN);                // GPIOA: DAC NSS/SCK/SDO
	RCC->APB2ENR |= (RCC_APB2ENR_SPI1EN);                 // SPI1 port

	/* USER ADD GPIO configuration of MODER/PUPDR/OTYPER/OSPEEDR registers HERE */
	GPIOA->MODER &= ~(GPIO_MODER_MODE4 | GPIO_MODER_MODE5 | GPIO_MODER_MODE7);
	GPIOA->MODER |= ((2 << 8) | (2 << 10) | (2 << 14));
	GPIOA->OTYPER &= ~(GPIO_OTYPER_OT4 | GPIO_OTYPER_OT5 | GPIO_OTYPER_OT7);
	GPIOA->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED4_Pos | GPIO_OSPEEDR_OSPEED5_Pos
			| GPIO_OSPEEDR_OSPEED7_Pos);
	GPIOA->OSPEEDR |= ((3 << GPIO_OSPEEDR_OSPEED4_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED5_Pos) | (3 << GPIO_OSPEEDR_OSPEED7_Pos));

	// SPI NSS
	GPIOA->AFR[0] &= ~((0x000F << GPIO_AFRL_AFSEL4_Pos)); // clear nibble for bit 5 AF
	GPIOA->AFR[0] |=  ((0x0005 << GPIO_AFRL_AFSEL4_Pos)); // set b5 AF to SPI1 (fcn 5)

	// SPI SCK
	GPIOA->AFR[0] &= ~((0x000F << GPIO_AFRL_AFSEL5_Pos)); // clear nibble for bit 5 AF
	GPIOA->AFR[0] |=  ((0x0005 << GPIO_AFRL_AFSEL5_Pos)); // set b5 AF to SPI1 (fcn 5)

	// SPI MOSI
	GPIOA->AFR[0] &= ~((0x000F << GPIO_AFRL_AFSEL7_Pos)); // clear nibble for bit 7 AF
	GPIOA->AFR[0] |=  ((0x0005 << GPIO_AFRL_AFSEL7_Pos)); // set b7 AF to SPI1 (fcn 5)
}

void DAC_GPIO_setup(){
	GPIOA->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1);
	GPIOA->MODER |= ((1 << 0) | (1 << 2));

	GPIOA->OTYPER &= ~(GPIO_OTYPER_OT0 | GPIO_OTYPER_OT1);

	GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD0 | GPIO_PUPDR_PUPD1);

	GPIOA->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED0_Pos | GPIO_OSPEEDR_OSPEED1_Pos);
	GPIOA->OSPEEDR |= ((3 << GPIO_OSPEEDR_OSPEED0_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED1_Pos));
}

void DAC_update(){
	GPIOA->BRR = GPIO_PIN_0;
	for (int i = 0; i < 5; i++);
	GPIOA->BSRR = GPIO_PIN_0;
}
