/*
*******************************************************************************
* EE 329 A7 LPUART FUNCTIONS
*******************************************************************************
* @file           : LPUART.c
* @brief          : LPUART communication with message and escape code processing
* 						  Keeps track of character position for the game
* project         : EE 329 S'25 - Assignment A7
* authors         : Kelvin Shi - kshi04@calpoly.edu
* version         : 1.0
* date            : 2025/05/07
* compiler        : STM32CubeIDE v.1.12.0 Build: 14980_20230301_1550 (UTC)
* target          : NUCLEO-L4A6ZG
* clocks          : 4 MHz MSI to AHB2
* @attention      : (c) 2023 STMicroelectronics.  All rights reserved.
*******************************************************************************
**/

#include "LPUART.h"


/* -----------------------------------------------------------------------------
* function : void LPUART_init(void)
* INs      : none
* OUTs     : none
* action   : Initializes for LPUART communication. Sets up GPIOG pin 7 and 8 as
* 				 transmission and receiving ports despite not using pins. Sets up
* 				 interrupts and sets the baud rate
* authors  : Kelvin Shi - kshi04@calpoly.edu
* 				 John Penvenne - jpenvenn@calpoly.edu
* version  : 1.0
* date     : 2025/05/07
* -------------------------------------------------------------------------- */
void LPUART_init(){
	PWR->CR2 |= (PWR_CR2_IOSV);              // power avail on PG[15:2] (LPUART1)
	RCC->AHB2ENR |= (RCC_AHB2ENR_GPIOGEN);   // enable GPIOG clock
	RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN; // enable LPUART clock bridge

	// setting alternate function for G7 and G8
	GPIOG->AFR[0] &= ~(0x000F << GPIO_AFRL_AFSEL7_Pos); // clear PG7 nibble AF
	GPIOG->AFR[0] |=  (0x0008 << GPIO_AFRL_AFSEL7_Pos); // set PG7 AF = LPUART1_TX
	GPIOG->AFR[1] &= ~(0x000F << GPIO_AFRH_AFSEL8_Pos); // clear PG8 nibble AF
	GPIOG->AFR[1] |=  (0x0008 << GPIO_AFRH_AFSEL8_Pos); // set PG8 AF = LPUART1_RX

	GPIOG->MODER &= ~(GPIO_MODER_MODE7 | GPIO_MODER_MODE8);
	GPIOG->MODER |= ((2 << 14) | (2 << 16));

	GPIOG->OTYPER &= ~(GPIO_OTYPER_OT7); // pushpull output

	GPIOG->PUPDR &= ~(GPIO_PUPDR_PUPD7 | GPIO_PUPDR_PUPD8);
	GPIOG->PUPDR |= (2 << 16); // pull down for input

	GPIOG->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED7_Pos);

	LPUART1->CR1 &= ~(USART_CR1_M1 | USART_CR1_M0); // 8-bit data
	LPUART1->CR1 |= USART_CR1_UE;                   // enable LPUART1
	LPUART1->CR1 |= (USART_CR1_TE | USART_CR1_RE);  // enable xmit & recv
	LPUART1->CR1 |= USART_CR1_RXNEIE;        // enable LPUART1 recv interrupt
	LPUART1->ISR &= ~(USART_ISR_RXNE);       // clear Recv-Not-Empty flag
	LPUART1->BRR = 0x2B622;    // 115.2 kbps @ 80 MHz PCLK1
	NVIC->ISER[2] = (1 << (LPUART1_IRQn & 0x1F));   // enable LPUART1 ISR
	__enable_irq();                          // enable global interrupts
}

/* -----------------------------------------------------------------------------
* function : void LPUART1_IRQHandler(void)
* INs      : none
* OUTs     : none
* action   : Initializes for IRQ handler for the LPUART1. Depending on stage of
* 				 the program, it will echo the user input to the terminal or it will
* 				 update a character position
* authors  : Kelvin Shi - kshi04@calpoly.edu
* version  : 1.0
* date     : 2025/05/07
* -------------------------------------------------------------------------- */
void LPUART1_IRQHandler( void  ) {
   uint8_t charRecv;
   if (LPUART1->ISR & USART_ISR_RXNE){

   }
}


/* -----------------------------------------------------------------------------
* function : void LPUART_print(void)
* INs      : const char* - the message to display
* OUTs     : none
* action   : Output the message to the LPUART transmission terminal to display
* 				 on the on screen terminal
* authors  : John Penvenne - jpenvenn@calpoly.edu
* version  : 1.0
* date     : 2025/05/07
* -------------------------------------------------------------------------- */
void LPUART_print( const char* message ) {
   uint16_t iStrIdx = 0;
   while ( message[iStrIdx] != 0 ) {
      while(!(LPUART1->ISR & USART_ISR_TXE)); // wait for empty xmit buffer
      LPUART1->TDR = message[iStrIdx];        // send this character
	iStrIdx++;                                 // advance index to next char
   }
}


/* -----------------------------------------------------------------------------
* function : void LPUART_ESC_print(void)
* INs      : const char* - escape code sequence
* OUTs     : none
* action   : Performs escape codes passed by the user
* authors  : Kelvin Shi - kshi04@calpoly.edu
* version  : 1.0
* date     : 2025/05/07
* -------------------------------------------------------------------------- */
void LPUART_ESC_print( const char* code ){
	while(!(LPUART1->ISR & USART_ISR_TXE)); // wait for empty xmit buffer
	LPUART1->TDR = '\x1B';       // send escape key
	LPUART_print( code );        // send code

}
