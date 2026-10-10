#ifndef FAKE_LCD_H
#define FAKE_LCD_H

#include <stdint.h>

typedef struct {
    long px;              /* pixels sent (pushes + fills)       */
    int  pushes;          /* lcd_push calls                     */
    int  max_push_px;     /* biggest single push                */
    int  full_fills;      /* whole-screen fills                 */
    int  x0, y0, x1, y1;  /* bounding box of all pushes         */
} lcd_stats_t;

extern uint16_t    lcd_screen[480 * 320];
extern lcd_stats_t lcd_stats;

void lcd_stats_reset(void);

#endif
