/* fake_lcd.c — lcd_test.h on the laptop: a 480x320 RGB565 screen in RAM.
 *
 * Lets the real gfx_spi.c (region buffer, strips, blend) run unchanged.
 * Counts what would go over the SPI wire, so the test can check that face
 * changes stay small.
 */
#include <stdio.h>
#include <stdlib.h>
#include "lcd_test.h"
#include "fake_lcd.h"

uint16_t lcd_w = 320, lcd_h = 480;

uint16_t   lcd_screen[480 * 320];
lcd_stats_t lcd_stats;

static void put(int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= lcd_w || y >= lcd_h) {
        fprintf(stderr, "FAIL: pixel (%d,%d) off screen\n", x, y);
        exit(1);
    }
    lcd_screen[y * 480 + x] = c;
}

void lcd_init(void)          { lcd_w = 320; lcd_h = 480; }
void lcd_set_landscape(void) { lcd_w = 480; lcd_h = 320; }

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) put(x + i, y + j, color);
    lcd_stats.px += (long)w * h;
    if (w == 480 && h == 320) lcd_stats.full_fills++;
}

void lcd_fill(uint16_t color) { lcd_fill_rect(0, 0, lcd_w, lcd_h, color); }

void lcd_push(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *px)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int k = 2 * (j * w + i);
            put(x + i, y + j, (uint16_t)((px[k] << 8) | px[k + 1]));   /* big-endian, as on the wire */
        }
    lcd_stats.px += (long)w * h;
    lcd_stats.pushes++;
    if (w * h > lcd_stats.max_push_px) lcd_stats.max_push_px = w * h;
    if (x < lcd_stats.x0) lcd_stats.x0 = x;
    if (y < lcd_stats.y0) lcd_stats.y0 = y;
    if (x + w > lcd_stats.x1) lcd_stats.x1 = x + w;
    if (y + h > lcd_stats.y1) lcd_stats.y1 = y + h;
}

void lcd_stats_reset(void)
{
    lcd_stats = (lcd_stats_t){ .x0 = 9999, .y0 = 9999 };
}
