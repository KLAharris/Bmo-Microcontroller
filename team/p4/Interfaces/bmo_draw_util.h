#ifndef BMO_DRAW_UTIL_H
#define BMO_DRAW_UTIL_H

/* Drawing helpers shared by games, apps and the harness menus.
 * Portable: built ONLY on gfx_fill_rect()/bmo_text(), so everything here
 * works unchanged on the real display. */

#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "bmo_gfx_interface.h"

typedef enum { ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT } ArrowDir;

/* Solid arrow centred on (cx, cy); s = half the arrow's length. */
static inline void draw_arrow(int cx, int cy, int s, ArrowDir dir, uint16_t color) {
    int shaft = s / 3;
    switch (dir) {
        case ARROW_UP:
            for (int t = 0; t < s; t++) gfx_fill_rect(cx - (t + 1), cy - s + t, 2 * (t + 1), 1, color);
            gfx_fill_rect(cx - shaft, cy, 2 * shaft, s, color);
            break;
        case ARROW_DOWN:
            for (int t = 0; t < s; t++) gfx_fill_rect(cx - (t + 1), cy + s - 1 - t, 2 * (t + 1), 1, color);
            gfx_fill_rect(cx - shaft, cy - s, 2 * shaft, s, color);
            break;
        case ARROW_LEFT:
            for (int t = 0; t < s; t++) gfx_fill_rect(cx - s + t, cy - (t + 1), 1, 2 * (t + 1), color);
            gfx_fill_rect(cx, cy - shaft, s, 2 * shaft, color);
            break;
        case ARROW_RIGHT:
            for (int t = 0; t < s; t++) gfx_fill_rect(cx + s - 1 - t, cy - (t + 1), 1, 2 * (t + 1), color);
            gfx_fill_rect(cx - s, cy - shaft, s, 2 * shaft, color);
            break;
    }
}

/* Width of a string drawn with bmo_text (12 px per character at the harness's
   default size). Confirm against P1's real font if you use it. */
static inline int draw_text_width(const char *str) { return (int)strlen(str) * 12 - 2; }

static inline void draw_text_centered(int cx, int y, const char *str, uint16_t color) {
    bmo_text(cx - draw_text_width(str) / 2, y, str, color);
}

/* RGB565 colour scaled to `percent` brightness (for dim/idle states) */
static inline uint16_t draw_scaled_color(int r, int g, int b, int percent) {
    return RGB565(r * percent / 100, g * percent / 100, b * percent / 100);
}

/* Filled circle, drawn as one-pixel-high rows */
static inline void draw_fill_circle(int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++) {
        int lim = r * r - dy * dy;
        int dx = 0;
        while ((dx + 1) * (dx + 1) <= lim) dx++;
        gfx_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

/* ---- Text at any size, from a built-in 5x7 font (uppercase, digits and a
   few symbols; lowercase is shown as uppercase). Each character is 5x7
   "pixels" of size `scale`, advancing 6*scale. Uses only gfx_fill_rect, so
   it looks the same on the desktop and on the board. ---- */
static inline void draw_text_scaled(int x, int y, const char *str, uint16_t color, int scale) {
    typedef struct { char c; uint8_t rows[7]; } Glyph;
    static const Glyph font[] = {
        {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
        {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
        {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
        {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
        {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
        {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
        {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
        {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
        {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
        {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
        {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
        {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
        {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
        {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
        {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
        {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
        {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
        {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
        {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
        {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
        {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
        {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
        {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
        {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
        {'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
        {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
        {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
        {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
        {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
        {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
        {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
        {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
        {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
        {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
        {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
        {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
        {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
        {':', {0x00,0x04,0x04,0x00,0x04,0x04,0x00}},
        {'/', {0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
        {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
        {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
        {',', {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}},
        {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
        {'%', {0x18,0x19,0x02,0x04,0x08,0x13,0x03}},
    };
    for (const char *p = str; *p; p++) {
        char ch = (char)toupper((unsigned char)*p);
        for (unsigned i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
            if (font[i].c != ch) continue;
            for (int row = 0; row < 7; row++) {
                int col = 0;
                while (col < 5) {
                    if (font[i].rows[row] & (1 << (4 - col))) {
                        int start = col;
                        while (col < 5 && (font[i].rows[row] & (1 << (4 - col)))) col++;
                        gfx_fill_rect(x + start * scale, y + row * scale, (col - start) * scale, scale, color);
                    } else {
                        col++;
                    }
                }
            }
            break;
        }
        x += 6 * scale;
    }
}

static inline int draw_text_scaled_width(const char *str, int scale) {
    return (int)strlen(str) * 6 * scale - scale;
}

static inline void draw_text_scaled_centered(int cx, int y, const char *str, uint16_t color, int scale) {
    draw_text_scaled(cx - draw_text_scaled_width(str, scale) / 2, y, str, color, scale);
}

/* ---- Seven-segment digit (like a digital clock). Lit segments use `on`,
   unlit ones use `off` (pass the background colour to hide them, or a faint
   colour for the classic "ghost" look). digit outside 0..9 = all off. ---- */
static inline void draw_7seg_digit(int x, int y, int w, int h, int t, int digit,
                                   uint16_t on, uint16_t off) {
    static const uint8_t map[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    uint8_t m = (digit >= 0 && digit <= 9) ? map[digit] : 0;
    int hh = (h - 3 * t) / 2;     /* length of each vertical segment */
    int mid_y = y + t + hh;       /* top of the middle bar */
    gfx_fill_rect(x + t,     y,              w - 2 * t, t,  (m & 0x01) ? on : off); /* a top          */
    gfx_fill_rect(x + w - t, y + t,          t,         hh, (m & 0x02) ? on : off); /* b top-right    */
    gfx_fill_rect(x + w - t, mid_y + t,      t,         hh, (m & 0x04) ? on : off); /* c bottom-right */
    gfx_fill_rect(x + t,     y + h - t,      w - 2 * t, t,  (m & 0x08) ? on : off); /* d bottom       */
    gfx_fill_rect(x,         mid_y + t,      t,         hh, (m & 0x10) ? on : off); /* e bottom-left  */
    gfx_fill_rect(x,         y + t,          t,         hh, (m & 0x20) ? on : off); /* f top-left     */
    gfx_fill_rect(x + t,     mid_y,          w - 2 * t, t,  (m & 0x40) ? on : off); /* g middle       */
}

/* The ":" between digit pairs: two square dots in a column w wide, h tall */
static inline void draw_7seg_colon(int x, int y, int w, int h, int t, uint16_t color) {
    gfx_fill_rect(x + (w - t) / 2, y + h / 3 - t / 2,     t, t, color);
    gfx_fill_rect(x + (w - t) / 2, y + 2 * h / 3 - t / 2, t, t, color);
}

#endif
