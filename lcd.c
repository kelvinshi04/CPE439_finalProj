/*******************************************************************************
 * EE 329 A3 LCD FUNCTIONS
 *******************************************************************************
 * @file           : lcd.c
 * @brief          : lcd initialization in 4-bit mode, character/string inputs
 * project         : CPE 439 Final Project
 * authors         : Kelvin Shi - kshi04@calpoly.edu
 * version         : 1.0
 * date            : 2025/04/23
 * compiler        : STM32CubeIDE v.1.12.0 Build: 14980_20230301_1550 (UTC)
 * target          : NUCLEO-L4A6ZG
 * clocks          : 4 MHz MSI to AHB2
 * @attention      : (c) 2023 STMicroelectronics.  All rights reserved.
 *******************************************************************************
 * LCD WIRING 4-bit Mode (pinout NUCLEO-L4A6ZG = L496ZG)
 *      peripheral – Nucleo I/O
 *
 * PINOUT:
 * 1  - GND
 * 2  - VDD - +3V3
 * 3  - DNC - NOT CONNECTED
 * 4  - RS  - D66 - PE1
 * 5  - R/W - D67 - PE0
 * 6  - ENA - D55 - PE3
 * 7  - DB0 - NOT CONNECTED
 * 8  - DB1 - NOT CONNECTED
 * 9  - DB2 - NOT CONNECTED
 * 10 - DB3 - NOT CONNECTED
 * 11 - DB4 - D54 - PE4
 * 12 - DB5 - D53 - PE5
 * 13 - DB6 - D52 - PE6
 * 14 - DB7 - D51 - PE7
 * 15 - A+  - D48 - PE2
 * 16 - GND - GND
 *
 *******************************************************************************
 */
#include "lcd.h"
#include "delay.h"

/* -----------------------------------------------------------------------------
 * function : void LCD_init(void)
 * INs      : none
 * OUTs     : none
 * action   : Initialize the STM32 ports to interface with the LCD. enables
 * 			  GPIO D ports, high speed, push-pull outputs, no PUPD resistors
 * 			  Initialize the LCD by legacy wake up call, set into 4-bit mode
 * 			  get LCD ready for display
 * authors  : Kelvin Shi    - kshi04@calpoly.edu
 * 			  John Penvenne - jpenvenn@calpoly.edu
 * version  : 1.0
 * date     : 2025/04/23
 * -------------------------------------------------------------------------- */
void LCD_init( void )  {
	// configure LCD pins
	RCC->AHB2ENR |= (RCC_AHB2ENR_GPIODEN);

	GPIOD->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1 | GPIO_MODER_MODE3
			| GPIO_MODER_MODE4 | GPIO_MODER_MODE5 | GPIO_MODER_MODE6
			| GPIO_MODER_MODE7 | GPIO_MODER_MODE2);

	GPIOD->MODER |= (GPIO_MODER_MODE0_0 | GPIO_MODER_MODE1_0 | GPIO_MODER_MODE3_0
				| GPIO_MODER_MODE4_0 | GPIO_MODER_MODE5_0 | GPIO_MODER_MODE6_0
				| GPIO_MODER_MODE7_0 | GPIO_MODER_MODE2_0);

	// set all outputs to push pull mode
	GPIOD->OTYPER &= ~(GPIO_OTYPER_OT0 | GPIO_OTYPER_OT1 | GPIO_OTYPER_OT3
			| GPIO_OTYPER_OT4 | GPIO_OTYPER_OT5 | GPIO_OTYPER_OT6
			| GPIO_OTYPER_OT7 | GPIO_OTYPER_OT2);

	//set to high speed
	GPIOD->OSPEEDR |= ((3 << GPIO_OSPEEDR_OSPEED0_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED1_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED2_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED3_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED4_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED5_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED6_Pos)
			| (3 << GPIO_OSPEEDR_OSPEED7_Pos));

	// set no PUPD resistors
	GPIOD->PUPDR &= ~(GPIO_PUPDR_PUPD0 | GPIO_PUPDR_PUPD1
			| GPIO_PUPDR_PUPD3 | GPIO_PUPDR_PUPD4
			| GPIO_PUPDR_PUPD5 | GPIO_PUPDR_PUPD6
			| GPIO_PUPDR_PUPD7 | GPIO_PUPDR_PUPD2);

	// set up systick timer
	DWT_Init();
	delay_us( 40000 );                      // power-up wait 40 ms
	for ( int idx = 0; idx < 3; idx++ ) {   // wake up 1,2,3: DATA = 0011 XXXX
		LCD_4b_command( 0x30 );             // HI 4b of 8b cmd, low nibble = X
		delay_us( 4500 );
	}
	LCD_4b_command( 0x20 ); // send LCD into 4 bit mode
	delay_us( 40 );
	LCD_command( 0x28 );    // 4 bit, 2-line, 5x8 dots
	delay_us( 40 );
	LCD_command( 0x0F );    // turn display on with blinking cursor
	delay_us( 40 );
	LCD_command( 0x01 );    // clear display
	delay_us( 1600 );
	LCD_command( 0x06 );    // Entry mode: move cursor right, no shift
	delay_us( 40 );
}

