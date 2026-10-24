#ifndef BMO_GFX_INTERFACE_H
#define BMO_GFX_INTERFACE_H

#include <stdint.h>

/* Implemented TWICE:
 *   - Harness/bmo_gfx_harness.c    -> SDL2 window, for desktop testing
 *   - Display/stm32/bmo_gfx_stm32.c -> real FMC/DMA2D driver (P1's code)
 * Game code calls ONLY these two functions. Never touch hardware or
 * SDL2 directly from a game file. */

/* Fill an axis-aligned rectangle. Color is RGB565 (16-bit), matching the
 * real display's pixel format — confirm this with P1. */
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);

/* Draw short text at (x, y) — status/score, not paragraphs. */
void bmo_p4_gfx_text(int x, int y, const char *str, uint16_t color);
#define gfx_text bmo_p4_gfx_text

/* Confirm these match the real panel. */
#define GFX_WIDTH  480
#define GFX_HEIGHT 320

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define COLOR_BLACK  RGB565(0, 0, 0)
#define COLOR_WHITE  RGB565(255, 255, 255)
#define COLOR_RED    RGB565(255, 0, 0)
#define COLOR_GREEN  RGB565(0, 255, 0)
#define COLOR_BLUE   RGB565(0, 0, 255)
#define COLOR_YELLOW RGB565(255, 255, 0)

#endif
