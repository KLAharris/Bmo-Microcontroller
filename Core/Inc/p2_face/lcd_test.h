#ifndef LCD_TEST_H
#define LCD_TEST_H

#include <stdint.h>

/* Current screen size — changes when orientation changes */
extern uint16_t lcd_w;
extern uint16_t lcd_h;

/* RGB565 colours */
#define LCD_BLACK 0x0000
#define LCD_WHITE 0xFFFF
#define LCD_RED   0xF800
#define LCD_GREEN 0x07E0
#define LCD_BLUE  0x001F

void lcd_init(void);                 /* starts in portrait 320 x 480 */
void lcd_set_landscape(void);        /* switch to 480 x 320 */

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_fill(uint16_t color);

/* Send a block of pixels. px = w*h pixels, 2 bytes each, high byte first. */
void lcd_push(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *px);

void lcd_test_cycle(void);           /* colour test, kept for debugging */
void lcd_screen_test(void);          /* corner pattern to fix rotation / colour */

#endif