/* -----------------------------------------------------------------------------
 * function : void LCD_pulse_ENA(void)
 * INs      : none
 * OUTs     : none
 * action   : Control the enable signal; important for latching data to LCD
 * authors  : John Penvenne - jpenvenn@calpoly.edu
 * version  : 1.0
 * date     : 2025/04/23
 * -------------------------------------------------------------------------- */
void LCD_pulse_ENA( void )  {
// ENAble line sends command on falling edge
// set to restore default then clear to trigger
	LCD_PORT->ODR   |= ( ENABLE_PIN );         	// ENABLE = HI
	delay_us( 20 );
	LCD_PORT->ODR   &= ~( ENABLE_PIN );         // ENABLE = LOW
	delay_us( 20 );
}

/* -----------------------------------------------------------------------------
 * function : void LCD_4b_command(void)
 * INs      : uint8_t - desired command represented by one byte
 * OUTs     : none
 * action   : LCD commands using high nibble only - used for 'wake-up' 0x30
 *            commands. lower nibble does not matter
 * authors  : John Penvenne - jpenvenn@calpoly.edu
 * version  : 1.0
 * date     : 2025/04/23
 * -------------------------------------------------------------------------- */
void LCD_4b_command( uint8_t command )  {
	LCD_PORT->ODR   &= ~( UPPER_NIBBLE ); 	// clear DATA bits
	LCD_PORT->ODR   |= ( command );         // DATA = command
	delay_us( 20 );
	LCD_pulse_ENA( );
}

/* -----------------------------------------------------------------------------
 * function : void LCD_command(void)
 * INs      : uint8_t - desired command represented by one byte
 * OUTs     : none
 * action   : LCD commands in 4 bit mode. Sends the high nibble first, send
 *            lower nibble second
 * authors  : Kelvin Shi - kshi04@calpoly.edu
 * version  : 1.0
 * date     : 2025/04/23
 * -------------------------------------------------------------------------- */
void LCD_command( uint8_t command )  {
// send command to LCD in 4-bit instruction mode
// HIGH nibble then LOW nibble, timing sensitive
	LCD_PORT->ODR   &= ~( UPPER_NIBBLE );               // isolate cmd bits
	LCD_PORT->ODR   |= ( command & UPPER_NIBBLE );      // HIGH shifted low
	delay_us( 20 );
	LCD_pulse_ENA( );                                   // latch HIGH NIBBLE

	LCD_PORT->ODR   &= ~( UPPER_NIBBLE );               // isolate cmd bits
	LCD_PORT->ODR   |= ( ((command & 0x0F) << 4) & UPPER_NIBBLE ); // LOW nibble
	delay_us( 20 );
	LCD_pulse_ENA( );                                   // latch LOW NIBBLE
}

/* -----------------------------------------------------------------------------
 * function : void LCD_write_char(void)
 * INs      : uint8_t - ASCII value for character to display
 * OUTs     : none
 * action   : turn on register to select to indicate data being sent; display
 * 			  the character passed in on the LCD; assumes all control bits are
 * 			  cleared
 * authors  : John Penvenne - jpenvenn@calpoly.edu
 * version  : 1.0
 * date     : 2025/04/23
 * -------------------------------------------------------------------------- */
void LCD_write_char( uint8_t letter )  {
	LCD_PORT->ODR   |= (RS_PIN);       // RS = HI for data to address
	delay_us( 20 );
	LCD_command( letter );             // character to print
	LCD_PORT->ODR   &= ~(RS_PIN);      // RS = LO
}

/* -----------------------------------------------------------------------------
 * function : void LCD_write_string(void)
 * INs      : char*   - a pointer to the start of the string
 * 			  uint8_t - the length of the string
 * 			  uint8_t - row index
 * 			     0 - Don't change cursor placement
 * 			     1 - Move cursor to first character in row 1
 * 			     2 - Move cursor to first character in row 2
 * OUTs     : none
 * action   : set the cursor based on row flag. iteratively use LCD_write_char()
 * 			  to display all characters in the string
 * authors  : Kelvin Shi - kshi04@calpoly.edu
 * version  : 1.0
 * date     : 2025/04/23
 * -------------------------------------------------------------------------- */
void LCD_write_string(char* string, uint8_t string_len, uint8_t row){
	//set cursor location
	if (row)
		row == 2 ? LCD_command(0xC0) : LCD_command(0x80);
	delay_us(50);

	// iteratively display every character in the string
	for (int i = 0; i < string_len; i++){
		LCD_write_char(string[i]);
		delay_us(50);
	}
}

/* -----------------------------------------------------------------------------
 * function : void LCD_toggleBacklight(uint8_t)
 * INs      : uint8_t - on/off flag
 * OUTs     : none
 * action   : turn the LCD backlight on/off
 * authors  : Kelvin Shi - kshi04@calpoly.edu
 * version  : 1.0
 * date     : 2025/04/23
 * -------------------------------------------------------------------------- */
void LCD_ToggleBacklight(uint8_t status){
	if (status)
		LCD_PORT->ODR |= BACKLIGHT;
	else
		LCD_PORT->ODR &= ~BACKLIGHT;
}



