/* gfx_font.c — gfx_text() for the whole team (P3 menu, P4 scores).
 *
 * Built only on face_gfx.h primitives (gfx_begin / gfx_fill_rect / gfx_end),
 * so it works unchanged on the SPI stub, the SDL2 desktop stub,
 * and P1's driver later.
 *
 * 5x7 font in a 6x8 cell, scaled by an integer. Lowercase is shown as uppercase.
 * Each character is built in RAM and pushed once: no flicker.
 */
#include "face_gfx.h"
#include "gfx_font_data.h"

void gfx_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    if (!s) return;
    if (scale < 1) scale = 1;

    int cx = x;
    int cw = 6 * scale, ch = 8 * scale;

    for (; *s; s++) {
        char c = *s;
        if (c == '\n') { cx = x; y += ch; continue; }
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c < FONT_FIRST || c > FONT_LAST) c = '?';
        const uint8_t *g = FONT5X7[c - FONT_FIRST];

        gfx_begin(cx, y, cw, ch);
        gfx_fill_rect(cx, y, cw, ch, bg);
        for (int r = 0; r < 7; r++)
            for (int col = 0; col < 5; col++)
                if (g[r] & (0x10 >> col))
                    gfx_fill_rect(cx + col * scale, y + r * scale, scale, scale, fg);
        gfx_end();

        cx += cw;
    }
}

int gfx_text_width(const char *s, int scale)
{
    int n = 0, best = 0;
    if (scale < 1) scale = 1;
    for (; s && *s; s++) {
        if (*s == '\n') { if (n > best) best = n; n = 0; }
        else n++;
    }
    if (n > best) best = n;
    return best * 6 * scale;
}
