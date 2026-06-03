/*
 *******************************************************************************
 * task_lcd.c
 * LCD_task — sole owner of the 2x16 LCD hardware.
 *******************************************************************************
 */

#include <string.h>
#include "app_types.h"
#include "lcd.h"
#include "delay.h"

static void LCD_write_line(char *text, uint8_t row) {
    LCD_write_string(text, LCD_LINE_LEN, row);
}

void LCD_task(void *pvParams) {
    (void)pvParams;

    LCD_ToggleBacklight(1);

    /* Clear DDRAM once at startup so all 32 positions contain 0x20.
     * This eliminates any power-up garbage in positions we haven't
     * written yet. The 0x01 clear command needs 1.6 ms to execute. */
    LCD_command(0x01);
    delay_us(1600);

    LCD_write_line("  Initialising  ", 1);
    LCD_write_line("  Please wait.. ", 2);

    LcdMsg_t msg;

    for (;;) {
        if (xQueueReceive(lcdQueue, &msg, portMAX_DELAY) == pdTRUE) {
            LCD_write_line(msg.line1, 1);
            LCD_write_line(msg.line2, 2);
        }
    }
}
