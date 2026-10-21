/* gfx_spi.c — temporary implementation of face_gfx.h on the ILI9486 SPI panel.
 *
 * THROWAWAY: when P1 delivers the real driver (DMA2D blend + SPI-DMA push),
 * delete this file. bmo_face.c stays the same.
 *
 * How it works:
 *   gfx_begin(box)  -> start a region: draws go into `region[]` in RAM
 *   gfx_fill_rect() -> fill inside the region (or straight to screen if none)
 *   gfx_blend_sprite() -> software A4 blend into the region
 *                         (no region open: own region, in strips if too big)
 *   gfx_end()       -> push the finished region to the screen in one go
 *
 * Note for P1: when this moves to DMA, region[] must sit in SRAM1
 * (__attribute__((section(".sram1")))), never DTCM, and needs a cache clean
 * before each transfer. Polling SPI (used here) does not care.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")        /* blend loop; the Debug build is -O0 */
#endif

#include "main.h"
#include "lcd_test.h"
#include "face_gfx.h"

#define REGION_MAX_PX   GFX_BOX_MAX_PX       /* biggest box we ever draw */

static uint8_t  region[REGION_MAX_PX * 2] __attribute__((aligned(32)));
static int      rx, ry, rw, rh;              /* current region */
static int      in_region;
static uint16_t screen_bg = 0x0000;          /* colour of the last full-screen fill */

/* ---------------------------------------------------------------- helpers */
static int clip(int *x, int *y, int *w, int *h, int cx, int cy, int cw, int ch)
{
    int x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
    if (x0 < cx) x0 = cx;
    if (y0 < cy) y0 = cy;
    if (x1 > cx + cw) x1 = cx + cw;
    if (y1 > cy + ch) y1 = cy + ch;
    if (x1 <= x0 || y1 <= y0) return 0;
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return 1;
}

static inline uint16_t rd(int i)             { return (uint16_t)((region[2 * i] << 8) | region[2 * i + 1]); }
static inline void     wr(int i, uint16_t c) { region[2 * i] = c >> 8; region[2 * i + 1] = c & 0xFF; }

/* mix fg over bg, a = 0..15 */
static uint16_t mix(uint16_t fg, uint16_t bg, int a)
{
    int r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (15 - a) + 7) / 15;
    int g = (((fg >> 5)  & 0x3F) * a + ((bg >> 5)  & 0x3F) * (15 - a) + 7) / 15;
    int b = (( fg        & 0x1F) * a + ( bg        & 0x1F) * (15 - a) + 7) / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* ---------------------------------------------------------------- contract */
void gfx_init(void)
{
    lcd_init();
    lcd_set_landscape();
}

void gfx_begin(int x, int y, int w, int h)
{
    in_region = 0;
    if (!clip(&x, &y, &w, &h, 0, 0, GFX_W, GFX_H)) return;
    if (w * h > REGION_MAX_PX) return;            /* too big: draw direct */
    rx = x; ry = y; rw = w; rh = h;
    for (int i = 0; i < w * h; i++) wr(i, screen_bg);
    in_region = 1;
}

void gfx_end(void)
{
    if (in_region) lcd_push((uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh, region);
    in_region = 0;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!in_region) {
        if (!clip(&x, &y, &w, &h, 0, 0, GFX_W, GFX_H)) return;
        if (x == 0 && y == 0 && w == GFX_W && h == GFX_H) screen_bg = color;
        lcd_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
        return;
    }
    if (!clip(&x, &y, &w, &h, rx, ry, rw, rh)) return;
    for (int j = 0; j < h; j++) {
        int i = (y - ry + j) * rw + (x - rx);
        for (int k = 0; k < w; k++) wr(i + k, color);
    }
}

static void blend_into_region(const uint8_t *a4, int w, int h, int x, int y, uint16_t color)
{
    /* only the rows and columns of the sprite that fall inside the region */
    int sy0 = ry - y > 0 ? ry - y : 0, sy1 = ry + rh - y < h ? ry + rh - y : h;
    int sx0 = rx - x > 0 ? rx - x : 0, sx1 = rx + rw - x < w ? rx + rw - x : w;

    for (int sy = sy0; sy < sy1; sy++) {
        int py = y + sy;
        for (int sx = sx0; sx < sx1; sx++) {
            int px = x + sx;
            int p = sy * w + sx;
            uint8_t b = a4[p >> 1];
            int a = (p & 1) ? (b & 0x0F) : (b >> 4);   /* high nibble = left pixel */
            if (a == 0) continue;
            int i = (py - ry) * rw + (px - rx);
            wr(i, a == 15 ? color : mix(color, rd(i), a));
        }
    }
}

void gfx_blend_sprite(const uint8_t *a4, int w, int h, int x, int y, uint16_t color)
{
    if (in_region) { blend_into_region(a4, w, h, x, y, color); return; }

    /* No region open: make one around the sprite, in strips of whole rows
     * when the sprite is bigger than region[]. Strips start from screen_bg. */
    int cx = x, cy = y, cw = w, ch = h;
    if (!clip(&cx, &cy, &cw, &ch, 0, 0, GFX_W, GFX_H)) return;
    int rows = REGION_MAX_PX / cw;
    for (int sy = cy; sy < cy + ch; sy += rows) {
        int sh = (cy + ch - sy < rows) ? cy + ch - sy : rows;
        gfx_begin(cx, sy, cw, sh);
        if (!in_region) return;
        blend_into_region(a4, w, h, x, y, color);
        gfx_end();
    }
}
