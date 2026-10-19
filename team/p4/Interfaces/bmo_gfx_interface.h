#ifndef BMO_GFX_INTERFACE_H
#define BMO_GFX_INTERFACE_H

#include <stdint.h>

/* What games and apps may draw with:   gfx_fill_rect()   and   bmo_text().
 * Games never touch hardware or SDL2 directly.
 *
 * Two builds, one set of games:
 *
 *   DESKTOP (default)      Harness/bmo_gfx_harness.c implements gfx_fill_rect
 *                          with SDL2. Screen size comes from BMO_SCREEN_W /
 *                          BMO_SCREEN_H (default 800x480; build_and_run.sh
 *                          passes 480x320 to preview the SPI test panel).
 *
 *   BOARD (automatic when built by CubeIDE, or -DBMO_USE_FACE_GFX)
 *                          P2's face_gfx.h is the shared drawing contract, so
 *                          gfx_fill_rect, RGB565 and the screen size come from
 *                          there. P1's real driver will implement the same
 *                          functions; nothing here has to change.
 */

#if defined(BMO_USE_FACE_GFX) || defined(STM32F767xx)   /* CubeIDE defines STM32F767xx for the board */

#include "face_gfx.h"            /* gfx_fill_rect(), RGB565(), GFX_W, GFX_H */
#define GFX_WIDTH   GFX_W
#define GFX_HEIGHT  GFX_H

#else

#ifndef BMO_SCREEN_W
#define BMO_SCREEN_W 800
#endif
#ifndef BMO_SCREEN_H
#define BMO_SCREEN_H 480
#endif
#define GFX_WIDTH   BMO_SCREEN_W
#define GFX_HEIGHT  BMO_SCREEN_H

/* Fill an axis-aligned rectangle. Color is RGB565 (16-bit). */
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

#endif /* board / desktop */

/* Draw short text at (x, y), 12 px per character (5x7 font at 2x). Same code
 * on desktop and board: Common/bmo_text.c, built only on gfx_fill_rect(). */
void bmo_text(int x, int y, const char *str, uint16_t color);

#define COLOR_BLACK  RGB565(0, 0, 0)
#define COLOR_WHITE  RGB565(255, 255, 255)
#define COLOR_RED    RGB565(255, 0, 0)
#define COLOR_GREEN  RGB565(0, 255, 0)
#define COLOR_BLUE   RGB565(0, 0, 255)
#define COLOR_YELLOW RGB565(255, 255, 0)

#endif
